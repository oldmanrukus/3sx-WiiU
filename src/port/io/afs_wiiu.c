/**
 * @file afs_wiiu.c
 * @brief AFS file reader for Wii U
 *
 * Key difference from original: the AFS file is opened ONCE during
 * init and kept open. The original version did fopen/fclose on every
 * read which may hang or be extremely slow on Wii U's SD card.
 */
#include "port/io/afs.h"
#include "common.h"

#include <coreinit/debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ======================================
 * Constants
 * ====================================== */

#define AFS_MAGIC 0x41465300
#define AFS_MAX_READ_REQUESTS 100
#define AFS_MAX_NAME_LENGTH 32

/* ======================================
 * Types
 * ====================================== */

typedef struct AFSEntry {
    unsigned int offset;
    unsigned int size;
    char name[AFS_MAX_NAME_LENGTH];
} AFSEntry;

typedef struct AFS {
    char* file_path;
    FILE* file_handle;  /* Kept open for the lifetime of the AFS */
    unsigned int entry_count;
    AFSEntry* entries;
} AFS;

typedef struct ReadRequest {
    bool initialized;
    int index;
    int file_num;
    int sector;
    AFSReadState state;
    void* pending_buf;
    int pending_sectors;
    unsigned int pending_offset;
} ReadRequest;

/* ======================================
 * State
 * ====================================== */

static AFS afs = { 0 };
static ReadRequest requests[AFS_MAX_READ_REQUESTS] = { { 0 } };

/* ======================================
 * Helper: read little-endian values from file
 * ====================================== */

static uint32_t fread_be32(FILE* f) {
    uint8_t buf[4];
    fread(buf, 1, 4, f);
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  | buf[3];
}

static uint32_t fread_le32(FILE* f) {
    uint8_t buf[4];
    fread(buf, 1, 4, f);
    return buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/* ======================================
 * Initialization
 * ====================================== */

static bool is_valid_attribute_data(uint32_t attr_off, uint32_t attr_size,
                                     long file_size, uint32_t entries_end,
                                     uint32_t entry_count) {
    if (attr_off == 0 || attr_size == 0) return false;
    if (attr_size > (uint32_t)(file_size - entries_end)) return false;
    if (attr_size < (entry_count * 48)) return false;
    if (attr_off < entries_end) return false;
    if (attr_off > (uint32_t)(file_size - attr_size)) return false;
    return true;
}

static bool init_afs(const char* file_path) {
    size_t path_len = strlen(file_path);
    afs.file_path = (char*)malloc(path_len + 1);
    memcpy(afs.file_path, file_path, path_len + 1);

    FILE* f = fopen(file_path, "rb");
    if (!f) {
        OSReport("[3SX] AFS: Cannot open %s\n", file_path);
        return false;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Check magic */
    uint32_t magic = fread_be32(f);
    if (magic != AFS_MAGIC) {
        OSReport("[3SX] AFS: Bad magic: 0x%08X\n", magic);
        fclose(f);
        return false;
    }

    /* Read entry count */
    afs.entry_count = fread_le32(f);
    afs.entries = (AFSEntry*)calloc(afs.entry_count, sizeof(AFSEntry));

    uint32_t entries_start = 0;
    uint32_t entries_end = 0;

    for (unsigned int i = 0; i < afs.entry_count; i++) {
        afs.entries[i].offset = fread_le32(f);
        afs.entries[i].size   = fread_le32(f);

        if (afs.entries[i].offset != 0) {
            if (entries_start == 0) entries_start = afs.entries[i].offset;
            entries_end = afs.entries[i].offset + afs.entries[i].size;
        }
    }

    /* Try to read attributes (file names) */
    uint32_t attr_off = fread_le32(f);
    uint32_t attr_size = fread_le32(f);
    bool has_attrs = false;

    if (is_valid_attribute_data(attr_off, attr_size, file_size,
                                entries_end, afs.entry_count)) {
        has_attrs = true;
    } else {
        fseek(f, entries_start - 8, SEEK_SET);
        attr_off  = fread_le32(f);
        attr_size = fread_le32(f);
        if (is_valid_attribute_data(attr_off, attr_size, file_size,
                                    entries_end, afs.entry_count)) {
            has_attrs = true;
        }
    }

    for (unsigned int i = 0; i < afs.entry_count; i++) {
        if (afs.entries[i].offset != 0 && has_attrs) {
            fseek(f, attr_off + i * 48, SEEK_SET);
            int j = 0;
            char c;
            do {
                fread(&c, 1, 1, f);
                if (j < AFS_MAX_NAME_LENGTH - 1)
                    afs.entries[i].name[j++] = c;
            } while (c != '\0' && j < AFS_MAX_NAME_LENGTH);
            afs.entries[i].name[AFS_MAX_NAME_LENGTH - 1] = '\0';
        }
    }

    /* Keep the file open for reads — don't fclose */
    afs.file_handle = f;

    OSReport("[3SX] AFS: Loaded %u entries from %s\n", afs.entry_count, file_path);
    return true;
}

bool AFS_Init(const char* file_path) {
    return init_afs(file_path);
}

void AFS_Finish(void) {
    if (afs.file_handle) {
        fclose(afs.file_handle);
        afs.file_handle = NULL;
    }
    free(afs.file_path);
    free(afs.entries);
    memset(&afs, 0, sizeof(afs));
    memset(requests, 0, sizeof(requests));
}

unsigned int AFS_GetFileCount(void) { return afs.entry_count; }

unsigned int AFS_GetSize(int file_num) {
    if (file_num < 0 || file_num >= (int)afs.entry_count) return 0;
    return afs.entries[file_num].size;
}

/* ======================================
 * Direct sync read by file number
 * Used by the simplified gd3rd.c sync path
 * ====================================== */

static bool afs_read_direct(int file_num, void* buf, unsigned int byte_count) {
    if (!afs.file_handle) return false;
    if (file_num < 0 || file_num >= (int)afs.entry_count) return false;

    unsigned int offset = afs.entries[file_num].offset;
    fseek(afs.file_handle, offset, SEEK_SET);
    size_t got = fread(buf, 1, byte_count, afs.file_handle);
    return (got == byte_count);
}

/* ======================================
 * Read operations
 * ====================================== */

void AFS_RunServer(void) {
    if (!afs.file_handle) return;

    for (int i = 0; i < AFS_MAX_READ_REQUESTS; i++) {
        ReadRequest* req = &requests[i];
        if (!req->initialized || req->state != AFS_READ_STATE_READING)
            continue;

        fseek(afs.file_handle, req->pending_offset, SEEK_SET);
        size_t bytes_to_read = req->pending_sectors * 2048;
        size_t bytes_read = fread(req->pending_buf, 1, bytes_to_read, afs.file_handle);

        if (bytes_read == bytes_to_read) {
            req->state = AFS_READ_STATE_FINISHED;
        } else {
            /* Partial read is OK — file might not be sector-aligned */
            if (bytes_read > 0) {
                req->state = AFS_READ_STATE_FINISHED;
            } else {
                req->state = AFS_READ_STATE_ERROR;
                OSReport("[3SX] AFS: Read error file %d: wanted %zu, got %zu\n",
                         req->file_num, bytes_to_read, bytes_read);
            }
        }
    }
}

AFSHandle AFS_Open(int file_num) {
    for (int i = 0; i < AFS_MAX_READ_REQUESTS; i++) {
        ReadRequest* req = &requests[i];
        if (req->initialized) continue;

        req->file_num = file_num;
        req->sector = 0;
        req->index = i;
        req->state = AFS_READ_STATE_IDLE;
        req->initialized = true;
        return i;
    }

    OSReport("[3SX] AFS: No free request slots!\n");
    return AFS_NONE;
}

void AFS_Read(AFSHandle handle, int sectors, void* buf) {
    if (handle < 0 || handle >= AFS_MAX_READ_REQUESTS) return;

    ReadRequest* req = &requests[handle];
    req->pending_buf = buf;
    req->pending_sectors = sectors;
    req->pending_offset = afs.entries[req->file_num].offset +
                          req->sector * 2048;
    req->state = AFS_READ_STATE_READING;
    req->sector += sectors;
}

void AFS_ReadSync(AFSHandle handle, int sectors, void* buf) {
    if (handle < 0 || handle >= AFS_MAX_READ_REQUESTS) return;
    if (!afs.file_handle) return;

    AFS_Read(handle, sectors, buf);

    ReadRequest* req = &requests[handle];

    fseek(afs.file_handle, req->pending_offset, SEEK_SET);
    size_t bytes = sectors * 2048;
    size_t got = fread(buf, 1, bytes, afs.file_handle);

    /* Accept partial reads — file data may not be sector-aligned */
    req->state = (got > 0) ? AFS_READ_STATE_FINISHED :
                              AFS_READ_STATE_ERROR;
}

void AFS_Stop(AFSHandle handle) {
    (void)handle;
}

void AFS_Close(AFSHandle handle) {
    if (handle < 0 || handle >= AFS_MAX_READ_REQUESTS) return;
    memset(&requests[handle], 0, sizeof(ReadRequest));
}

AFSReadState AFS_GetState(AFSHandle handle) {
    if (handle < 0 || handle >= AFS_MAX_READ_REQUESTS)
        return AFS_READ_STATE_ERROR;
    return requests[handle].state;
}

unsigned int AFS_GetSectorCount(AFSHandle handle) {
    if (handle < 0 || handle >= AFS_MAX_READ_REQUESTS) return 0;
    ReadRequest* req = &requests[handle];
    unsigned int size = afs.entries[req->file_num].size;
    return (size + 2048 - 1) / 2048;
}
