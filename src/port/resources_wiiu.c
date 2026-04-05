/**
 * @file resources_wiiu.c
 * @brief Wii U resource loading — replaces SDL dialog-based resources.c
 */
#include "port/resources.h"
#include "port/paths.h"

#include <coreinit/debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static char afs_path[256] = { 0 };

char* Resources_GetPath(const char* file_path) {
    const char* base = Paths_GetBasePath();
    size_t base_len = strlen(base);

    if (file_path == NULL) {
        char* path = (char*)malloc(base_len + 1);
        strcpy(path, base);
        return path;
    }

    size_t file_len = strlen(file_path);
    char* path = (char*)malloc(base_len + file_len + 1);
    strcpy(path, base);
    strcat(path, file_path);
    return path;
}

bool Resources_Check(void) {
    snprintf(afs_path, sizeof(afs_path), "%sSF33RD.AFS", Paths_GetBasePath());

    FILE* f = fopen(afs_path, "rb");
    if (f) {
        fclose(f);
        OSReport("[3SX] Found AFS at: %s\n", afs_path);
        return true;
    }

    snprintf(afs_path, sizeof(afs_path),
             "fs:/vol/external01/3sx/SF33RD.AFS");
    f = fopen(afs_path, "rb");
    if (f) {
        fclose(f);
        OSReport("[3SX] Found AFS at: %s\n", afs_path);
        return true;
    }

    OSReport("[3SX] ERROR: SF33RD.AFS not found!\n");
    OSReport("[3SX] Place it at: sd:/wiiu/apps/3sx/SF33RD.AFS\n");
    return false;
}

bool Resources_RunResourceCopyingFlow(void) {
    return true;
}

const char* Resources_GetAFSPath(void) {
    return afs_path;
}
