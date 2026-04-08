/**
 * @file wiiu_app.h
 * @brief Wii U application lifecycle — SDL2 mode
 */
#ifndef WIIU_APP_H
#define WIIU_APP_H

#include <stdbool.h>

#define TARGET_FPS 59.59949

int WiiUApp_PreInit(void);
int WiiUApp_FullInit(void);
void WiiUApp_Quit(void);

/// @brief Process Wii U ProcUI events + HOME menu.
/// @return true if the main loop should continue, false to exit.
bool WiiUApp_PollEvents(void);

void WiiUApp_BeginFrame(void);
void WiiUApp_EndFrame(void);
void WiiUApp_Exit(void);
void WiiUApp_SetGX2Initialized(void);

/* SDL2 accessors for renderer backend */
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Window* WiiUApp_GetWindow(void);
struct SDL_Renderer* WiiUApp_GetRenderer(void);

/* Aliases so main.c can call SDLApp_* unchanged */
#define SDLApp_PreInit    WiiUApp_PreInit
#define SDLApp_FullInit   WiiUApp_FullInit
#define SDLApp_Quit       WiiUApp_Quit
#define SDLApp_PollEvents WiiUApp_PollEvents
#define SDLApp_BeginFrame WiiUApp_BeginFrame
#define SDLApp_EndFrame   WiiUApp_EndFrame
#define SDLApp_Exit       WiiUApp_Exit

#endif
