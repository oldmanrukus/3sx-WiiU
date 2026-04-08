/**
 * @brief Wii U game renderer — SAFE NO-OP version
 */
#include "common.h"
#include "sf33rd/AcrSDK/ps2/flps2etc.h"
#include "sf33rd/AcrSDK/ps2/flps2render.h"
#include "sf33rd/AcrSDK/ps2/foundaps2.h"

void WiiUGameRenderer_Init(void) {}
void WiiUGameRenderer_Shutdown(void) {}
void WiiUGameRenderer_BeginFrame(void) {}
void WiiUGameRenderer_RenderFrame(void) {}
void WiiUGameRenderer_EndFrame(void) {}

void WiiUGameRenderer_CreateTexture(unsigned int th) { (void)th; }
void WiiUGameRenderer_DestroyTexture(unsigned int texture_handle) { (void)texture_handle; }
void WiiUGameRenderer_UnlockTexture(unsigned int th) { (void)th; }
void WiiUGameRenderer_CreatePalette(unsigned int ph) { (void)ph; }
void WiiUGameRenderer_DestroyPalette(unsigned int palette_handle) { (void)palette_handle; }
void WiiUGameRenderer_UnlockPalette(unsigned int ph) { (void)ph; }
void WiiUGameRenderer_SetTexture(unsigned int th) { (void)th; }

void WiiUGameRenderer_DrawTexturedQuad(const Sprite* sprite, unsigned int color) {
    (void)sprite; (void)color;
}

void WiiUGameRenderer_DrawSolidQuad(const Quad* quad, unsigned int color) {
    (void)quad; (void)color;
}

void WiiUGameRenderer_DrawSprite(const Sprite* sprite, unsigned int color) {
    (void)sprite; (void)color;
}

void WiiUGameRenderer_DrawSprite2(const Sprite2* sprite2) {
    (void)sprite2;
}
