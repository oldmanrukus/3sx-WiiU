/**
 * @file spu_wiiu.c
 * @brief Wii U SPU audio — SDL2 callback output
 *
 * The SPU emulation logic (ADPCM decode, ADSR envelope, voice mixing)
 * is pure C math from the original spu.c. This file replaces only the
 * audio OUTPUT path: instead of SDL3 AudioStream, we use SDL2 callback.
 *
 * Architecture:
 *   - SDL2 audio callback runs on the audio thread
 *   - Callback calls SPU_Tick() to generate samples directly
 *   - EML timer callback runs at 250 Hz within the audio callback
 */
#include "port/sound/spu.h"
#include "common.h"

#include <SDL2/SDL.h>
#include <coreinit/debug.h>
#include <coreinit/mutex.h>

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* ======================================
 * Constants
 * ====================================== */

#define SPU_SAMPLE_RATE 48000

/* ======================================
 * SPU Voice state (from original spu.c)
 * ====================================== */

#define VOICE_COUNT 48

#include "interp_table.inc"

enum {
    ADSR_PHASE_ATTACK,
    ADSR_PHASE_DECAY,
    ADSR_PHASE_SUSTAIN,
    ADSR_PHASE_RELEASE,
    ADSR_PHASE_STOPPED,
};

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define clamp(val, lo, hi) (((val) > (hi)) ? (hi) : (((val) < (lo)) ? (lo) : (val)))

struct AdsrParamCache {
    bool decr;
    bool exp;
    u8 shift;
    s8 step;
    s32 target;
    bool infinite;
};

struct SPU_Voice {
    bool run;
    bool noise;
    bool endx;
    s16 decodeHist[2];
    u32 counter;
    u16 pitch;
    u16* sample;
    u32 ssa;
    u32 nax;
    u32 lsa;
    bool customLoop;
    s32 envx;
    s32 voll, volr;
    u16 adsr1, adsr2;
    u8 adsr_phase;
    u32 adsr_counter;
    struct AdsrParamCache adsr_param;
    s16 decodeBuf[0x40];
    u32 decRPos, decWPos, decLeft;
};

/* Mutex — uses OSMutex on Wii U */
OSMutex spu_mutex;
SDL_Mutex* soundLock = (SDL_Mutex*)&spu_mutex;

static void (*timer_cb)(void);
static struct SPU_Voice voices[VOICE_COUNT];
static u16 ram[(2 * 1024 * 1024) >> 1];
static s16 adpcm_coefs[5][2] = {
    { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 },
};

/* ======================================
 * SPU core logic (identical to original spu.c)
 * ====================================== */

static s16 SPU_ApplyVolume(s16 sample, s32 volume) {
    return (sample * volume) >> 15;
}

static void SPU_VoiceCacheADSR(struct SPU_Voice* v) {
    struct AdsrParamCache* pc = &v->adsr_param;
    switch (v->adsr_phase) {
    case ADSR_PHASE_ATTACK:
        pc->decr = false;
        pc->exp = ((v->adsr1 & 0x8000) != 0);
        pc->shift = (v->adsr1 >> 10) & 0x1f;
        pc->step = 7 - ((v->adsr1 >> 8) & 0x3);
        pc->target = 0x7fff;
        pc->infinite = ((v->adsr1 >> 8) & 0x7f) == 0x7f;
        break;
    case ADSR_PHASE_DECAY:
        pc->decr = true;
        pc->exp = true;
        pc->shift = (v->adsr1 >> 4) & 0xf;
        pc->step = -8;
        pc->target = ((v->adsr1 & 0xf) + 1) << 11;
        pc->infinite = ((v->adsr1 >> 4) & 0xf) == 0xf;
        break;
    case ADSR_PHASE_SUSTAIN:
        pc->decr = ((v->adsr2 & 0x4000) != 0);
        pc->exp = ((v->adsr2 & 0x8000) != 0);
        pc->shift = (v->adsr2 >> 8) & 0x1f;
        pc->step = 7 - ((v->adsr2 >> 6) & 0x3);
        pc->target = 0;
        pc->infinite = ((v->adsr2 >> 6) & 0x7f) == 0x7f;
        if (pc->decr) pc->step = ~pc->step;
        break;
    case ADSR_PHASE_RELEASE:
        pc->decr = true;
        pc->exp = ((v->adsr2 & 0x20) != 0);
        pc->shift = v->adsr2 & 0x1f;
        pc->step = -8;
        pc->target = 0;
        pc->infinite = (v->adsr2 & 0x1f) == 0x1f;
        break;
    }
}

static void SPU_VoiceRunADSR(struct SPU_Voice* v) {
    struct AdsrParamCache* pc = &v->adsr_param;
    u32 counter_inc = 0x8000 >> max(0, pc->shift - 11);
    s32 level_inc = pc->step << max(0, 11 - pc->shift);

    if (pc->exp && !pc->decr && v->envx >= 0x6000) {
        if (pc->shift < 10) level_inc >>= 2;
        else if (pc->shift >= 11) counter_inc >>= 2;
        else { counter_inc >>= 1; level_inc >>= 1; }
    } else if (pc->exp && pc->decr) {
        level_inc = (level_inc * v->envx) >> 15;
    }

    if (!pc->infinite) counter_inc = max(counter_inc, 1);
    v->adsr_counter += counter_inc;

    if (v->adsr_counter & 0x8000) {
        v->adsr_counter = 0;
        v->envx = clamp(v->envx + level_inc, 0, INT16_MAX);
    }

    if (v->adsr_phase == ADSR_PHASE_SUSTAIN) return;

    if ((!pc->decr && v->envx >= pc->target) ||
        (pc->decr && v->envx <= pc->target)) {
        v->adsr_phase++;
        SPU_VoiceCacheADSR(v);
    }

    if (v->adsr_phase > ADSR_PHASE_RELEASE) {
        v->run = false;
    }
}

static void SPU_VoiceDecode(struct SPU_Voice* v) {
    u32 data;
    u16 header, filter, shift;

    if (v->decLeft >= 16) return;

    data = ram[v->nax];
    header = ram[v->nax & ~0x7];
    shift = header & 0xf;
    filter = (header >> 4) & 7;

    for (int i = 0; i < 4; i++) {
        s32 sample = (s16)((data & 0xF) << 12);
        sample >>= shift;
        sample += (adpcm_coefs[filter][0] * v->decodeHist[0]) >> 6;
        sample += (adpcm_coefs[filter][1] * v->decodeHist[1]) >> 6;
        sample = clamp(sample, INT16_MIN, INT16_MAX);
        v->decodeHist[1] = v->decodeHist[0];
        v->decodeHist[0] = (s16)sample;
        v->decodeBuf[v->decWPos] = sample;
        v->decodeBuf[v->decWPos | 0x20] = sample;
        v->decWPos = (v->decWPos + 1) & 0x1f;
        v->decLeft++;
        data >>= 4;
    }

    v->nax = (v->nax + 1) & 0xfffff;

    if ((v->nax & 0x7) == 0) {
        if (header & 0x100) {
            v->nax = v->lsa;
            v->endx = true;
            if ((header & 0x200) == 0) {
                if (!v->noise) {
                    v->envx = 0;
                    v->adsr_phase = ADSR_PHASE_STOPPED;
                    v->run = false;
                }
            }
        }
        header = ram[v->nax & ~0x7];
        if (header & 0x400) v->lsa = v->nax;
        v->nax = (v->nax + 1) & 0xfffff;
    }
}

static void SPU_VoiceTick(struct SPU_Voice* v, s32* output) {
    s32 sample, pitchStep, decInc;
    u32 index;

    SPU_VoiceDecode(v);

    index = (v->counter & 0x0ff0) >> 4;
    sample = 0;
    sample += ((v->decodeBuf[v->decRPos + 0] * interp_table[index][0]) >> 15);
    sample += ((v->decodeBuf[v->decRPos + 1] * interp_table[index][1]) >> 15);
    sample += ((v->decodeBuf[v->decRPos + 2] * interp_table[index][2]) >> 15);
    sample += ((v->decodeBuf[v->decRPos + 3] * interp_table[index][3]) >> 15);

    pitchStep = v->pitch;
    pitchStep = min(pitchStep, 0x3fff);
    v->counter += pitchStep;

    decInc = v->counter >> 12;
    v->counter &= 0xfff;
    v->decRPos = (v->decRPos + decInc) & 0x1f;
    v->decLeft -= decInc;

    sample = SPU_ApplyVolume(sample, v->envx);
    output[0] = SPU_ApplyVolume(sample, v->voll);
    output[1] = SPU_ApplyVolume(sample, v->volr);

    SPU_VoiceRunADSR(v);
}

/* ======================================
 * Public SPU API
 * ====================================== */

void SPU_Tick(s16* output) {
    struct SPU_Voice* v;
    s32 acc[2] = { 0, 0 };
    s32 vout[2] = { 0, 0 };

    for (int i = 0; i < VOICE_COUNT; i++) {
        v = &voices[i];
        if (v->run) {
            SPU_VoiceTick(v, vout);
            acc[0] += vout[0];
            acc[1] += vout[1];
        }
    }

    output[0] = clamp(acc[0], INT16_MIN, INT16_MAX);
    output[1] = clamp(acc[1], INT16_MIN, INT16_MAX);
}

bool SPU_VoiceIsFinished(int vnum) {
    return (voices[vnum].envx == 0 &&
            voices[vnum].adsr_phase != ADSR_PHASE_ATTACK);
}

void SPU_VoiceKeyOff(int vnum) {
    if (voices[vnum].adsr_phase < ADSR_PHASE_RELEASE) {
        voices[vnum].adsr_phase = ADSR_PHASE_RELEASE;
        SPU_VoiceCacheADSR(&voices[vnum]);
    }
}

void SPU_VoiceStop(int vnum) {
    voices[vnum].envx = 0;
    voices[vnum].adsr_phase = ADSR_PHASE_STOPPED;
    voices[vnum].run = false;
}

void SPU_VoiceGetConf(int vnum, struct SPUVConf* conf) {
    struct SPU_Voice* v = &voices[vnum];
    conf->pitch = v->pitch;
    conf->voll = v->voll;
    conf->volr = v->volr;
    conf->adsr1 = v->adsr1;
    conf->adsr2 = v->adsr2;
}

void SPU_VoiceSetConf(int vnum, struct SPUVConf* conf) {
    struct SPU_Voice* v = &voices[vnum];
    v->pitch = conf->pitch;
    v->voll = conf->voll << 1;
    v->volr = conf->volr << 1;
    v->adsr1 = conf->adsr1;
    v->adsr2 = conf->adsr2;
}

void SPU_VoiceStart(int vnum, u32 start_addr) {
    { static int vs_dbg = 0; if (vs_dbg < 5) { OSReport("[3SX] SPU_VoiceStart: v=%d addr=0x%X\n", vnum, start_addr); vs_dbg++; } }
    struct SPU_Voice* v = &voices[vnum];
    u16 header;

    v->ssa = start_addr;
    v->lsa = start_addr;
    v->nax = v->ssa;
    v->run = true;
    v->envx = 0;
    v->adsr_counter = 0;
    v->adsr_phase = ADSR_PHASE_ATTACK;
    SPU_VoiceCacheADSR(v);

    header = ram[v->nax & ~0x7];
    if ((header >> 10) & 1) v->lsa = v->nax;
    v->nax = (v->nax + 1) & 0xfffff;
}

void SPU_Upload(u32 dst, void* src, u32 size) {
    { static int su_dbg = 0; if (su_dbg < 3) { OSReport("[3SX] SPU_Upload: dst=0x%X size=%u\n", dst, size); su_dbg++; } }
    OSLockMutex(&spu_mutex);
    memcpy(&ram[dst >> 1], src, size);
    OSUnlockMutex(&spu_mutex);
}

/* ======================================
 * Initialization
 * ====================================== */

static SDL_AudioDeviceID spu_audio_device = 0;

static void nullcb(void) {}

/* Defined in adx_wiiu.c — registers SPU timer with the shared audio callback */
extern void ADX_RegisterSPUCallback(void (*cb)(void));

void SPU_Init(void (*cb)()) {
    timer_cb = cb ? cb : nullcb;
    memset(voices, 0, sizeof(voices));
    OSInitMutex(&spu_mutex);

    /* Register the EML timer callback with ADX's audio callback.
       SDL2-wiiu only supports one audio device, so SPU piggybacks
       on ADX's existing SDL audio callback for both mixing and timer. */
    ADX_RegisterSPUCallback(timer_cb);

    OSReport("[3SX] SPU audio initialized (mixed into ADX callback)\n");
}
