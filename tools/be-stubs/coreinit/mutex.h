#ifndef STUB_COREINIT_MUTEX_H
#define STUB_COREINIT_MUTEX_H
#include <wut_types.h>
typedef struct OSMutex { char opaque[68]; } OSMutex;
void OSInitMutex(OSMutex* m);
void OSLockMutex(OSMutex* m);
void OSUnlockMutex(OSMutex* m);
#endif
