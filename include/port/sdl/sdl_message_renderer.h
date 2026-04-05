#ifndef SDL_MESSAGE_RENDERER_H
#define SDL_MESSAGE_RENDERER_H

#if defined(TARGET_WIIU)

/* Dummy SDL types so Wii U build can compile */
typedef struct SDL_Texture { int _unused; } SDL_Texture;
typedef struct SDL_Renderer { int _unused; } SDL_Renderer;

extern SDL_Texture* message_canvas;

void SDLMessageRenderer_Initialize(SDL_Renderer* renderer);
void SDLMessageRenderer_CreateTexture(int width, int height, void* pixels, int format);
void SDLMessageRenderer_DrawTexture(
    int src_x, int src_y, int src_w, int src_h,
    int dst_x, int dst_y, int dst_w, int dst_h,
    unsigned int color
);
void SDLMessageRenderer_Shutdown(void);

#else

#include <SDL3/SDL.h>

extern SDL_Texture* message_canvas;

void SDLMessageRenderer_Initialize(SDL_Renderer* renderer);
void SDLMessageRenderer_CreateTexture(int width, int height, void* pixels, int format);
void SDLMessageRenderer_DrawTexture(
    int src_x, int src_y, int src_w, int src_h,
    int dst_x, int dst_y, int dst_w, int dst_h,
    unsigned int color
);
void SDLMessageRenderer_Shutdown(void);

#endif

#endif