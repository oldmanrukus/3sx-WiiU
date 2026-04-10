#include "sf33rd/AcrSDK/ps2/flps2render.h"
#include "common.h"
#include "sf33rd/AcrSDK/ps2/flps2debug.h"
#include "sf33rd/AcrSDK/ps2/flps2etc.h"
#include "sf33rd/AcrSDK/ps2/flps2vram.h"
#include "sf33rd/AcrSDK/ps2/foundaps2.h"

#include "rendering/game_renderer.h"

#include <coreinit/debug.h>

void flPS2SetClearColor(u32 col);
s32 flPS2SendTextureRegister(u32 th);

s32 flSetRenderState(enum _FLSETRENDERSTATE func, u32 value) {
    u32 th;

    switch (func) {
    case FLRENDER_TEXSTAGE0:
    case FLRENDER_TEXSTAGE1:
    case FLRENDER_TEXSTAGE2:
    case FLRENDER_TEXSTAGE3:
        th = value;

        if (func == FLRENDER_TEXSTAGE0) {
            flPS2SendTextureRegister(th);
        }

        break;

    case FLRENDER_BACKCOLOR:
        flPS2SetClearColor(value);
        break;

    default:
        break;
    }

    return 1;
}

void flPS2SetClearColor(u32 col) {
    flPs2State.FrameClearColor = col;
}

s32 flPS2SendTextureRegister(u32 th) {
    static u64 psTexture_data[16] = {
        0x0000000070000007, 0x0000000000000000, 0x1000000000008006, 0x000000000000000E,
        0x0000000000000000, 0x000000000000003B, 0x0000000000000000, 0x0000000000000014,
        0x0000000000000000, 0x0000000000000006, 0x0000000000000000, 0x0000000000000008,
        0x0000000000000000, 0x0000000000000034, 0x0000000000000000, 0x0000000000000036,
    };

    if (!flPS2SetTextureRegister(th,
                                 &psTexture_data[4],
                                 &psTexture_data[6],
                                 &psTexture_data[8],
                                 &psTexture_data[10],
                                 &psTexture_data[12],
                                 &psTexture_data[14],
                                 flSystemRenderOperation)) {
        return 0;
    }

    return 1;
}

s32 flPS2SetTextureRegister(u32 th, u64* texA, u64* tex1, u64* tex0, u64* clamp, u64* miptbp1, u64* miptbp2,
                            u32 render_ope) {
    FLTexture* lpflTexture;

    // FIXME: make sure these checks are made in Renderer_SetTexture

    if (!LO_16_BITS(th) || (LO_16_BITS(th) > FL_TEXTURE_MAX)) {
        static int tex_err1 = 0;
        if (tex_err1 < 5) { OSReport("[3SX] WARN flPS2SetTextureRegister: bad tex handle lo=%u (max=%d), skipping draw\n", LO_16_BITS(th), FL_TEXTURE_MAX); tex_err1++; }
        return 0;
    }

    lpflTexture = &flTexture[LO_16_BITS(th) - 1];

    if (lpflTexture->desc & 0x4) {
        if (!HI_16_BITS(th) || HI_16_BITS(th) > FL_PALETTE_MAX) {
            static int tex_err2 = 0;
            if (tex_err2 < 5) { OSReport("[3SX] WARN flPS2SetTextureRegister: bad pal handle hi=%u (max=%d) for tex lo=%u desc=0x%x, skipping draw\n", HI_16_BITS(th), FL_PALETTE_MAX, LO_16_BITS(th), lpflTexture->desc); tex_err2++; }
            return 0;
        }
    }

    Renderer_SetTexture(th);
    return 1;
}

f32 flPS2ConvScreenFZ(f32 z) {
    z -= 1.0f;
    z = z * -0.5f;
    z *= flPs2State.ZBuffMax;

    return z;
}
