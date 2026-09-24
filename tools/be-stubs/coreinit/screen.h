#ifndef STUB_COREINIT_SCREEN_H
#define STUB_COREINIT_SCREEN_H
#include <wut_types.h>
#include <stdint.h>
typedef enum { SCREEN_TV = 0, SCREEN_DRC = 1 } OSScreenID;
void OSScreenInit(void);
uint32_t OSScreenGetBufferSizeEx(OSScreenID id);
void OSScreenSetBufferEx(OSScreenID id, void* buf);
void OSScreenEnableEx(OSScreenID id, int enable);
void OSScreenClearBufferEx(OSScreenID id, uint32_t color);
void OSScreenPutFontEx(OSScreenID id, uint32_t col, uint32_t row, const char* str);
void OSScreenFlipBuffersEx(OSScreenID id);
#endif
