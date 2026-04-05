/**
 * @file sdl_compat.h
 * @brief Minimal SDL3 compatibility shim for Wii U
 *
 * The 3SX game logic (src/sf33rd/) uses a handful of SDL utility macros:
 *   SDL_zero, SDL_zeroa, SDL_zerop, SDL_copyp, SDL_memmove,
 *   SDL_memset, SDL_memcpy, SDL_malloc, SDL_free, SDL_Log, etc.
 *
 * This header provides those as thin wrappers so the decompiled game
 * code compiles without modification on Wii U.
 *
 * Include path: This file should be found via -I when the build looks
 * for <SDL3/SDL.h>. Place it at: include/SDL3/SDL.h in the Wii U
 * overlay include directory.
 */
#ifndef SDL_COMPAT_WIIU_H
#define SDL_COMPAT_WIIU_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>

/* ======================================
 * Integer types that SDL3 normally provides
 * ====================================== */

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef int64_t  Sint64;

/* ======================================
 * Memory utility macros
 * ====================================== */

#define SDL_zero(x)      memset(&(x), 0, sizeof(x))
#define SDL_zeroa(x)     memset((x), 0, sizeof(x))
#define SDL_zerop(x)     memset((x), 0, sizeof(*(x)))
#define SDL_copyp(dst, src) memcpy((dst), (src), sizeof(*(src)))
#define SDL_copya(dst, src) memcpy((dst), (src), sizeof(src))

#define SDL_memset       memset
#define SDL_memcpy       memcpy
#define SDL_memmove      memmove
#define SDL_malloc       malloc
#define SDL_free         free
#define SDL_qsort        qsort

/* ======================================
 * String utilities
 * ====================================== */

#define SDL_strcmp        strcmp
#define SDL_strncmp      strncmp
#define SDL_strlen       strlen
/*
 * Provide a fallback implementation of SDL_strdup.  Some
 * environments (e.g. devkitPPC + MSYS2) don’t declare the POSIX
 * strdup prototype in <string.h>, leading to an implicit function
 * declaration warning.  Implement a simple version that allocates
 * memory with malloc and copies the string using memcpy.  Callers
 * must free the returned string with SDL_free.
 */
static inline char* SDL_strdup(const char* src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char* dst = (char*)malloc(len + 1);
    if (!dst) {
        return NULL;
    }
    memcpy(dst, src, len + 1);
    return dst;
}
/*
 * Use the system strlcpy if available; otherwise fall back to our
 * _wiiu_strlcpy defined below.  We redefine strlcpy to our
 * implementation only when not provided by libc.  This macro must
 * come after SDL_strdup so that the compiler sees the function
 * prototype before it encounters any calls.
 */
#define SDL_strlcpy      strlcpy
#define SDL_strtok_r     strtok_r
#define SDL_snprintf     snprintf
#define SDL_sscanf       sscanf
#define SDL_atoi         atoi
#define SDL_isdigit      isdigit
#define SDL_isspace      isspace
#define SDL_fmodf        fmodf

/*
 * On Wii U we don’t need any special calling conventions, so define
 * SDLCALL as empty. Some code (e.g. arcade_char_data.c) uses SDLCALL
 * before function names; without this definition the compiler will
 * treat SDLCALL as an unknown type or attribute and emit an error.
 */
#ifndef SDLCALL
#define SDLCALL
#endif

/*
 * Provide SDL_vasprintf and SDL_asprintf.  Some platforms (e.g. MSYS2)
 * don’t implement vasprintf, so fall back to vsnprintf-based allocation.
 */
static inline int SDL_vasprintf(char** strp, const char* fmt, va_list ap) {
#if defined(_GNU_SOURCE) || defined(__gnu_linux__) || defined(__linux__)
    return vasprintf(strp, fmt, ap);
#else
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, fmt, ap);
    if (len < 0) {
        *strp = NULL;
        return len;
    }
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) {
        *strp = NULL;
        return -1;
    }
    vsnprintf(buf, (size_t)len + 1, fmt, ap2);
    *strp = buf;
    return len;
#endif
}

static inline int SDL_asprintf(char** strp, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = SDL_vasprintf(strp, fmt, ap);
    va_end(ap);
    return ret;
}

/*
 * Read a signed 16‑bit big‑endian value from an IO stream. The
 * _wiiu_read_u16be helper reads a 16‑bit unsigned value; cast the
 * result to signed for SDL_ReadS16BE.
 */

/* ======================================
 * strlcpy fallback (not in all libcs)
 * ====================================== */

#ifndef strlcpy
static inline size_t _wiiu_strlcpy(char* dst, const char* src, size_t dstsize) {
    size_t srclen = strlen(src);
    if (dstsize > 0) {
        size_t cpylen = (srclen >= dstsize) ? dstsize - 1 : srclen;
        memcpy(dst, src, cpylen);
        dst[cpylen] = '\0';
    }
    return srclen;
}
#define strlcpy _wiiu_strlcpy
#endif

/* ======================================
 * Logging
 * ====================================== */

#include <coreinit/debug.h>

#define SDL_Log(fmt, ...)   OSReport("[3SX] " fmt "\n", ##__VA_ARGS__)

/* ======================================
 * Misc macros used in game code
 * ====================================== */

#define SDL_arraysize(a)    (sizeof(a) / sizeof((a)[0]))
#define SDL_assert(x)       do { if (!(x)) { OSReport("ASSERT FAIL: %s:%d\n", __FILE__, __LINE__); } } while(0)

#define SDL_min(a, b)       (((a) < (b)) ? (a) : (b))
#define SDL_max(a, b)       (((a) > (b)) ? (a) : (b))
#define SDL_clamp(x, lo, hi) (((x) < (lo)) ? (lo) : (((x) > (hi)) ? (hi) : (x)))

#define SDL_MAX_SINT16      INT16_MAX

/* ======================================
 * Byte-swap macros (Wii U is big-endian natively)
 * ====================================== */

/* On Wii U (big-endian PPC), big-endian values are native — no swap needed */
#define SDL_Swap16BE(x)     (x)
#define SDL_Swap32BE(x)     (x)
#define SDL_ReadS16BE(x)    (*(int16_t*)(x))
#define SDL_ReadU16BE(io, p) _wiiu_read_u16be(io, p)
#define SDL_ReadU32BE(io, p) _wiiu_read_u32be(io, p)
#define SDL_ReadU32LE(io, p) _wiiu_read_u32le(io, p)
#define SDL_ReadU8(io, p)    _wiiu_read_u8(io, p)

/* ======================================
 * Pixel formats (enum values matching SDL3)
 * ====================================== */

#define SDL_PIXELFORMAT_UNKNOWN     0
#define SDL_PIXELFORMAT_INDEX4LSB   0x12100004
#define SDL_PIXELFORMAT_INDEX8      0x13000801
#define SDL_PIXELFORMAT_ABGR1555   0x15331002
#define SDL_PIXELFORMAT_ARGB32     0x16362004
#define SDL_PIXELFORMAT_RGBA8888   0x16462004

/* ======================================
 * Alpha constants
 * ====================================== */

#define SDL_ALPHA_OPAQUE        255
#define SDL_ALPHA_TRANSPARENT   0
#define SDL_ALPHA_OPAQUE_FLOAT  1.0f

/* ======================================
 * Scancode stubs (only needed for keymap.c, which we bypass on Wii U)
 * ====================================== */

typedef int SDL_Scancode;
#define SDL_SCANCODE_UNKNOWN 0

/* ======================================
 * Mutex — use OSMutex from coreinit
 * ====================================== */

#include <coreinit/mutex.h>

typedef OSMutex SDL_Mutex;

static inline SDL_Mutex* SDL_CreateMutex(void) {
    SDL_Mutex* m = (SDL_Mutex*)malloc(sizeof(SDL_Mutex));
    if (m) OSInitMutex(m);
    return m;
}

static inline void SDL_LockMutex(SDL_Mutex* m)   { OSLockMutex(m); }
static inline void SDL_UnlockMutex(SDL_Mutex* m) { OSUnlockMutex(m); }

static inline void SDL_DestroyMutex(SDL_Mutex* m) {
    if (m) free(m);
}

/* ======================================
 * Timing
 * ====================================== */

#include <coreinit/time.h>
#include <coreinit/thread.h>

static inline Uint64 SDL_GetTicks(void) {
    return OSTicksToMilliseconds(OSGetSystemTime());
}

static inline Uint64 SDL_GetTicksNS(void) {
    /* OSGetSystemTime() returns ticks at the bus clock rate.
     * Convert: ns = ticks * 1000000000 / OSTimerClockSpeed */
    OSTime t = OSGetSystemTime();
    /* Wii U bus clock = 248.625 MHz → each tick ~= 4.022 ns */
    return (Uint64)((double)t * 1000000000.0 / (double)OSTimerClockSpeed);
}

static inline void SDL_Delay(Uint32 ms) {
    OSSleepTicks(OSMillisecondsToTicks(ms));
}

static inline void SDL_DelayNS(Uint64 ns) {
    OSSleepTicks(OSNanosecondsToTicks(ns));
}

/* ======================================
 * IOStream replacement — thin FILE* wrapper
 * ====================================== */

typedef FILE SDL_IOStream;

#define SDL_IO_SEEK_SET SEEK_SET
#define SDL_IO_SEEK_CUR SEEK_CUR

static inline SDL_IOStream* SDL_IOFromFile(const char* path, const char* mode) {
    return fopen(path, mode);
}

static inline void SDL_CloseIO(SDL_IOStream* io) {
    if (io) fclose(io);
}

static inline size_t SDL_ReadIO(SDL_IOStream* io, void* buf, size_t size) {
    return fread(buf, 1, size, io);
}

static inline size_t SDL_WriteIO(SDL_IOStream* io, const void* buf, size_t size) {
    return fwrite(buf, 1, size, io);
}

static inline Sint64 SDL_SeekIO(SDL_IOStream* io, Sint64 offset, int whence) {
    fseek(io, (long)offset, whence);
    return ftell(io);
}

static inline Sint64 SDL_GetIOSize(SDL_IOStream* io) {
    long cur = ftell(io);
    fseek(io, 0, SEEK_END);
    long size = ftell(io);
    fseek(io, cur, SEEK_SET);
    return size;
}

static inline bool _wiiu_read_u8(SDL_IOStream* io, Uint8* val) {
    return fread(val, 1, 1, io) == 1;
}

static inline bool _wiiu_read_u16be(SDL_IOStream* io, Uint16* val) {
    /* Wii U is big-endian, so native read is BE */
    return fread(val, 2, 1, io) == 1;
}

static inline bool _wiiu_read_u32be(SDL_IOStream* io, Uint32* val) {
    return fread(val, 4, 1, io) == 1;
}

static inline bool _wiiu_read_u32le(SDL_IOStream* io, Uint32* val) {
    Uint32 raw;
    if (fread(&raw, 4, 1, io) != 1) return false;
    /* Swap from LE to BE (native) */
    *val = ((raw & 0xFF) << 24) |
           ((raw & 0xFF00) << 8) |
           ((raw & 0xFF0000) >> 8) |
           ((raw & 0xFF000000) >> 24);
    return true;
}

/*
 * Helper to read a signed 16‑bit big‑endian value from an IO stream.
 * Uses the existing unsigned reader and casts to Sint16.
 */
static inline bool _wiiu_read_s16be(SDL_IOStream* io, Sint16* val) {
    Uint16 tmp;
    if (!_wiiu_read_u16be(io, &tmp)) {
        return false;
    }
    *val = (Sint16)tmp;
    return true;
}

/*
 * Override SDL_ReadS16BE to accept two parameters (IO stream and pointer).
 * Without this override, the default SDL_ReadS16BE macro takes a single
 * pointer argument and simply dereferences it, which is not suitable for
 * reading from an IO stream.
 */
#ifdef SDL_ReadS16BE
#undef SDL_ReadS16BE
#endif
#define SDL_ReadS16BE(io, p) _wiiu_read_s16be(io, p)

static inline SDL_IOStream* SDL_IOFromConstMem(const void* mem, size_t size) {
    /* Not used on Wii U — stub */
    (void)mem; (void)size;
    return NULL;
}

static inline SDL_IOStream* SDL_IOFromMem(void* mem, size_t size) {
    (void)mem; (void)size;
    return NULL;
}

static inline SDL_IOStream* SDL_IOFromDynamicMem(void) {
    return NULL;
}

/* ======================================
 * Error / message stubs
 * ====================================== */

static inline const char* SDL_GetError(void) {
    return "SDL_GetError not available on Wii U";
}

/* ======================================
 * Audio types (stubs — real audio is in wiiu_spu/wiiu_adx)
 * ====================================== */

typedef int SDL_AudioStream;
#define SDL_AUDIO_S16 0x8010
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0

typedef struct {
    int format;
    int channels;
    int freq;
} SDL_AudioSpec;

/* ======================================
 * Color / geometry types used by renderers
 * ====================================== */

typedef struct { Uint8 r, g, b, a; } SDL_Color;

typedef struct { float r, g, b, a; } SDL_FColor;

typedef struct { float x, y, w, h; } SDL_FRect;

typedef struct { int x, y; } SDL_Point;

/* ======================================
 * Blend mode stub
 * ====================================== */

#define SDL_BLENDMODE_BLEND 1

/* ======================================
 * Scale mode
 * ====================================== */

typedef int SDL_ScaleMode;
#define SDL_SCALEMODE_INVALID  0
#define SDL_SCALEMODE_NEAREST  1
#define SDL_SCALEMODE_LINEAR   2

/* ======================================
 * Path utilities — SD card based
 * ====================================== */

#include <coreinit/filesystem.h>

static inline bool SDL_GetPathInfo(const char* path, void* info) {
    /* Just check existence via stat-like approach */
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return true; }
    return false;
}

static inline bool SDL_CreateDirectory(const char* path) {
    /* WUT provides mkdir via POSIX compat */
    extern int mkdir(const char*, int);
    return mkdir(path, 0755) == 0;
}

static inline bool SDL_RemovePath(const char* path) {
    return remove(path) == 0;
}

/* ======================================
 * Palette / Surface types (used by renderer)
 * ====================================== */

typedef struct SDL_Palette {
    int ncolors;
    SDL_Color colors[256];
} SDL_Palette;

typedef int SDL_PixelFormat;

typedef struct SDL_Surface {
    int w, h;
    SDL_PixelFormat format;
    void* pixels;
    int pitch;
    SDL_Palette* palette;
} SDL_Surface;

static inline SDL_Palette* SDL_CreatePalette(int ncolors) {
    SDL_Palette* p = (SDL_Palette*)calloc(1, sizeof(SDL_Palette));
    if (p) p->ncolors = ncolors;
    return p;
}

static inline bool SDL_SetPaletteColors(SDL_Palette* p, const SDL_Color* colors, int first, int ncolors) {
    if (!p) return false;
    memcpy(&p->colors[first], colors, ncolors * sizeof(SDL_Color));
    return true;
}

static inline void SDL_DestroyPalette(SDL_Palette* p) {
    free(p);
}

static inline SDL_Surface* SDL_CreateSurfaceFrom(int w, int h, SDL_PixelFormat fmt, void* pixels, int pitch) {
    SDL_Surface* s = (SDL_Surface*)calloc(1, sizeof(SDL_Surface));
    if (s) { s->w = w; s->h = h; s->format = fmt; s->pixels = pixels; s->pitch = pitch; }
    return s;
}

static inline bool SDL_SetSurfacePalette(SDL_Surface* s, SDL_Palette* p) {
    if (s) { s->palette = p; return true; }
    return false;
}

static inline void SDL_DestroySurface(SDL_Surface* s) {
    free(s);
}

#endif /* SDL_COMPAT_WIIU_H */
