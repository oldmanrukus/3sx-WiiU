/**
 * @file wiiu_message_renderer.h
 * @brief Wii U message overlay renderer — replaces sdl_message_renderer.h
 */
#ifndef WIIU_MESSAGE_RENDERER_H
#define WIIU_MESSAGE_RENDERER_H

#include <gx2/surface.h>
#include <gx2/texture.h>

/* Aliases so existing code calling SDLMessageRenderer_* compiles */
#define SDLMessageRenderer_Initialize(r)  WiiUMessageRenderer_Initialize()
#define SDLMessageRenderer_BeginFrame()   WiiUMessageRenderer_BeginFrame()
#define SDLMessageRenderer_CreateTexture  WiiUMessageRenderer_CreateTexture
#define SDLMessageRenderer_DrawTexture    WiiUMessageRenderer_DrawTexture

void WiiUMessageRenderer_Initialize(void);
void WiiUMessageRenderer_BeginFrame(void);
void WiiUMessageRenderer_CreateTexture(int width, int height,
                                        void* pixels, int format);
void WiiUMessageRenderer_DrawTexture(int x0, int y0, int x1, int y1,
                                      int u0, int v0, int u1, int v1,
                                      unsigned int color);
void WiiUMessageRenderer_Shutdown(void);

/* For compositing the message canvas onto the final output */
GX2Texture* WiiUMessageRenderer_GetCanvasTexture(void);
GX2ColorBuffer* WiiUMessageRenderer_GetColorBuffer(void);

#endif
