/**
 * @file wiiu_game_renderer.h
 * @brief Wii U GX2 game renderer — replaces sdl_game_renderer.h
 *
 * Implements the Renderer_* dispatch interface from rendering/game_renderer.h
 * using GX2 hardware rendering on the Wii U.
 */
#ifndef WIIU_GAME_RENDERER_H
#define WIIU_GAME_RENDERER_H

#include "rendering/game_renderer.h"

/* Lifecycle */
void WiiUGameRenderer_Init(void);
void WiiUGameRenderer_BeginFrame(void);
void WiiUGameRenderer_RenderFrame(void);
void WiiUGameRenderer_EndFrame(void);
void WiiUGameRenderer_Shutdown(void);

/* Renderer_ backend implementations */
void WiiUGameRenderer_CreateTexture(unsigned int th);
void WiiUGameRenderer_DestroyTexture(unsigned int texture_handle);
void WiiUGameRenderer_UnlockTexture(unsigned int th);
void WiiUGameRenderer_CreatePalette(unsigned int ph);
void WiiUGameRenderer_DestroyPalette(unsigned int palette_handle);
void WiiUGameRenderer_UnlockPalette(unsigned int ph);
void WiiUGameRenderer_SetTexture(unsigned int th);
void WiiUGameRenderer_DrawTexturedQuad(const Sprite* sprite, unsigned int color);
void WiiUGameRenderer_DrawSprite(const Sprite* sprite, unsigned int color);
void WiiUGameRenderer_DrawSprite2(const Sprite2* sprite2);
void WiiUGameRenderer_DrawSolidQuad(const Quad* quad, unsigned int color);

#endif
