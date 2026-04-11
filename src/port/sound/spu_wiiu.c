/**
 * @file spu_wiiu.c
 * @brief Wii U SPU audio output — replaces SDL audio in spu.c
 *
 * The SPU emulation logic (ADPCM decode, ADSR envelope, voice mixing)
 * is pure C math from the original spu.c. This file replaces only the
 * audio OUTPUT path: instead of SDL_AudioStream, we use an AXVoice
 * that pulls samples via a callback.
 *
 * Architecture:
 *   - AX callback runs on the audio thread at ~3ms intervals
 *   - Callback calls SPU_Tick() to generate samples into a ring buffer
 *   - AXVoice plays from the ring buffer continuously
 */
#include "port/sound/spu.h"
#include "common.h"

#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <sndcore2/drcvs.h>
#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/mutex.h>
#include <coreinit/memdefaultheap.h>

#include <string.h>
#include <stdlib.h>

/* ======================================
 * Constants
 * ====================================== */

#define SPU_SAMPLE_RATE 48000
#define RING_BUFFER_SAMPLES 4096  /* Must be power of 2 */
#define RING_BUFFER_MASK (RING_BUFFER_SAMPLES - 1)

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

/* AX voice for output */
static AXVoice* ax_voice_l = NULL;
static AXVoice* ax_voice_r = NULL;

/* Ring buffer for generated samples (interleaved L/R s16) */
static s16* ring_buffer = NULL;
static volatile uint32_t ring_write_pos = 0;
static volatile uint32_t ring_read_pos = 0;

/* Separate L/R buffers for AX (it wants mono per voice) */
static s16* ax_buffer_l = NULL;
static s16* ax_buffer_r = NULL;

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
    OSLockMutex(&spu_mutex);
    memcpy(&ram[dst >> 1], src, size);
    OSUnlockMutex(&spu_mutex);
}

/* ======================================
 * AX audio callback
 *
 * Called by the AX audio system every ~3ms.
 * Generates SPU samples and feeds them to AX voice buffers.
 * ====================================== */

static void ax_frame_callback(void) {
    /* AX runs at 48000 Hz, callback every 3ms = 144 samples per call */
    const int samples_per_callback = 144;

    OSLockMutex(&spu_mutex);

    /* EML timer callback at 250 Hz (48000/250 = 192 samples between calls) */
    static int cb_timer = 192;

    for (int i = 0; i < samples_per_callback; i++) {
        s16 out[2];
        SPU_Tick(out);

        uint32_t wp = ring_write_pos & RING_BUFFER_MASK;
        ax_buffer_l[wp] = out[0];
        ax_buffer_r[wp] = out[1];
        ring_write_pos++;

        cb_timer--;
        if (!cb_timer) {
            if (timer_cb) timer_cb();
            cb_timer = 192;
        }
    }

    OSUnlockMutex(&spu_mutex);

    /* Flush CPU cache so AX DMA can read the buffers */
    DCFlushRange(ax_buffer_l, RING_BUFFER_SAMPLES * sizeof(s16));
    DCFlushRange(ax_buffer_r, RING_BUFFER_SAMPLES * sizeof(s16));
}

/* ======================================
 * Initialization
 * ====================================== */

static void nullcb(void) {}

void SPU_Init(void (*cb)()) {
    timer_cb = cb ? cb : nullcb;
    memset(voices, 0, sizeof(voices));
    OSInitMutex(&spu_mutex);

    /* Allocate AX sample buffers in MEM2 (DMA-accessible) */
    ax_buffer_l = (s16*)MEMAllocFromDefaultHeapEx(
        RING_BUFFER_SAMPLES * sizeof(s16), 64);
    ax_buffer_r = (s16*)MEMAllocFromDefaultHeapEx(
        RING_BUFFER_SAMPLES * sizeof(s16), 64);
    memset(ax_buffer_l, 0, RING_BUFFER_SAMPLES * sizeof(s16));
    memset(ax_buffer_r, 0, RING_BUFFER_SAMPLES * sizeof(s16));

    /* Initialize AX audio system (may already be init'd by SDL audio) */
    if (!AXIsInit()) {
        AXInitParams init_params = {
            .renderer = AX_INIT_RENDERER_48KHZ,
            .pipeline = AX_INIT_PIPELINE_SINGLE,
        };
        AXInitWithParams(&init_params);
    }

    /* Register our frame callback */
    AXRegisterAppFrameCallback(ax_frame_callback);

    /* Acquire a voice for left channel */
    ax_voice_l = AXAcquireVoice(31, NULL, NULL);
    if (ax_voice_l) {
        AXVoiceBegin(ax_voice_l);

        AXVoiceOffsets offsets = {
            .dataType   = AX_VOICE_FORMAT_LPCM16,
            .loopingEnabled = AX_VOICE_LOOP_ENABLED,
            .loopOffset = 0,
            .endOffset  = RING_BUFFER_SAMPLES - 1,
            .currentOffset = 0,
            .data       = ax_buffer_l,
        };
        AXSetVoiceOffsets(ax_voice_l, &offsets);

        /* Full volume, centered pan for L channel → left speaker */
        AXVoiceVeData ve = { .volume = 0x8000 };
        AXSetVoiceVe(ax_voice_l, &ve);

        AXVoiceDeviceMixData mix[6] = { 0 };
        mix[0].bus[0].volume = 0x8000; /* Left speaker */
        AXSetVoiceDeviceMix(ax_voice_l, AX_DEVICE_TYPE_TV, 0, mix);
        AXSetVoiceDeviceMix(ax_voice_l, AX_DEVICE_TYPE_DRC, 0, mix);

        AXSetVoiceSrcType(ax_voice_l, AX_VOICE_SRC_TYPE_NONE);
        AXSetVoiceState(ax_voice_l, AX_VOICE_STATE_PLAYING);

        AXVoiceEnd(ax_voice_l);
    }

    /* Acquire a voice for right channel */
    ax_voice_r = AXAcquireVoice(31, NULL, NULL);
    if (ax_voice_r) {
        AXVoiceBegin(ax_voice_r);

        AXVoiceOffsets offsets = {
            .dataType   = AX_VOICE_FORMAT_LPCM16,
            .loopingEnabled = AX_VOICE_LOOP_ENABLED,
            .loopOffset = 0,
            .endOffset  = RING_BUFFER_SAMPLES - 1,
            .currentOffset = 0,
            .data       = ax_buffer_r,
        };
        AXSetVoiceOffsets(ax_voice_r, &offsets);

        AXVoiceVeData ve = { .volume = 0x8000 };
        AXSetVoiceVe(ax_voice_r, &ve);

        AXVoiceDeviceMixData mix[6] = { 0 };
        mix[0].bus[0].volume = 0x0000; /* No left */
        mix[1].bus[0].volume = 0x8000; /* Right speaker */
        AXSetVoiceDeviceMix(ax_voice_r, AX_DEVICE_TYPE_TV, 0, mix);
        AXSetVoiceDeviceMix(ax_voice_r, AX_DEVICE_TYPE_DRC, 0, mix);

        AXSetVoiceSrcType(ax_voice_r, AX_VOICE_SRC_TYPE_NONE);
        AXSetVoiceState(ax_voice_r, AX_VOICE_STATE_PLAYING);

        AXVoiceEnd(ax_voice_r);
    }

    OSReport("[3SX] SPU audio initialized with AX voices\n");
}
