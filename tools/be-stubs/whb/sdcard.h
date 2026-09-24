#ifndef STUB_WHB_SDCARD_H
#define STUB_WHB_SDCARD_H
#include <wut_types.h>
#include <stdbool.h>
bool WHBMountSdCard(void);
bool WHBUnmountSdCard(void);
const char* WHBGetSdCardMountPath(void);
#endif
