/**
 * @file wiiu_app.c
 * @brief Wii U application lifecycle — OSScreen debug mode
 *
 * Uses OSScreen for text output instead of GX2/WHBGfx.
 * OSScreen and WHBGfx cannot coexist — they fight over the framebuffer.
 */
#include "port/wiiu/wiiu_app.h"
#include "port/wiiu/wiiu_pad.h"
#include "common.h"

#include <coreinit/debug.h>
#include <coreinit/cache.h>
#include <coreinit/screen.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/event.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

#include <stdbool.h>

/* State */
static bool app_running = true;

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
    SDLPad_Init();
    return 0;
}

void WiiUApp_Quit(void) {
    WHBUnmountSdCard();
    WHBProcShutdown();
}

/* ======================================
 * Event Polling
 * ====================================== */

bool WiiUApp_PollEvents(void) {
    if (!WHBProcIsRunning()) {
        app_running = false;
        return false;
    }
    return true;
}

/* ======================================
 * Frame Begin / End — no-op in OSScreen mode
 * ====================================== */

void WiiUApp_BeginFrame(void) {
    /* No GX2 rendering in debug mode */
}

void WiiUApp_EndFrame(void) {
    /* VSync for frame pacing + ProcUI processing */
    OSSleepTicks(OSMillisecondsToTicks(16));
}

void WiiUApp_Exit(void) {
    app_running = false;
}

void WiiUApp_SetGX2Initialized(void) {
    /* Not used in OSScreen mode */
}
