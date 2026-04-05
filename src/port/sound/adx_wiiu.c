/**
 * @file adx_wiiu.c
 * @brief Standalone ADX audio decoder for Wii U — replaces FFmpeg-based adx.c
 *
 * CRI ADX is a relatively simple ADPCM codec. This implements a minimal
 * decoder that handles:
 *   - ADX version 3 and 4 headers
 *   - Stereo and mono streams
 *   - Loop points (start/end sample with infinite looping)
 *   - Seamless multi-track playback
 *
 * Output feeds into AX audio via a ring buffer.
 *
 * Reference: https://en.wikipedia.org/wiki/ADX_(file_format)
 */
#include "port/sound/adx.h"
#include "port/io/afs.h"
#include "port/utils.h"
#include "sf33rd/Source/Game/io/gd3rd.h"

#include <coreinit/debug.h>
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>

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

/* ======================================
 * Constants
 * ====================================== */

#define ADX_SAMPLE_RATE    48000
#define ADX_CHANNELS       2
#define ADX_BYTES_PER_SAMPLE 2
#define ADX_RING_SIZE      (48000 * 2)  /* 1 second of stereo samples */
#define ADX_RING_MASK      (ADX_RING_SIZE - 1)
#define TRACKS_MAX         10

/* ======================================
 * ADX decoder state
 * ====================================== */

/* ADX ADPCM coefficients (fixed, derived from the cutoff frequency) */
typedef struct ADXCoeffs {
    int coeff1;
    int coeff2;
} ADXCoeffs;

typedef struct ADXDecoder {
    int16_t prev1[2];  /* per-channel history */
    int16_t prev2[2];
    int channels;
    int sample_rate;
    int block_size;    /* typically 18 bytes */
    int samples_per_block; /* (block_size - 2) * 2 = 32 for 18-byte blocks */
    int header_size;   /* offset to first audio frame */
    ADXCoeffs coeffs;
} ADXDecoder;

typedef struct ADXLoopInfo {
    bool enabled;
    int start_sample;
    int end_sample;
    int16_t* loop_buffer;  /* pre-decoded loop region */
    int loop_buffer_size;  /* in samples (per channel) */
    int loop_position;     /* current read position in loop buffer */
} ADXLoopInfo;

typedef struct ADXTrack {
    uint8_t* data;
    int size;
    bool should_free;
    int read_offset;
    int decoded_samples;
    ADXDecoder decoder;
    ADXLoopInfo loop;
} ADXTrack;

/* ======================================
 * AX output
 * ====================================== */

static AXVoice* adx_voice_l = NULL;
static AXVoice* adx_voice_r = NULL;

/* Ring buffer for decoded PCM (separate L/R for AX) */
static int16_t* adx_pcm_l = NULL;
static int16_t* adx_pcm_r = NULL;
static volatile uint32_t adx_write_pos = 0;

/* ======================================
 * Track management
 * ====================================== */

static ADXTrack tracks[TRACKS_MAX] = { 0 };
static int num_tracks = 0;
static int first_track_index = 0;
static bool has_tracks = false;

static bool is_paused = true;
static float output_gain = 1.0f;

/* ======================================
 * ADX header parsing
 * ====================================== */

static uint16_t read_be16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | p[3];
}

static void compute_adx_coefficients(int cutoff_freq, int sample_rate,
                                      ADXCoeffs* out) {
    /* Standard CRI coefficient calculation */
    double a = sqrt(2.0) - cos(2.0 * M_PI * cutoff_freq / sample_rate);
    double b = sqrt(2.0) - 1.0;
    double c = (a - sqrt((a + b) * (a - b))) / b;

    out->coeff1 = (int)(c * 2.0 * 4096.0);   /* Fixed-point 12-bit */
    out->coeff2 = (int)(-(c * c) * 4096.0);
}

static bool parse_adx_header(const uint8_t* data, int size,
                              ADXDecoder* dec, ADXLoopInfo* loop) {
    if (size < 0x20) return false;

    /* Check magic: 0x80 0x00 */
    if (data[0] != 0x80 || data[1] != 0x00) return false;

    /* Header offset = big-endian u16 at offset 2, then +4 for "(c)CRI" marker */
    uint16_t copyright_offset = read_be16(data + 2);
    dec->header_size = copyright_offset + 4; /* Skip past "(c)CRI" */

    /* Encoding type at offset 4 (should be 3 = ADX standard) */
    uint8_t encoding = data[4];
    if (encoding != 3) {
        OSReport("[3SX] ADX: Unsupported encoding type %d\n", encoding);
        return false;
    }

    dec->block_size = data[5];
    /* bits_per_sample at offset 6 (should be 4) */
    dec->channels = data[7];
    dec->sample_rate = read_be32(data + 8);
    /* total_samples at offset 12 */
    dec->samples_per_block = (dec->block_size - 2) * 2;

    /* Highpass cutoff frequency at offset 16 */
    uint16_t cutoff = read_be16(data + 16);
    if (cutoff == 0) cutoff = 500; /* Default */
    compute_adx_coefficients(cutoff, dec->sample_rate, &dec->coeffs);

    /* Reset history */
    memset(dec->prev1, 0, sizeof(dec->prev1));
    memset(dec->prev2, 0, sizeof(dec->prev2));

    /* Parse loop info */
    memset(loop, 0, sizeof(*loop));
    uint8_t version = data[0x12];

    switch (version) {
    case 3:
        if (read_be16(data + 0x16) == 1) {
            loop->enabled = true;
            loop->start_sample = read_be32(data + 0x1C);
            loop->end_sample   = read_be32(data + 0x24);
        }
        break;

    case 4:
        if (read_be32(data + 0x24) == 1) {
            loop->enabled = true;
            loop->start_sample = read_be32(data + 0x28);
            loop->end_sample   = read_be32(data + 0x30);
        }
        break;

    default:
        OSReport("[3SX] ADX: Unknown version %d, no loop info\n", version);
        break;
    }

    if (loop->enabled) {
        int loop_samples = loop->end_sample - loop->start_sample;
        loop->loop_buffer_size = loop_samples * dec->channels;
        loop->loop_buffer = (int16_t*)malloc(
            loop->loop_buffer_size * sizeof(int16_t));
        loop->loop_position = 0;
    }

    return true;
}

/* ======================================
 * ADX block decoding
 * ====================================== */

static int decode_adx_block(ADXDecoder* dec, const uint8_t* block,
                             int channel, int16_t* output, int max_samples) {
    int scale = read_be16(block);
    const uint8_t* nibbles = block + 2;
    int samples_decoded = 0;
    int c1 = dec->coeffs.coeff1;
    int c2 = dec->coeffs.coeff2;
    int32_t prev1 = dec->prev1[channel];
    int32_t prev2 = dec->prev2[channel];

    for (int i = 0; i < dec->block_size - 2 && samples_decoded < max_samples; i++) {
        /* Each byte has two 4-bit nibbles (high first) */
        for (int nib = 0; nib < 2 && samples_decoded < max_samples; nib++) {
            int nibble;
            if (nib == 0) {
                nibble = (nibbles[i] >> 4) & 0x0F;
            } else {
                nibble = nibbles[i] & 0x0F;
            }

            /* Sign-extend 4-bit to 32-bit */
            if (nibble & 0x08) nibble -= 16;

            /* Reconstruct sample */
            int32_t sample = (nibble * scale +
                              (c1 * prev1 + c2 * prev2)) >> 12;

            /* Clamp to 16-bit */
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;

            output[samples_decoded] = (int16_t)sample;
            samples_decoded++;

            prev2 = prev1;
            prev1 = sample;
        }
    }

    dec->prev1[channel] = prev1;
    dec->prev2[channel] = prev2;

    return samples_decoded;
}

/* ======================================
 * Track decoding
 * ====================================== */

/* Decode samples from a track into the AX ring buffers */
static int decode_track_samples(ADXTrack* track, int max_samples) {
    ADXDecoder* dec = &track->decoder;
    int samples_output = 0;
    int16_t block_samples[64]; /* Enough for one block */

    while (samples_output < max_samples) {
        /* Check if we've reached the end of the data */
        if (track->read_offset + (dec->block_size * dec->channels) > track->size) {
            break;
        }

        /* Check loop end */
        if (track->loop.enabled &&
            track->decoded_samples >= track->loop.end_sample) {
            break;
        }

        /* Decode one frame (one block per channel) */
        int samples_this_frame = 0;

        if (dec->channels == 2) {
            /* Stereo: L block, then R block */
            int n = decode_adx_block(dec,
                                      track->data + track->read_offset,
                                      0, block_samples, dec->samples_per_block);
            track->read_offset += dec->block_size;

            int16_t block_samples_r[64];
            decode_adx_block(dec,
                             track->data + track->read_offset,
                             1, block_samples_r, n);
            track->read_offset += dec->block_size;

            /* Write interleaved to ring buffers */
            for (int i = 0; i < n && samples_output < max_samples; i++) {
                uint32_t wp = adx_write_pos & (ADX_RING_SIZE - 1);
                int16_t l = (int16_t)(block_samples[i] * output_gain);
                int16_t r = (int16_t)(block_samples_r[i] * output_gain);

                adx_pcm_l[wp] = l;
                adx_pcm_r[wp] = r;
                adx_write_pos++;
                samples_output++;

                /* Store in loop buffer if within loop region */
                if (track->loop.enabled &&
                    track->decoded_samples + i >= track->loop.start_sample &&
                    track->decoded_samples + i < track->loop.end_sample) {
                    int li = (track->decoded_samples + i - track->loop.start_sample) * 2;
                    if (li + 1 < track->loop.loop_buffer_size) {
                        track->loop.loop_buffer[li] = l;
                        track->loop.loop_buffer[li + 1] = r;
                    }
                }
            }

            samples_this_frame = n;

        } else {
            /* Mono */
            int n = decode_adx_block(dec,
                                      track->data + track->read_offset,
                                      0, block_samples, dec->samples_per_block);
            track->read_offset += dec->block_size;

            for (int i = 0; i < n && samples_output < max_samples; i++) {
                uint32_t wp = adx_write_pos & (ADX_RING_SIZE - 1);
                int16_t s = (int16_t)(block_samples[i] * output_gain);
                adx_pcm_l[wp] = s;
                adx_pcm_r[wp] = s;
                adx_write_pos++;
                samples_output++;
            }

            samples_this_frame = n;
        }

        track->decoded_samples += samples_this_frame;
    }

    return samples_output;
}

static bool track_exhausted(ADXTrack* track) {
    if (track->loop.enabled) return false;
    return track->read_offset >= track->size;
}

static bool track_loop_filled(ADXTrack* track) {
    return track->loop.enabled &&
           track->decoded_samples >= track->loop.end_sample;
}

/* Play loop buffer samples into the ring */
static int play_loop_samples(ADXTrack* track, int max_samples) {
    if (!track->loop.enabled || !track->loop.loop_buffer) return 0;

    int samples = 0;
    int loop_samples = track->loop.end_sample - track->loop.start_sample;

    while (samples < max_samples) {
        uint32_t wp = adx_write_pos & (ADX_RING_SIZE - 1);
        int li = track->loop.loop_position * 2;

        adx_pcm_l[wp] = track->loop.loop_buffer[li];
        adx_pcm_r[wp] = track->loop.loop_buffer[li + 1];

        adx_write_pos++;
        samples++;
        track->loop.loop_position++;

        if (track->loop.loop_position >= loop_samples) {
            track->loop.loop_position = 0;
        }
    }

    return samples;
}

/* ======================================
 * File loading
 * ====================================== */

static void* load_afs_file(int file_id, int* out_size) {
    unsigned int file_size = fsGetFileSize(file_id);
    *out_size = file_size;
    size_t buff_size = (file_size + 2048 - 1) & ~(2048 - 1);
    void* buff = malloc(buff_size);

    AFSHandle handle = AFS_Open(file_id);
    AFS_ReadSync(handle, fsCalSectorSize(file_size), buff);
    AFS_Close(handle);

    return buff;
}

/* ======================================
 * Track management
 * ====================================== */

static void track_init(ADXTrack* track, int file_id,
                        void* buf, size_t buf_size, bool allow_loop) {
    memset(track, 0, sizeof(*track));

    if (file_id != -1) {
        track->data = load_afs_file(file_id, &track->size);
        track->should_free = true;
    } else {
        track->data = buf;
        track->size = buf_size;
        track->should_free = false;
    }

    if (!parse_adx_header(track->data, track->size,
                           &track->decoder, &track->loop)) {
        OSReport("[3SX] ADX: Failed to parse header\n");
        return;
    }

    if (!allow_loop) {
        track->loop.enabled = false;
    }

    track->read_offset = track->decoder.header_size;
    track->decoded_samples = 0;

    /* Decode initial batch */
    decode_track_samples(track, ADX_SAMPLE_RATE / 4);
}

static void track_destroy(ADXTrack* track) {
    if (track->loop.loop_buffer) {
        free(track->loop.loop_buffer);
    }
    if (track->should_free && track->data) {
        free(track->data);
    }
    memset(track, 0, sizeof(*track));
}

static ADXTrack* alloc_track(void) {
    int index = (first_track_index + num_tracks) % TRACKS_MAX;
    num_tracks++;
    has_tracks = true;
    return &tracks[index];
}

/* ======================================
 * Public API
 * ====================================== */

void ADX_ProcessTracks(void) {
    if (is_paused || !has_tracks) return;

    int first = first_track_index;
    int count = num_tracks;

    for (int i = 0; i < count; i++) {
        int j = (first + i) % TRACKS_MAX;
        ADXTrack* track = &tracks[j];

        /* Decode more samples if needed */
        if (!track_loop_filled(track) && !track_exhausted(track)) {
            decode_track_samples(track, ADX_SAMPLE_RATE / 60);
        }

        /* Play loop buffer if loop region fully decoded */
        if (track_loop_filled(track)) {
            play_loop_samples(track, ADX_SAMPLE_RATE / 60);
            break; /* Looping tracks play indefinitely */
        }

        if (!track_exhausted(track)) break;

        /* Track is done, move to next */
        track_destroy(track);
        num_tracks--;
        if (num_tracks > 0) {
            first_track_index++;
        } else {
            first_track_index = 0;
        }
    }

    /* Flush cache for AX DMA */
    if (adx_pcm_l && adx_pcm_r) {
        DCFlushRange(adx_pcm_l, ADX_RING_SIZE * sizeof(int16_t));
        DCFlushRange(adx_pcm_r, ADX_RING_SIZE * sizeof(int16_t));
    }
}

void ADX_Init(void) {
    /* Allocate ring buffers in MEM2 for DMA access */
    adx_pcm_l = (int16_t*)MEMAllocFromDefaultHeapEx(
        ADX_RING_SIZE * sizeof(int16_t), 64);
    adx_pcm_r = (int16_t*)MEMAllocFromDefaultHeapEx(
        ADX_RING_SIZE * sizeof(int16_t), 64);
    memset(adx_pcm_l, 0, ADX_RING_SIZE * sizeof(int16_t));
    memset(adx_pcm_r, 0, ADX_RING_SIZE * sizeof(int16_t));
    adx_write_pos = 0;

    /* Acquire AX voices for ADX playback */
    adx_voice_l = AXAcquireVoice(30, NULL, NULL);
    adx_voice_r = AXAcquireVoice(30, NULL, NULL);

    if (adx_voice_l) {
        AXVoiceBegin(adx_voice_l);
        AXVoiceOffsets offsets = {
            .dataType = AX_VOICE_FORMAT_LPCM16,
            .loopingEnabled = AX_VOICE_LOOP_ENABLED,
            .loopOffset = 0,
            .endOffset = ADX_RING_SIZE - 1,
            .currentOffset = 0,
            .data = adx_pcm_l,
        };
        AXSetVoiceOffsets(adx_voice_l, &offsets);
        AXVoiceVeData ve = { .volume = 0x8000 };
        AXSetVoiceVe(adx_voice_l, &ve);

        AXVoiceDeviceMixData mix[6] = { 0 };
        mix[0].bus[0].volume = 0x8000;
        AXSetVoiceDeviceMix(adx_voice_l, AX_DEVICE_TYPE_TV, 0, mix);
        AXSetVoiceDeviceMix(adx_voice_l, AX_DEVICE_TYPE_DRC, 0, mix);

        AXSetVoiceSrcType(adx_voice_l, AX_VOICE_SRC_TYPE_NONE);
        AXSetVoiceState(adx_voice_l, AX_VOICE_STATE_PLAYING);
        AXVoiceEnd(adx_voice_l);
    }

    if (adx_voice_r) {
        AXVoiceBegin(adx_voice_r);
        AXVoiceOffsets offsets = {
            .dataType = AX_VOICE_FORMAT_LPCM16,
            .loopingEnabled = AX_VOICE_LOOP_ENABLED,
            .loopOffset = 0,
            .endOffset = ADX_RING_SIZE - 1,
            .currentOffset = 0,
            .data = adx_pcm_r,
        };
        AXSetVoiceOffsets(adx_voice_r, &offsets);
        AXVoiceVeData ve = { .volume = 0x8000 };
        AXSetVoiceVe(adx_voice_r, &ve);

        AXVoiceDeviceMixData mix[6] = { 0 };
        mix[1].bus[0].volume = 0x8000;
        AXSetVoiceDeviceMix(adx_voice_r, AX_DEVICE_TYPE_TV, 0, mix);
        AXSetVoiceDeviceMix(adx_voice_r, AX_DEVICE_TYPE_DRC, 0, mix);

        AXSetVoiceSrcType(adx_voice_r, AX_VOICE_SRC_TYPE_NONE);
        AXSetVoiceState(adx_voice_r, AX_VOICE_STATE_PLAYING);
        AXVoiceEnd(adx_voice_r);
    }

    OSReport("[3SX] ADX audio initialized (standalone decoder)\n");
}

void ADX_Exit(void) {
    ADX_Stop();
    if (adx_voice_l) { AXFreeVoice(adx_voice_l); adx_voice_l = NULL; }
    if (adx_voice_r) { AXFreeVoice(adx_voice_r); adx_voice_r = NULL; }
    if (adx_pcm_l) { MEMFreeToDefaultHeap(adx_pcm_l); adx_pcm_l = NULL; }
    if (adx_pcm_r) { MEMFreeToDefaultHeap(adx_pcm_r); adx_pcm_r = NULL; }
}

void ADX_Stop(void) {
    is_paused = true;

    /* Clear ring buffers */
    if (adx_pcm_l) memset(adx_pcm_l, 0, ADX_RING_SIZE * sizeof(int16_t));
    if (adx_pcm_r) memset(adx_pcm_r, 0, ADX_RING_SIZE * sizeof(int16_t));
    adx_write_pos = 0;

    for (int i = 0; i < num_tracks; i++) {
        int j = (first_track_index + i) % TRACKS_MAX;
        track_destroy(&tracks[j]);
    }
    num_tracks = 0;
    first_track_index = 0;
    has_tracks = false;
}

int ADX_IsPaused(void) { return is_paused; }

void ADX_Pause(int pause) { is_paused = pause; }

void ADX_StartMem(void* buf, size_t size) {
    ADX_Stop();
    ADXTrack* track = alloc_track();
    track_init(track, -1, buf, size, true);
    is_paused = false;
}

int ADX_GetNumFiles(void) { return num_tracks; }

void ADX_EntryAfs(int file_id) {
    ADXTrack* track = alloc_track();
    track_init(track, file_id, NULL, 0, false);
}

void ADX_StartSeamless(void) { is_paused = false; }
void ADX_ResetEntry(void) { /* Called after Stop, nothing needed */ }

void ADX_StartAfs(int file_id) {
    ADX_Stop();
    ADXTrack* track = alloc_track();
    track_init(track, file_id, NULL, 0, true);
    is_paused = false;
}

void ADX_SetOutVol(int volume) {
    output_gain = powf(10.0f, volume / 200.0f);
}

void ADX_SetMono(bool mono) {
    /* Not implemented */
    (void)mono;
}

ADXState ADX_GetState(void) {
    if (!has_tracks) return ADX_STATE_STOP;
    if (is_paused) return ADX_STATE_STOP;
    /* Check if all non-looping tracks are exhausted */
    for (int i = 0; i < num_tracks; i++) {
        int j = (first_track_index + i) % TRACKS_MAX;
        if (!track_exhausted(&tracks[j])) return ADX_STATE_PLAYING;
    }
    return ADX_STATE_PLAYEND;
}
