/**
 * @file wiiu_pad.h
 * @brief Wii U gamepad input — replaces sdl_pad.h
 *
 * Provides the same SDLPad_ButtonState struct and function signatures
 * so that sdk_libpad2.c (the PS2 pad shim) compiles unmodified.
 */
#ifndef WIIU_PAD_H
#define WIIU_PAD_H

#include <stdbool.h>
#include <stdint.h>

/* Re-use the same button state layout expected by sdk_libpad2.c.
 * Sint16 is just int16_t on non-SDL platforms. */
typedef int16_t Sint16;
typedef uint8_t Uint8;
typedef uint32_t Uint32;

typedef struct SDLPad_ButtonState {
    bool south;
    bool east;
    bool west;
    bool north;
    bool back;
    bool start;
    bool left_stick;
    bool right_stick;
    bool left_shoulder;
    bool right_shoulder;
    Sint16 left_trigger;
    Sint16 right_trigger;
    bool dpad_up;
    bool dpad_down;
    bool dpad_left;
    bool dpad_right;
    Sint16 left_stick_x;
    Sint16 left_stick_y;
    Sint16 right_stick_x;
    Sint16 right_stick_y;
} SDLPad_ButtonState;

/* Stub type — Wii U doesn't use SDL gamepad device events */
typedef struct { int type; } SDL_GamepadDeviceEvent;

void SDLPad_Init(void);
void SDLPad_HandleGamepadDeviceEvent(SDL_GamepadDeviceEvent* event);
bool SDLPad_IsGamepadConnected(int id);
void SDLPad_GetButtonState(int id, SDLPad_ButtonState* state);
void SDLPad_RumblePad(int id, bool low_freq_enabled, Uint8 high_freq_rumble);

#endif
