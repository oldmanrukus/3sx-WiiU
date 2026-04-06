/**
 * @file afs_wiiu.c
 * @brief AFS file reader for Wii U
 *
 * Rewritten to use POSIX open/read/lseek instead of fopen/fread/fseek.
 * The PSP/ASP port uses this approach successfully. The Wii U's WUT
 * supports POSIX I/O via devoptab, and it avoids potential stdio
 * buffering issues that may have caused hangs with fread.
 *
 * The AFS file descriptor is opened once during init and kept open.
 */
#include "port/io/afs.h"
#include "common.h"

#include <coreinit/debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

/* ======================================
 * Constants
 * ====================================== */

#define AFS_MAGIC_BE 0x41465300  /* "AFS\0" read as big-endian on Wii U */
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
    int fd;                      /* POSIX file descriptor, kept open */
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

static AFS afs = { .fd = -1 };
static ReadRequest requests[AFS_MAX_READ_REQUESTS] = { { 0 } };

/* ======================================
 * Helpers: read little-endian from buffer
 * ====================================== */

static uint32_t read_le32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ======================================
 * Initialization
 * ====================================== */

static bool init_afs(const char* file_path) {
    OSReport("[3SX] AFS: Opening %s\n", file_path);

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        OSReport("[3SX] AFS: Cannot open %s\n", file_path);
        return false;
    }

    /* Read header: 4 bytes magic + 4 bytes entry count */
    uint8_t header[8];
    if (read(fd, header, 8) != 8) {
        OSReport("[3SX] AFS: Failed to read header\n");
        close(fd);
        return false;
    }

    /* Check magic — AFS files start with "AFS\0" (0x41 0x46 0x53 0x00) */
    if (header[0] != 0x41 || header[1] != 0x46 ||
        header[2] != 0x53 || header[3] != 0x00) {
        OSReport("[3SX] AFS: Bad magic: %02X %02X %02X %02X\n",
                 header[0], header[1], header[2], header[3]);
        close(fd);
        return false;
    }

    /* Entry count is little-endian */
    uint32_t entry_count = read_le32(&header[4]);
    OSReport("[3SX] AFS: %u entries\n", entry_count);

    afs.entries = (AFSEntry*)calloc(entry_count, sizeof(AFSEntry));
    if (!afs.entries) {
        OSReport("[3SX] AFS: Failed to allocate entry table\n");
        close(fd);
        return false;
    }
    afs.entry_count = entry_count;

    /* Read entry table: 8 bytes per entry (offset + size), all LE32 */
    size_t table_size = entry_count * 8;
    uint8_t* table_buf = (uint8_t*)malloc(table_size);
    if (!table_buf) {
        OSReport("[3SX] AFS: Failed to allocate table buffer\n");
        free(afs.entries);
        close(fd);
        return false;
    }

    ssize_t got = read(fd, table_buf, table_size);
    if (got != (ssize_t)table_size) {
        OSReport("[3SX] AFS: Short read on entry table: %zd / %zu\n", got, table_size);
        free(table_buf);
        free(afs.entries);
        close(fd);
        return false;
    }

    for (unsigned int i = 0; i < entry_count; i++) {
        afs.entries[i].offset = read_le32(&table_buf[i * 8]);
        afs.entries[i].size   = read_le32(&table_buf[i * 8 + 4]);
    }
    free(table_buf);

    /* Try to read file names from attribute section */
    /* Find attribute offset — it's after the last entry pair in the TOC */
    uint8_t attr_buf[8];
    /* The attribute pointer is at offset 8 + entry_count*8 */
    if (read(fd, attr_buf, 8) == 8) {
        uint32_t attr_off  = read_le32(&attr_buf[0]);
        uint32_t attr_size = read_le32(&attr_buf[4]);

        if (attr_off > 0 && attr_size >= entry_count * 48) {
            for (unsigned int i = 0; i < entry_count; i++) {
                lseek(fd, attr_off + i * 48, SEEK_SET);
                char name[AFS_MAX_NAME_LENGTH];
                memset(name, 0, sizeof(name));
                read(fd, name, AFS_MAX_NAME_LENGTH - 1);
                name[AFS_MAX_NAME_LENGTH - 1] = '\0';
                memcpy(afs.entries[i].name, name, AFS_MAX_NAME_LENGTH);
            }
        }
    }

    afs.fd = fd;

    OSReport("[3SX] AFS: Loaded %u entries, fd=%d\n", afs.entry_count, afs.fd);
    return true;
}

bool AFS_Init(const char* file_path) {
    return init_afs(file_path);
}

void AFS_Finish(void) {
    if (afs.fd >= 0) {
        close(afs.fd);
        afs.fd = -1;
    }
    free(afs.entries);
    memset(&afs, 0, sizeof(afs));
    afs.fd = -1;
    memset(requests, 0, sizeof(requests));
}

unsigned int AFS_GetFileCount(void) { return afs.entry_count; }

unsigned int AFS_GetSize(int file_num) {
    if (file_num < 0 || file_num >= (int)afs.entry_count) return 0;
    return afs.entries[file_num].size;
}

/* ======================================
 * Read operations — POSIX lseek + read
 * ====================================== */

void AFS_RunServer(void) {
    if (afs.fd < 0) return;

    for (int i = 0; i < AFS_MAX_READ_REQUESTS; i++) {
        ReadRequest* req = &requests[i];
        if (!req->initialized || req->state != AFS_READ_STATE_READING)
            continue;

        /* Clamp read size to actual file entry size */
        unsigned int file_size = afs.entries[req->file_num].size;
        unsigned int read_size = req->pending_sectors * 2048;
        if (read_size > file_size) read_size = file_size;

        lseek(afs.fd, req->pending_offset, SEEK_SET);
        ssize_t got = read(afs.fd, req->pending_buf, read_size);

        if (got > 0) {
            req->state = AFS_READ_STATE_FINISHED;
        } else {
            req->state = AFS_READ_STATE_ERROR;
            OSReport("[3SX] AFS: RunServer read error file %d: got %zd\n",
                     req->file_num, got);
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
    if (afs.fd < 0) return;

    ReadRequest* req = &requests[handle];

    /* Calculate offset */
    unsigned int offset = afs.entries[req->file_num].offset +
                          req->sector * 2048;

    /* Clamp to actual file size */
    unsigned int file_size = afs.entries[req->file_num].size;
    unsigned int read_size = sectors * 2048;
    if (read_size > file_size) read_size = file_size;

    lseek(afs.fd, offset, SEEK_SET);
    ssize_t got = read(afs.fd, buf, read_size);

    req->sector += sectors;
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
