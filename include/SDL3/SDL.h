/**
 * @file SDL.h (include/SDL3/SDL.h)
 * @brief SDL3 → SDL2 compatibility shim for Wii U
 *
 * Includes real SDL2 and provides thin wrappers to map SDL3 names
 * to SDL2 equivalents. Only defines things SDL2 doesn't provide.
 */
#ifndef SDL_COMPAT_WIIU_H
#define SDL_COMPAT_WIIU_H

/* ======================================
 * Real SDL2
 * ====================================== */
#include <SDL2/SDL.h>

#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>

/* ======================================
 * SDL3 event names → SDL2
 * ====================================== */

#define SDL_EVENT_QUIT              SDL_QUIT
#define SDL_EVENT_KEY_DOWN          SDL_KEYDOWN
#define SDL_EVENT_KEY_UP            SDL_KEYUP
#define SDL_EVENT_MOUSE_MOTION      SDL_MOUSEMOTION
#define SDL_EVENT_WINDOW_RESIZED    SDL_WINDOWEVENT_RESIZED
#define SDL_EVENT_GAMEPAD_ADDED     SDL_CONTROLLERDEVICEADDED
#define SDL_EVENT_GAMEPAD_REMOVED   SDL_CONTROLLERDEVICEREMOVED

/* ======================================
 * SDL3 Gamepad → SDL2 GameController
 * ====================================== */

#define SDL_GAMEPAD_AXIS_LEFTX          SDL_CONTROLLER_AXIS_LEFTX
#define SDL_GAMEPAD_AXIS_LEFTY          SDL_CONTROLLER_AXIS_LEFTY
#define SDL_GAMEPAD_AXIS_RIGHTX         SDL_CONTROLLER_AXIS_RIGHTX
#define SDL_GAMEPAD_AXIS_RIGHTY         SDL_CONTROLLER_AXIS_RIGHTY
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER   SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_RIGHT_TRIGGER  SDL_CONTROLLER_AXIS_TRIGGERRIGHT
#define SDL_GAMEPAD_BUTTON_DPAD_UP      SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN    SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT    SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT   SDL_CONTROLLER_BUTTON_DPAD_RIGHT
#define SDL_GAMEPAD_BUTTON_EAST         SDL_CONTROLLER_BUTTON_B
#define SDL_GAMEPAD_BUTTON_SOUTH        SDL_CONTROLLER_BUTTON_A
#define SDL_GAMEPAD_BUTTON_WEST         SDL_CONTROLLER_BUTTON_X
#define SDL_GAMEPAD_BUTTON_NORTH        SDL_CONTROLLER_BUTTON_Y
#define SDL_GAMEPAD_BUTTON_LEFT_SHOULDER  SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_GAMEPAD_BUTTON_LEFT_STICK   SDL_CONTROLLER_BUTTON_LEFTSTICK
#define SDL_GAMEPAD_BUTTON_RIGHT_STICK  SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define SDL_GAMEPAD_BUTTON_BACK         SDL_CONTROLLER_BUTTON_BACK
#define SDL_GAMEPAD_BUTTON_START        SDL_CONTROLLER_BUTTON_START
#define SDL_GAMEPAD_BUTTON_GUIDE        SDL_CONTROLLER_BUTTON_GUIDE

typedef SDL_GameController* SDL_Gamepad;
#define SDL_OpenGamepad(id)         SDL_GameControllerOpen(id)
#define SDL_CloseGamepad(g)         SDL_GameControllerClose(g)
#define SDL_GetGamepadAxis(g, a)    SDL_GameControllerGetAxis(g, a)
#define SDL_GetGamepadButton(g, b)  SDL_GameControllerGetButton(g, b)
#define SDL_IsGamepad(id)           SDL_IsGameController(id)

/* ======================================
 * SDL3 alpha constant (not in SDL2)
 * ====================================== */

#ifndef SDL_ALPHA_OPAQUE_FLOAT
#define SDL_ALPHA_OPAQUE_FLOAT  1.0f
#endif

/* ======================================
 * SDL3 SDL_FColor (not in SDL2)
 * ====================================== */

typedef struct SDL_FColor {
    float r, g, b, a;
} SDL_FColor;

/* ======================================
 * SDL3 Palette → SDL2
 * ====================================== */

#define SDL_CreatePalette(n)    SDL_AllocPalette(n)
#define SDL_DestroyPalette(p)   SDL_FreePalette(p)

/* ======================================
 * SDL3 Surface → SDL2
 * ====================================== */

static inline SDL_Surface* SDL_CreateSurfaceFrom(int w, int h,
                                                  Uint32 fmt,
                                                  void* pixels, int pitch) {
    int bpp;
    Uint32 Rm, Gm, Bm, Am;
    SDL_PixelFormatEnumToMasks(fmt, &bpp, &Rm, &Gm, &Bm, &Am);
    return SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, bpp, pitch, fmt);
}

#define SDL_DestroySurface(s)   SDL_FreeSurface(s)

/* SDL3: SDL_SetSurfacePalette(surface, palette) returns bool
 * SDL2: SDL_SetSurfacePalette(surface, palette) returns int (0=ok) */
/* SDL2 actually has SDL_SetSurfacePalette! Just wrap the return: */
static inline bool _wiiu_SetSurfacePalette(SDL_Surface* s, SDL_Palette* p) {
    return SDL_SetSurfacePalette(s, p) == 0;
}
/* But we can't redefine SDL_SetSurfacePalette since SDL2 already has it.
 * The game code calls SDL_SetSurfacePalette and expects bool on SDL3.
 * On SDL2 it returns int. The code checks: if (SDL_SetSurfacePalette(...))
 * which works the same way (non-zero = error on SDL2, true on SDL3).
 * Actually this is inverted... let's just leave it — SDL2's version works. */

/* ======================================
 * SDL3 Texture scale mode
 * ====================================== */

/* SDL3 uses an enum, SDL2 uses SDL_ScaleMode (same in 2.0.12+) */
#ifndef SDL_SCALEMODE_NEAREST
#define SDL_SCALEMODE_NEAREST SDL_ScaleModeNearest
#endif
#ifndef SDL_SCALEMODE_LINEAR
#define SDL_SCALEMODE_LINEAR SDL_ScaleModeLinear
#endif

/* ======================================
 * SDL3 render helpers
 * ====================================== */

static inline int SDL_SetRenderDrawColorFloat(SDL_Renderer* r,
                                               float red, float green,
                                               float blue, float alpha) {
    return SDL_SetRenderDrawColor(r,
                                  (Uint8)(red * 255.0f),
                                  (Uint8)(green * 255.0f),
                                  (Uint8)(blue * 255.0f),
                                  (Uint8)(alpha * 255.0f));
}

/* ======================================
 * SDL3 timing extras
 * ====================================== */

static inline Uint64 SDL_GetTicksNS(void) {
    return (Uint64)SDL_GetTicks() * 1000000ULL;
}

static inline void SDL_DelayNS(Uint64 ns) {
    SDL_Delay((Uint32)(ns / 1000000ULL));
}

/* ======================================
 * IOStream — SDL3 uses SDL_IOStream, SDL2 uses SDL_RWops
 * Game code uses SDL3 IOStream API, so we wrap FILE*
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

/* SDL3 IOStream read helpers — renamed to avoid conflicts with SDL2's SDL_ReadU8 etc. */
static inline bool SDL3_ReadU8(SDL_IOStream* io, Uint8* val) {
    return fread(val, 1, 1, io) == 1;
}

static inline bool SDL3_ReadU16BE(SDL_IOStream* io, Uint16* val) {
    return fread(val, 2, 1, io) == 1;
}

static inline bool SDL3_ReadS16BE(SDL_IOStream* io, Sint16* val) {
    Uint16 tmp;
    if (fread(&tmp, 2, 1, io) != 1) return false;
    *val = (Sint16)tmp;
    return true;
}

static inline bool SDL3_ReadU32BE(SDL_IOStream* io, Uint32* val) {
    return fread(val, 4, 1, io) == 1;
}

static inline bool SDL3_ReadU32LE(SDL_IOStream* io, Uint32* val) {
    Uint32 raw;
    if (fread(&raw, 4, 1, io) != 1) return false;
    *val = SDL_Swap32(raw);
    return true;
}

/* Override SDL2's SDL_ReadU8 etc. with our IOStream versions
 * since game code calls SDL_ReadU8(io, &val) with two args (SDL3 style)
 * while SDL2's SDL_ReadU8 takes one arg (SDL_RWops*) */
#undef SDL_ReadU8
#define SDL_ReadU8      SDL3_ReadU8
#undef SDL_ReadU16BE
#define SDL_ReadU16BE   SDL3_ReadU16BE
#undef SDL_ReadS16BE
#define SDL_ReadS16BE   SDL3_ReadS16BE
#undef SDL_ReadU32BE
#define SDL_ReadU32BE   SDL3_ReadU32BE
#undef SDL_ReadU32LE
#define SDL_ReadU32LE   SDL3_ReadU32LE

static inline SDL_IOStream* SDL_IOFromConstMem(const void* mem, size_t size) {
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
 * Path utilities
 * ====================================== */

static inline bool SDL_GetPathInfo(const char* path, void* info) {
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return true; }
    return false;
}

static inline bool SDL_CreateDirectory(const char* path) {
    extern int mkdir(const char*, int);
    return mkdir(path, 0755) == 0;
}

static inline bool SDL_RemovePath(const char* path) {
    return remove(path) == 0;
}

/* ======================================
 * Audio stub — SDL2 has its own SDL_AudioStream
 * We just need SDL_AUDIO_S16 and SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK
 * ====================================== */

#ifndef SDL_AUDIO_S16
#define SDL_AUDIO_S16 AUDIO_S16SYS
#endif
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0

/* ======================================
 * Misc SDL3 extras not in SDL2
 * ====================================== */

#ifndef SDL_copyp
#define SDL_copyp(dst, src) SDL_memcpy((dst), (src), sizeof(*(src)))
#endif

#ifndef SDL_copya
#define SDL_copya(dst, src) SDL_memcpy((dst), (src), sizeof(src))
#endif

#ifndef SDL_MAX_SINT16
#define SDL_MAX_SINT16 INT16_MAX
#endif

/* SDL2 may not have SDL_isdigit/SDL_isspace as macros */
#ifndef SDL_isdigit
#define SDL_isdigit(c) isdigit((unsigned char)(c))
#endif

#ifndef SDL_isspace
#define SDL_isspace(c) isspace((unsigned char)(c))
#endif

#ifndef SDL_fmodf
#define SDL_fmodf fmodf
#endif

/* Byte-swap helpers */
#ifndef SDL_Swap16BE
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define SDL_Swap16BE(x) (x)
#define SDL_Swap32BE(x) (x)
#else
#define SDL_Swap16BE(x) SDL_Swap16(x)
#define SDL_Swap32BE(x) SDL_Swap32(x)
#endif
#endif

/* SDL3 uses SDL_Mutex, SDL2 uses SDL_mutex */
#ifndef SDL_Mutex
typedef SDL_mutex SDL_Mutex;
#endif
#endif /* SDL_COMPAT_WIIU_H */
