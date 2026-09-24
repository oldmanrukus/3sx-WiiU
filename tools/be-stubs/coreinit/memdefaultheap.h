#ifndef STUB_COREINIT_MEMDEFAULTHEAP_H
#define STUB_COREINIT_MEMDEFAULTHEAP_H
#include <wut_types.h>
#include <stdint.h>
#include <stdlib.h>
void* MEMAllocFromDefaultHeap(uint32_t size);
void* MEMAllocFromDefaultHeapEx(uint32_t size, int align);
void MEMFreeToDefaultHeap(void* ptr);
#endif
