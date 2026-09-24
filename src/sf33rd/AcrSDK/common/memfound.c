#include "sf33rd/AcrSDK/common/memfound.h"
#include "common.h"
#include "sf33rd/AcrSDK/common/memmgr.h"
#include "rendering/game_renderer.h"

MEM_BLOCK sysmemblock[4096];
MEM_MGR sysmemmgr;

void mflInit(void* mem_ptr, s32 memsize, s32 memalign) {
    plmemInit(&sysmemmgr, sysmemblock, 0x1000, mem_ptr, memsize, memalign, 1);
}

u32 mflGetSpace() {
    return plmemGetSpace(&sysmemmgr);
}

size_t mflGetFreeSpace() {
    return plmemGetFreeSpace(&sysmemmgr);
}

u32 mflRegisterS(s32 len) {
    return plmemRegisterS(&sysmemmgr, len);
}

u32 mflRegister(s32 len) {
    return plmemRegister(&sysmemmgr, len);
}

void* mflTemporaryUse(s32 len) {
    /* plmemTemporaryUse() compacts the pool when the scratch request doesn't
       fit, which moves every registered block. */
    u8* before = sysmemmgr.memnow;
    void* ptr = plmemTemporaryUse(&sysmemmgr, len);

    if (sysmemmgr.memnow != before) {
        Renderer_RelocateTextures();
    }

    return ptr;
}

void* mflRetrieve(u32 handle) {
    return plmemRetrieve(&sysmemmgr, handle);
}

s32 mflRelease(u32 handle) {
    return plmemRelease(&sysmemmgr, handle);
}

void* mflCompact() {
    void* ptr = plmemCompact(&sysmemmgr);

    /* Every block just moved; backends holding raw pool pointers must refresh. */
    Renderer_RelocateTextures();
    return ptr;
}
