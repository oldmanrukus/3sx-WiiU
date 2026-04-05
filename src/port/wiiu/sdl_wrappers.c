/**
 * @file sdl_wrappers.c
 * @brief Provides SDLApp_* and SDLMessageRenderer_* symbols for Wii U
 */
#ifdef TARGET_WIIU

#include <stdbool.h>

/* Declare WiiU functions directly — no headers, no macros */
extern int  WiiUApp_PreInit(void);
extern int  WiiUApp_FullInit(void);
extern void WiiUApp_Quit(void);
extern bool WiiUApp_PollEvents(void);
extern void WiiUApp_BeginFrame(void);
extern void WiiUApp_EndFrame(void);
extern void WiiUApp_Exit(void);
extern void WiiUApp_SetGX2Initialized(void);

extern void WiiUMessageRenderer_CreateTexture(int width, int height,
                                               void* pixels, int format);
extern void WiiUMessageRenderer_DrawTexture(int x0, int y0, int x1, int y1,
                                             int u0, int v0, int u1, int v1,
                                             unsigned int color);

/* Dummy texture pointer that knjsub.c references via extern */
typedef struct { int _unused; } SDL_Texture;
SDL_Texture* message_canvas = 0;

/* SDLApp wrappers */
int  SDLApp_PreInit(void)     { return WiiUApp_PreInit(); }
int  SDLApp_FullInit(void)    { return WiiUApp_FullInit(); }
void SDLApp_Quit(void)        { WiiUApp_Quit(); }
bool SDLApp_PollEvents(void)  { return WiiUApp_PollEvents(); }
void SDLApp_BeginFrame(void)  { WiiUApp_BeginFrame(); }
void SDLApp_EndFrame(void)    { WiiUApp_EndFrame(); }
void SDLApp_Exit(void)        { WiiUApp_Exit(); }

/* SDLMessageRenderer wrappers */
void SDLMessageRenderer_CreateTexture(int width, int height,
                                       void* pixels, int format) {
    WiiUMessageRenderer_CreateTexture(width, height, pixels, format);
}

void SDLMessageRenderer_DrawTexture(int x0, int y0, int x1, int y1,
                                     int u0, int v0, int u1, int v1,
                                     unsigned int color) {
    WiiUMessageRenderer_DrawTexture(x0, y0, x1, y1, u0, v0, u1, v1, color);
}

#endif
