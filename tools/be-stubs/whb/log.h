#ifndef STUB_WHB_LOG_H
#define STUB_WHB_LOG_H
#include <wut_types.h>
void WHBLogPrint(const char* s);
void WHBLogPrintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
