#ifndef STUB_COREINIT_CACHE_H
#define STUB_COREINIT_CACHE_H
#include <wut_types.h>
#include <stdint.h>
void DCFlushRange(void* addr, uint32_t size);
void DCInvalidateRange(void* addr, uint32_t size);
void DCStoreRange(void* addr, uint32_t size);
#endif
