#ifndef STUB_COREINIT_DEBUG_H
#define STUB_COREINIT_DEBUG_H
#include <wut_types.h>
void OSReport(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void OSFatal(const char* msg);
#endif
