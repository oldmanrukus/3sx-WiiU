/* Runs the real emlPhdFixEndian() over the real PHD banks on a big-endian CPU
   and re-implements PlaySe()'s lookup path, printing what the sound driver
   would see. Compared against a little-endian reference parse. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "structs.h"
#include "sf33rd/AcrSDK/MiddleWare/PS2/CapSndEng/emlSndEndian.h"
#include "sf33rd/AcrSDK/MiddleWare/PS2/CapSndEng/eflSpuMap.h"

/* wut's console logger, stubbed for the host build. */
void OSReport(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

extern s8 PHD_SE[5088];
extern s8 SpuMap[80];
extern s8 PHD_PL00[1024];

/* verbatim from emlRefPhd.c */
s32 IsSafeHeadChunk(_ps2_head_chunk* p){ return strncmp((const char*)p,"Head",4)==0; }
s32 IsSafeProgChunk(_ps2_prog_chunk* p){ return strncmp((char*)p,"Prog",4)==0; }
s32 IsSafeSmplChunk(_ps2_smpl_chunk* p){ return strncmp((char*)p,"Smpl",4)==0; }
s32 IsSafeVagiChunk(_ps2_vagi_chunk* p){ return strncmp((char*)p,"Vagi",4)==0; }

static s32 GetNumSplit(_ps2_head_chunk* pHEAD, u8 prog) {
    _ps2_prog_chunk* pPROG; _ps2_prog_param* pPPRM; u32 offset;
    if (IsSafeHeadChunk(pHEAD)!=1) return -1;
    pPROG=(_ps2_prog_chunk*)((uintptr_t)pHEAD+(u32)pHEAD->progChunkOffset);
    if (IsSafeProgChunk(pPROG)!=1) return -2;
    if (pPROG->maxProgNum < prog) return -11;
    offset = pPROG->progParamOffset[prog];
    if (offset == (u32)-1) return -11;
    pPPRM=(_ps2_prog_param*)((uintptr_t)pPROG+offset);
    return pPPRM->nSplit;
}

static void dump(const char* name, void* phd, int nprog) {
    _ps2_head_chunk* h = phd;
    emlPhdFixEndian(phd);
    emlPhdFixEndian(phd);   /* must be idempotent */
    printf("%s headerSize=%u prog@%u smpl@%u vagi@%u\n",
           name, h->headerSize, h->progChunkOffset, h->smplChunkOffset, h->vagiChunkOffset);
    _ps2_prog_chunk* pr=(_ps2_prog_chunk*)((uintptr_t)h + h->progChunkOffset);
    _ps2_smpl_chunk* sm=(_ps2_smpl_chunk*)((uintptr_t)h + h->smplChunkOffset);
    _ps2_vagi_chunk* vg=(_ps2_vagi_chunk*)((uintptr_t)h + h->vagiChunkOffset);
    printf("  tags Prog=%d Smpl=%d Vagi=%d maxProg=%u maxSmpl=%u maxVag=%u\n",
           IsSafeProgChunk(pr), IsSafeSmplChunk(sm), IsSafeVagiChunk(vg),
           pr->maxProgNum, sm->maxSmplNum, vg->maxVagInfoNum);
    for (int p=0; p<nprog; p++) printf("  GetNumSplit(prog=%d)=%d\n", p, GetNumSplit(h,(u8)p));
    _ps2_prog_param* pp=(_ps2_prog_param*)((uintptr_t)pr + pr->progParamOffset[0]);
    printf("  prog0 nSplit=%u split[0]: low=%u high=%u sampleIndex=%u bendLow=%u bendHigh=%u\n",
           pp->nSplit, pp->splitBlock[0].lowKey, pp->splitBlock[0].highKey,
           pp->splitBlock[0].sampleIndex, pp->splitBlock[0].bendLow, pp->splitBlock[0].bendHigh);
    _ps2_smpl_param* sp=&sm->smplParam[pp->splitBlock[0].sampleIndex];
    printf("  smpl[%u]: ADSR1=0x%04X ADSR2=0x%04X vagiIndex=%u base=%u\n",
           pp->splitBlock[0].sampleIndex, sp->ADSR1, sp->ADSR2, sp->vagiIndex, sp->base);
    _ps2_vagi_param* vp=&vg->vagiParam[sp->vagiIndex];
    printf("  vagi[%u]: vagOffset=%u vagSize=%u loop=%d rate=%d\n",
           sp->vagiIndex, vp->vagOffset, vp->vagSize, vp->loopFlag, vp->sampleRate);
}

int main(void) {
    printf("endianness: %s\n", (*(u16*)"\1\0" == 0x0100) ? "BIG" : "little");
    dump("PHD_SE",   PHD_SE,   3);
    dump("PHD_PL00", PHD_PL00, 1);

    printf("SpuMap flSpuMapInit=%d\n", flSpuMapInit((PSPUMAP*)SpuMap));
    for (int b = 0; b < 4; b++) {
        printf("  BankAddr[%d]=%u BankSize[%d]=%u\n", b, flSpuMapGetBankAddr(b), b, CurrMap.BankSize[b]);
    }
    return 0;
}
