/**
 * @file main_wiiu_test.c
 * @brief Minimal Wii U test — colored screen + HOME button
 *
 * This bypasses ALL game logic to test the basic Wii U lifecycle:
 *   - WHB proc init
 *   - SD card mount
 *   - GX2 rendering (clear color)
 *   - HOME button via ProcUI
 *   - Clean exit
 *
 * Usage: Temporarily replace src/main.c with this file, rebuild, test.
 *        Once HOME button works, swap the real main.c back and we know
 *        the problem is in the game init, not the Wii U port layer.
 */

#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/event.h>
#include <whb/proc.h>
#include <whb/gfx.h>
#include <whb/sdcard.h>
#include <whb/log.h>
#include <whb/log_udp.h>

#include <stdio.h>
#include <stdbool.h>

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    /* Initialize ProcUI (handles HOME button) */
    WHBProcInit();
    OSReport("[TEST] ProcUI initialized\n");

    /* Mount SD card */
    bool sd_ok = WHBMountSdCard();
    OSReport("[TEST] SD card mount: %s\n", sd_ok ? "OK" : "FAILED");

    /* Test if we can open the AFS file */
    if (sd_ok) {
        FILE* f = fopen("fs:/vol/external01/wiiu/apps/3sx/SF33RD.AFS", "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fclose(f);
            OSReport("[TEST] SF33RD.AFS found! Size: %ld bytes\n", size);
        } else {
            OSReport("[TEST] SF33RD.AFS NOT FOUND at expected path\n");
        }
    }

    /* Initialize GX2 rendering */
    WHBGfxInit();
    OSReport("[TEST] GX2 initialized\n");

    /* Color cycle state */
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.3f;
    int frame = 0;

    OSReport("[TEST] Entering main loop\n");

    /* Main loop — runs until HOME → Close */
    while (WHBProcIsRunning()) {
        /* Slowly cycle the blue channel so we can see the screen is alive */
        blue = 0.2f + 0.3f * ((float)(frame % 120) / 120.0f);
        frame++;

        /* Render to TV */
        WHBGfxBeginRender();
        WHBGfxBeginRenderTV();
        WHBGfxClearColor(red, green, blue, 1.0f);
        WHBGfxFinishRenderTV();

        /* Render to GamePad (same color) */
        WHBGfxBeginRenderDRC();
        WHBGfxClearColor(red, green, blue, 1.0f);
        WHBGfxFinishRenderDRC();

        /* Swap buffers */
        WHBGfxFinishRender();

        /* VSync — paces at 60fps and gives OS time for HOME events */
        GX2WaitForVsync();
    }

    OSReport("[TEST] Exiting cleanly\n");

    /* Cleanup */
    WHBGfxShutdown();
    if (sd_ok) WHBUnmountSdCard();
    WHBProcShutdown();

    return 0;
}
