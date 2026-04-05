/**
 * @file paths_wiiu.c
 * @brief Wii U path helpers — replaces SDL_GetPrefPath / SDL_GetBasePath
 *
 * On Wii U homebrew, files live on the SD card under:
 *   sd:/wiiu/apps/3sx/        (application + assets)
 *   sd:/wiiu/apps/3sx/save/   (preferences / config)
 */
#include "port/paths.h"

#include <string.h>

static const char* base_path = "fs:/vol/external01/wiiu/apps/3sx/";
static const char* pref_path = "fs:/vol/external01/wiiu/apps/3sx/save/";

const char* Paths_GetPrefPath(void) {
    return pref_path;
}

const char* Paths_GetBasePath(void) {
    return base_path;
}
