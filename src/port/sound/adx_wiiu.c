/**
 * @file adx_wiiu.c
 * @brief Standalone ADX audio decoder for Wii U — uses SDL2 callback audio
 *
 * CRI ADX ADPCM decoder with SDL2 callback-based audio output.
 * Uses a ring buffer between the game thread (decoder) and the audio
 * callback thread (SDL/AX).
 */
#include "port/sound/adx.h"
#include "port/sound/spu.h"
#include "port/io/afs.h"
#include "port/utils.h"
#include "sf33rd/Source/Game/io/gd3rd.h"

#include <coreinit/debug.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ADX_SAMPLE_RATE    48000
#define ADX_CHANNELS       2
#define TRACKS_MAX         10

/* Ring buffer: must be power of 2. Stereo interleaved s16 samples. */
#define RING_SAMPLES       (65536 * 2)
#define RING_MASK          (RING_SAMPLES - 1)
#define REFILL_THRESHOLD   (ADX_SAMPLE_RATE / 4 * ADX_CHANNELS)

typedef struct ADXCoeffs { int coeff1, coeff2; } ADXCoeffs;

typedef struct ADXDecoder {
    int16_t prev1[2], prev2[2];
    int channels, sample_rate, block_size, samples_per_block, header_size;
    ADXCoeffs coeffs;
} ADXDecoder;

typedef struct ADXLoopInfo {
    bool enabled;
    int start_sample, end_sample;
    int16_t* loop_buffer;
    int loop_buffer_size, loop_position;
} ADXLoopInfo;

typedef struct ADXTrack {
    uint8_t* data; int size; bool should_free;
    int read_offset, decoded_samples;
    ADXDecoder decoder; ADXLoopInfo loop;
} ADXTrack;

/* Ring buffer */
static int16_t ring_buf[RING_SAMPLES];
static volatile int ring_wpos = 0;
static volatile int ring_rpos = 0;

static int ring_used(void) { int u = ring_wpos - ring_rpos; return u < 0 ? u + RING_SAMPLES : u; }
static int ring_free(void) { return RING_SAMPLES - 1 - ring_used(); }

static void ring_write_stereo(int16_t l, int16_t r) {
    if (ring_free() < 2) return;
    ring_buf[ring_wpos] = l; ring_wpos = (ring_wpos + 1) & RING_MASK;
    ring_buf[ring_wpos] = r; ring_wpos = (ring_wpos + 1) & RING_MASK;
}

static void ring_clear(void) { ring_rpos = ring_wpos = 0; memset(ring_buf, 0, sizeof(ring_buf)); }

/* SPU timer callback — called at 250Hz from audio thread */
static void (*spu_timer_cb)(void) = NULL;
static int spu_cb_timer = 192; /* 48000/250 = 192 samples between timer calls */

static void audio_callback(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    int16_t* out = (int16_t*)stream;
    int samples = len / sizeof(int16_t) / 2; /* stereo sample pairs */

    for (int i = 0; i < samples; i++) {
        /* ADX BGM from ring buffer */
        int32_t l = 0, r = 0;
        if (ring_rpos != ring_wpos) {
            l = ring_buf[ring_rpos];
            ring_rpos = (ring_rpos + 1) & RING_MASK;
            r = ring_buf[ring_rpos];
            ring_rpos = (ring_rpos + 1) & RING_MASK;
        }

        /* SPU SFX — mix on top */
        int16_t spu_out[2];
        SPU_Tick(spu_out);
        l += spu_out[0];
        r += spu_out[1];

        /* Clamp to s16 range */
        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;

        out[i * 2]     = (int16_t)l;
        out[i * 2 + 1] = (int16_t)r;

        /* EML timer callback at 250 Hz */
        spu_cb_timer--;
        if (!spu_cb_timer) {
            if (spu_timer_cb) spu_timer_cb();
            spu_cb_timer = 192;
        }
    }
}

static SDL_AudioDeviceID audio_device = 0;
static ADXTrack tracks[TRACKS_MAX] = {0};
static int num_tracks = 0, first_track_index = 0;
static bool has_tracks = false, is_paused = true;
static float output_gain = 1.0f;

/* --- Header parsing --- */

static uint16_t read_be16(const uint8_t* p) { return ((uint16_t)p[0]<<8)|p[1]; }
static uint32_t read_be32(const uint8_t* p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

static void compute_adx_coefficients(int cutoff, int rate, ADXCoeffs* out) {
    double a = sqrt(2.0) - cos(2.0*M_PI*cutoff/rate);
    double b = sqrt(2.0) - 1.0;
    double c = (a - sqrt((a+b)*(a-b))) / b;
    out->coeff1 = (int)(c*2.0*4096.0);
    out->coeff2 = (int)(-(c*c)*4096.0);
}

static bool parse_adx_header(const uint8_t* data, int size, ADXDecoder* dec, ADXLoopInfo* loop) {
    if (size < 0x20 || data[0] != 0x80 || data[1] != 0x00) return false;
    dec->header_size = read_be16(data+2) + 4;
    if (data[4] != 3) { OSReport("[3SX] ADX: bad encoding %d\n", data[4]); return false; }
    dec->block_size = data[5]; dec->channels = data[7];
    dec->sample_rate = read_be32(data+8);
    dec->samples_per_block = (dec->block_size - 2) * 2;
    uint16_t cutoff = read_be16(data+16); if (!cutoff) cutoff = 500;
    compute_adx_coefficients(cutoff, dec->sample_rate, &dec->coeffs);
    OSReport("[3SX] ADX: cutoff=%d c1=%d c2=%d\n", cutoff, dec->coeffs.coeff1, dec->coeffs.coeff2);
    memset(dec->prev1, 0, sizeof(dec->prev1)); memset(dec->prev2, 0, sizeof(dec->prev2));
    memset(loop, 0, sizeof(*loop));
    uint8_t ver = data[0x12];
    if (ver == 3 && read_be16(data+0x16) == 1) {
        loop->enabled = true; loop->start_sample = read_be32(data+0x1C); loop->end_sample = read_be32(data+0x24);
    } else if (ver == 4 && read_be32(data+0x24) == 1) {
        loop->enabled = true; loop->start_sample = read_be32(data+0x28); loop->end_sample = read_be32(data+0x30);
    }
    if (loop->enabled) {
        int n = loop->end_sample - loop->start_sample;
        loop->loop_buffer_size = n * dec->channels;
        loop->loop_buffer = (int16_t*)malloc(loop->loop_buffer_size * sizeof(int16_t));
        loop->loop_position = 0;
    }
    OSReport("[3SX] ADX: ch=%d rate=%d blk=%d ver=%d loop=%d hdr=%d\n", dec->channels, dec->sample_rate, dec->block_size, ver, loop->enabled, dec->header_size);
    if (dec->header_size + 8 <= size) {
        const uint8_t* a = data + dec->header_size;
        OSReport("[3SX] ADX audio@%d: %02X %02X %02X %02X %02X %02X %02X %02X\n",
            dec->header_size, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
    return true;
}

/* --- Block decoding --- */

static int decode_adx_block(ADXDecoder* dec, const uint8_t* block, int ch, int16_t* out, int max) {
    int scale = read_be16(block); const uint8_t* nib = block+2;
    int n=0, c1=dec->coeffs.coeff1, c2=dec->coeffs.coeff2;
    int32_t p1=dec->prev1[ch], p2=dec->prev2[ch];
    for (int i=0; i<dec->block_size-2 && n<max; i++) {
        for (int j=0; j<2 && n<max; j++) {
            int v = j==0 ? (nib[i]>>4)&0xF : nib[i]&0xF;
            if (v&8) v -= 16;
            int32_t s = v * scale + ((c1*p1 + c2*p2) >> 12);
            if (s>32767) s=32767; if (s<-32768) s=-32768;
            out[n++] = (int16_t)s; p2=p1; p1=s;
        }
    }
    dec->prev1[ch]=p1; dec->prev2[ch]=p2;
    return n;
}

/* --- Track decode to ring --- */

static int decode_track_to_ring(ADXTrack* t, int max) {
    ADXDecoder* d = &t->decoder; int out=0;
    int16_t bl[64], br[64];
    static int dec_dbg = 0;
    while (out < max && ring_free() >= 2) {
        if (t->read_offset + d->block_size*d->channels > t->size) break;
        if (t->loop.enabled && t->decoded_samples >= t->loop.end_sample) break;
        if (d->channels == 2) {
            int n = decode_adx_block(d, t->data+t->read_offset, 0, bl, d->samples_per_block);
            t->read_offset += d->block_size;
            decode_adx_block(d, t->data+t->read_offset, 1, br, n);
            t->read_offset += d->block_size;
            if (dec_dbg < 3000 && n > 0) {
                dec_dbg++;
                if (dec_dbg <= 3) {
                    const uint8_t* raw = t->data + t->read_offset - d->block_size * 2;
                    OSReport("[3SX] BLK%d @%d: %02X%02X %02X%02X%02X%02X | %02X%02X %02X%02X%02X%02X\n",
                        dec_dbg, (int)(raw - t->data),
                        raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                        raw[18], raw[19], raw[20], raw[21], raw[22], raw[23]);
                }
                if ((dec_dbg % 500) == 0) {
                    int16_t maxval = 0;
                    for (int i=0; i<n; i++) { if (bl[i]>maxval) maxval=bl[i]; if (-bl[i]>maxval) maxval=-bl[i]; }
                    OSReport("[3SX] DEC: blk=%d max=%d scale=%d\n", dec_dbg, maxval, read_be16(t->data+t->read_offset-d->block_size*2));
                }
            }
            for (int i=0; i<n && out<max && ring_free()>=2; i++) {
                int16_t l=(int16_t)(bl[i]*output_gain), r=(int16_t)(br[i]*output_gain);
                ring_write_stereo(l, r); out++;
                if (t->loop.enabled && t->decoded_samples+i >= t->loop.start_sample && t->decoded_samples+i < t->loop.end_sample) {
                    int li = (t->decoded_samples+i - t->loop.start_sample)*2;
                    if (li+1 < t->loop.loop_buffer_size) { t->loop.loop_buffer[li]=l; t->loop.loop_buffer[li+1]=r; }
                }
            }
            t->decoded_samples += n;
        } else {
            int n = decode_adx_block(d, t->data+t->read_offset, 0, bl, d->samples_per_block);
            t->read_offset += d->block_size;
            for (int i=0; i<n && out<max && ring_free()>=2; i++) {
                int16_t s=(int16_t)(bl[i]*output_gain); ring_write_stereo(s, s); out++;
            }
            t->decoded_samples += n;
        }
    }
    return out;
}

static bool track_exhausted(ADXTrack* t) { return !t->loop.enabled && t->read_offset >= t->size; }
static bool track_loop_filled(ADXTrack* t) { return t->loop.enabled && t->decoded_samples >= t->loop.end_sample; }

static int play_loop_to_ring(ADXTrack* t, int max) {
    if (!t->loop.enabled || !t->loop.loop_buffer) return 0;
    int total = t->loop.end_sample - t->loop.start_sample, n=0;
    while (n < max && ring_free() >= 2) {
        int li = t->loop.loop_position*2;
        ring_write_stereo((int16_t)(t->loop.loop_buffer[li]*output_gain),
                          (int16_t)(t->loop.loop_buffer[li+1]*output_gain));
        n++; t->loop.loop_position++;
        if (t->loop.loop_position >= total) t->loop.loop_position = 0;
    }
    return n;
}

/* --- File loading --- */

static void* load_afs_file(int id, int* sz) {
    unsigned int fsz = fsGetFileSize(id); *sz = fsz;
    size_t bsz = (fsz+2047)&~2047; void* b = malloc(bsz);
    AFSHandle h = AFS_Open(id); AFS_ReadSync(h, fsCalSectorSize(fsz), b); AFS_Close(h);
    return b;
}

/* --- Track mgmt --- */

static void track_init(ADXTrack* t, int fid, void* buf, size_t bsz, bool loop) {
    memset(t, 0, sizeof(*t));
    if (fid != -1) { t->data = load_afs_file(fid, &t->size); t->should_free = true; }
    else { t->data = buf; t->size = bsz; t->should_free = false; }
    if (!parse_adx_header(t->data, t->size, &t->decoder, &t->loop)) { OSReport("[3SX] ADX: bad header\n"); return; }
    if (!loop) t->loop.enabled = false;
    t->read_offset = t->decoder.header_size; t->decoded_samples = 0;
    decode_track_to_ring(t, t->decoder.sample_rate / 4);
}

static void track_destroy(ADXTrack* t) {
    if (t->loop.loop_buffer) free(t->loop.loop_buffer);
    if (t->should_free && t->data) free(t->data);
    memset(t, 0, sizeof(*t));
}

static ADXTrack* alloc_track(void) {
    int i = (first_track_index + num_tracks) % TRACKS_MAX;
    num_tracks++; has_tracks = true; return &tracks[i];
}

/* --- Public API --- */

void ADX_ProcessTracks(void) {
    if (is_paused || !has_tracks) return;

    int used = ring_used();

    {
        static int pt_log = 0;
        if ((pt_log++ % 60) == 0 && pt_log < 1200) {
            OSReport("[3SX] PT: used=%d tracks=%d\n", used, num_tracks);
        }
    }

    if (used > REFILL_THRESHOLD) return;
    int first=first_track_index, count=num_tracks;
    for (int i=0; i<count; i++) {
        int j = (first+i)%TRACKS_MAX; ADXTrack* t = &tracks[j];
        if (!track_loop_filled(t) && !track_exhausted(t)) decode_track_to_ring(t, t->decoder.sample_rate/60);
        if (track_loop_filled(t)) { play_loop_to_ring(t, t->decoder.sample_rate/60); break; }
        if (!track_exhausted(t)) break;
        track_destroy(t); num_tracks--;
        if (num_tracks>0) first_track_index++; else first_track_index=0;
    }
}

void ADX_Init(void) {
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) { OSReport("[3SX] ADX: SDL audio init fail: %s\n", SDL_GetError()); return; }
    }
    ring_clear();
    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq = ADX_SAMPLE_RATE; want.format = AUDIO_S16SYS;
    want.channels = ADX_CHANNELS; want.samples = 512;
    want.callback = audio_callback; want.userdata = NULL;
    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_device) { OSReport("[3SX] ADX: open fail: %s\n", SDL_GetError()); }
    else { OSReport("[3SX] ADX: OK drv=%s dev=%u freq=%d ch=%d\n", SDL_GetCurrentAudioDriver()?SDL_GetCurrentAudioDriver():"?", audio_device, have.freq, have.channels); SDL_PauseAudioDevice(audio_device, 0); }
}

void ADX_Exit(void) { ADX_Stop(); if (audio_device) { SDL_CloseAudioDevice(audio_device); audio_device=0; } }

void ADX_Stop(void) {
    is_paused = true; ring_clear();
    for (int i=0; i<num_tracks; i++) { int j=(first_track_index+i)%TRACKS_MAX; track_destroy(&tracks[j]); }
    num_tracks=0; first_track_index=0; has_tracks=false;
}

int ADX_IsPaused(void) { return is_paused; }
void ADX_Pause(int p) { is_paused=p; if (audio_device) SDL_PauseAudioDevice(audio_device, p); }
void ADX_StartSeamless(void) { is_paused=false; if (audio_device) SDL_PauseAudioDevice(audio_device, 0); }
void ADX_ResetEntry(void) {}

void ADX_StartMem(void* buf, size_t size) {
    ADX_Stop(); ADXTrack* t = alloc_track(); track_init(t, -1, buf, size, true);
    is_paused=false; if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
}

int ADX_GetNumFiles(void) { return num_tracks; }
void ADX_EntryAfs(int id) { ADXTrack* t = alloc_track(); track_init(t, id, NULL, 0, false); }

void ADX_StartAfs(int id) {
    ADX_Stop(); ADXTrack* t = alloc_track(); track_init(t, id, NULL, 0, true);
    is_paused=false; if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
    OSReport("[3SX] ADX_StartAfs: id=%d sz=%d rate=%d ring=%d\n", id, t->size, t->decoder.sample_rate, ring_used());
}

void ADX_SetOutVol(int vol) { output_gain = 1.0f; /* Force max for testing */ }
void ADX_SetMono(bool m) { (void)m; }

ADXState ADX_GetState(void) {
    if (!has_tracks || is_paused) return ADX_STATE_STOP;
    for (int i=0; i<num_tracks; i++) { int j=(first_track_index+i)%TRACKS_MAX; if (!track_exhausted(&tracks[j])) return ADX_STATE_PLAYING; }
    return ADX_STATE_PLAYEND;
}

/* Called by SPU_Init to register the EML timer callback */
void ADX_RegisterSPUCallback(void (*cb)(void)) {
    spu_timer_cb = cb;
}
