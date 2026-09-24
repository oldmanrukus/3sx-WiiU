/**
 * @file emlSndEndian.c
 * @brief Byte order fixup for the verbatim PS2 sound data blobs.
 *
 * The PHD banks are verbatim PlayStation 2 data: the four-character chunk tags
 * read the same either way, but every numeric field in them is little-endian.
 * On a big-endian host the chunk offsets come out as garbage, so
 * IsSafeProgChunk() fails, PlaySe() gets NumSplit < 0 and no voice is ever
 * keyed on — which is why sound effects are silent rather than distorted.
 *
 * SpuMap has the same problem: its NumPages reads as 0x01000000, so
 * flSpuMapChgPage() bailed out before filling in the per-bank addresses and
 * every bank was left pointing at SpuTopAddr -- each sound bank uploaded on top
 * of the last one.
 *
 * These are static arrays that are registered (and re-registered) many times
 * over a session, so this converts each one in place exactly once and records
 * that it has done so.
 */

#include "sf33rd/AcrSDK/MiddleWare/PS2/CapSndEng/emlSndEndian.h"

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)

#include "common.h"
#include "sf33rd/AcrSDK/MiddleWare/PS2/CapSndEng/eflSpuMap.h"
#include "structs.h"

#define PHD_CONVERTED_MAX 32

static const void* phd_converted[PHD_CONVERTED_MAX];
static s32 phd_converted_count;

static void swap_u32(u32* p) {
    const u32 v = *p;
    *p = ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) |
         ((v & 0xFF000000u) >> 24);
}

static void swap_u16(u16* p) {
    const u16 v = *p;
    *p = (u16)(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
}

/* Byte-wise so the tag can sit in a u32/u64 member without tripping the
   compiler's object-size checks on strncmp(). */
static s32 tag_is(const void* p, const char* tag) {
    const u8* b = p;
    s32 i;

    for (i = 0; tag[i] != '\0'; i++) {
        if (b[i] != (u8)tag[i]) {
            return 0;
        }
    }

    return 1;
}

static s32 already_converted(const void* phd) {
    s32 i;

    for (i = 0; i < phd_converted_count; i++) {
        if (phd_converted[i] == phd) {
            return 1;
        }
    }

    if (phd_converted_count < PHD_CONVERTED_MAX) {
        phd_converted[phd_converted_count++] = phd;
    }

    return 0;
}

/*
 * Chunk offsets are relative to &pHEAD->tag, matching GetPhdParam(). The bank's
 * total size is headerSize; chunkSize is only the size of the Head chunk (32).
 */
static void* chunk_at(_ps2_head_chunk* head, u32 offset, const char* tag) {
    u8* p;

    if (offset < sizeof(_ps2_head_chunk) || offset + 16 > head->headerSize) {
        return NULL;
    }

    p = (u8*)&head->tag + offset;

    if (!tag_is(p, tag)) {
        return NULL;
    }

    return p;
}

void emlPhdFixEndian(void* phd) {
    _ps2_head_chunk* head = phd;
    _ps2_prog_chunk* prog;
    _ps2_smpl_chunk* smpl;
    _ps2_vagi_chunk* vagi;
    u32 i;
    u32 j;

    if (head == NULL || !tag_is(head, "Head")) {
        return;
    }

    if (already_converted(head)) {
        return;
    }

    swap_u32(&head->chunkSize);
    swap_u32(&head->version);
    swap_u32(&head->headerSize);
    swap_u32(&head->bodySize);
    swap_u32(&head->progChunkOffset);
    swap_u32(&head->smplChunkOffset);
    swap_u32(&head->vagiChunkOffset);

    prog = chunk_at(head, head->progChunkOffset, "Prog");

    if (prog != NULL) {
        swap_u32(&prog->chunkSize);
        swap_u32(&prog->maxProgNum);
        swap_u32(&prog->reserved);

        for (i = 0; i <= prog->maxProgNum; i++) {
            _ps2_prog_param* pprm;

            swap_u32(&prog->progParamOffset[i]);

            if (prog->progParamOffset[i] == 0xFFFFFFFFu || prog->progParamOffset[i] >= prog->chunkSize) {
                continue;
            }

            pprm = (_ps2_prog_param*)((u8*)prog + prog->progParamOffset[i]);
            swap_u16(&pprm->reserved);

            for (j = 0; j < pprm->nSplit; j++) {
                swap_u16(&pprm->splitBlock[j].bendLow);
                swap_u16(&pprm->splitBlock[j].bendHigh);
                swap_u16(&pprm->splitBlock[j].sampleIndex);
            }
        }
    }

    smpl = chunk_at(head, head->smplChunkOffset, "Smpl");

    if (smpl != NULL) {
        swap_u32(&smpl->chunkSize);
        swap_u32(&smpl->maxSmplNum);
        swap_u32(&smpl->reserved);

        for (i = 0; i <= smpl->maxSmplNum; i++) {
            swap_u16(&smpl->smplParam[i].ADSR1);
            swap_u16(&smpl->smplParam[i].ADSR2);
            swap_u16(&smpl->smplParam[i].vagiIndex);
        }
    }

    vagi = chunk_at(head, head->vagiChunkOffset, "Vagi");

    if (vagi != NULL) {
        swap_u32(&vagi->chunkSize);
        swap_u32(&vagi->maxVagInfoNum);
        swap_u32(&vagi->reserved);

        for (i = 0; i <= vagi->maxVagInfoNum; i++) {
            swap_u32(&vagi->vagiParam[i].vagOffset);
            swap_u32(&vagi->vagiParam[i].vagSize);
            swap_u32((u32*)&vagi->vagiParam[i].loopFlag);
            swap_u32((u32*)&vagi->vagiParam[i].sampleRate);
        }
    }
}

#endif /* big endian */

/*
 * Head is a tag plus NumPages and a pad word; the per-page bank size tables
 * follow it. Only the numeric fields need swapping.
 */
void emlSpuMapFixEndian(void* map) {
    PSPUMAP* m = map;
    u32 page;
    u32 i;

    if (m == NULL || !tag_is(m, "SPUMAPDT") || already_converted(m)) {
        return;
    }

    swap_u32(&m->Head.NumPages);

    for (page = 0; page < m->Head.NumPages; page++) {
        for (i = 0; i < SPUBANK_MAX; i++) {
            swap_u32(&m->Page[page].BankSize[i]);
        }
    }
}
