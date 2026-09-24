#ifndef STUB_COREINIT_TIME_H
#define STUB_COREINIT_TIME_H
#include <wut_types.h>
#include <stdint.h>
typedef int64_t OSTime;
typedef int32_t OSTick;
OSTime OSGetTime(void);
OSTick OSGetTick(void);
OSTime OSSecondsToTicks(OSTime s);
OSTime OSMillisecondsToTicks(OSTime ms);
OSTime OSMicrosecondsToTicks(OSTime us);
#endif
