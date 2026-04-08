/**
 * @file wiiu_app.c
 * @brief Wii U application lifecycle — SDL2 mode
 *
 * Uses real SDL2-wiiu for window creation and GX2 rendering.
 * Replaces the OSScreen debug mode.
 */
#include "port/wiiu/wiiu_app.h"
#include "port/wiiu/wiiu_pad.h"
#include "common.h"

#include <coreinit/debug.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

#include <SDL2/SDL.h>
#include <stdbool.h>

/* State */
static bool app_running = true;
static SDL_Window* sdl_window = NULL;
static SDL_Renderer* sdl_renderer = NULL;

/* CPS3 native resolution */
#define CPS3_WIDTH  384
#define CPS3_HEIGHT 224

/* ======================================
 * Initialization
 * ====================================== */

int WiiUApp_PreInit(void) {
    WHBProcInit();

    if (!WHBMountSdCard()) {
        OSReport("[3SX] FATAL: Failed to mount SD card!\n");
    }

    return 0;
}

int WiiUApp_FullInit(void) {
    OSReport("[3SX] SDL_Init...\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        OSReport("[3SX] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    OSReport("[3SX] Creating window + renderer...\n");

    sdl_window = SDL_CreateWindow("3SX",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  CPS3_WIDTH, CPS3_HEIGHT,
                                  0);
    if (!sdl_window) {
        OSReport("[3SX] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        OSReport("[3SX] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Set logical size to CPS3 resolution — SDL will handle scaling */
    SDL_RenderSetLogicalSize(sdl_renderer, CPS3_WIDTH, CPS3_HEIGHT);

    OSReport("[3SX] SDL2 initialized: %dx%d\n", CPS3_WIDTH, CPS3_HEIGHT);

    SDLPad_Init();
    return 0;
}

void WiiUApp_Quit(void) {
    if (sdl_renderer) {
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = NULL;
    }
    if (sdl_window) {
        SDL_DestroyWindow(sdl_window);
        sdl_window = NULL;
    }
    SDL_Quit();
    WHBUnmountSdCard();
    WHBProcShutdown();
}

/* ======================================
 * Accessors for the renderer
 * ====================================== */

SDL_Window* WiiUApp_GetWindow(void) {
    return sdl_window;
}

SDL_Renderer* WiiUApp_GetRenderer(void) {
    return sdl_renderer;
}

/* ======================================
 * Event Polling
 * ====================================== */

bool WiiUApp_PollEvents(void) {
    if (!WHBProcIsRunning()) {
        app_running = false;
        return false;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            app_running = false;
            return false;
        }
    }

    return true;
}

/* ======================================
 * Frame Begin / End
 * ====================================== */

void WiiUApp_BeginFrame(void) {
    if (sdl_renderer) {
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
    }
}

void WiiUApp_EndFrame(void) {
    if (sdl_renderer) {
        SDL_RenderPresent(sdl_renderer);
    }
}

void WiiUApp_Exit(void) {
    app_running = false;
}

void WiiUApp_SetGX2Initialized(void) {
    /* SDL2 handles GX2 internally */
}
