/**
 * @file wiiu_trace.h
 * @brief Per-frame tracing for the Wii U port.
 *
 * OSReport() writes to the Cafe OS console synchronously, so the frame-by-frame
 * tracing used to find the boot and VS-screen hangs costs far too much to leave
 * on. Build with -DWIIU_TRACE to get it back.
 *
 * Warnings and one-shot startup messages stay on OSReport() directly — this is
 * only for output that repeats every frame.
 */
#ifndef PORT_WIIU_TRACE_H
#define PORT_WIIU_TRACE_H

#if defined(TARGET_WIIU) && defined(WIIU_TRACE)
#include <coreinit/debug.h>
#define WIIU_TRACE_LOG(...) OSReport(__VA_ARGS__)
#else
#define WIIU_TRACE_LOG(...) ((void)0)
#endif

#endif
