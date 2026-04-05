/**
 * @file wiiu_message_renderer.c
 * @brief Wii U message renderer — SAFE NO-OP version
 */

void WiiUMessageRenderer_Initialize(void) {}
void WiiUMessageRenderer_BeginFrame(void) {}

void WiiUMessageRenderer_CreateTexture(int width, int height,
                                        void* pixels, int format) {
    (void)width; (void)height; (void)pixels; (void)format;
}

void WiiUMessageRenderer_DrawTexture(int x0, int y0, int x1, int y1,
                                      int u0, int v0, int u1, int v1,
                                      unsigned int color) {
    (void)x0; (void)y0; (void)x1; (void)y1;
    (void)u0; (void)v0; (void)u1; (void)v1;
    (void)color;
}

void WiiUMessageRenderer_Shutdown(void) {}
