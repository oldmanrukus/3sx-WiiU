/**
 * @file wiiu_app.h
 * @brief Wii U application lifecycle — replaces sdl_app.h
 */
#ifndef WIIU_APP_H
#define WIIU_APP_H

#include <stdbool.h>

#define TARGET_FPS 59.59949

/* Wii U native functions */
int  WiiUApp_PreInit(void);
int  WiiUApp_FullInit(void);
void WiiUApp_Quit(void);
bool WiiUApp_PollEvents(void);
void WiiUApp_BeginFrame(void);
void WiiUApp_EndFrame(void);
void WiiUApp_Exit(void);

/* Tell wiiu_app that GX2 was already initialized (e.g. by debug_flash) */
void WiiUApp_SetGX2Initialized(void);

/* SDL-named functions (implemented in sdl_wrappers.c) */
int  SDLApp_PreInit(void);
int  SDLApp_FullInit(void);
void SDLApp_Quit(void);
bool SDLApp_PollEvents(void);
void SDLApp_BeginFrame(void);
void SDLApp_EndFrame(void);
void SDLApp_Exit(void);

#endif
