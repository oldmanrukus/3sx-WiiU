/**
 * @file wiiu_pad.c
 * @brief Wii U gamepad input — uses SDL2 GameController API
 *
 * SDL2-wiiu maps the Wii U GamePad as an SDL GameController.
 * Using SDL's API avoids VPADRead conflicts where SDL_PollEvent
 * consumes the VPAD data on real hardware.
 */
#include "port/wiiu/wiiu_pad.h"

#include <SDL2/SDL.h>
#include <coreinit/debug.h>
#include <string.h>

#define INPUT_SOURCES_MAX 2

static SDL_GameController* controllers[INPUT_SOURCES_MAX] = { NULL };
static bool initialized = false;

/* ======================================
 * Initialization
 * ====================================== */

void SDLPad_Init(void) {
    if (!(SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER)) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    }

    /* Open any connected controllers */
    int num = SDL_NumJoysticks();
    OSReport("[3SX] SDLPad_Init: %d joysticks found\n", num);

    int opened = 0;
    for (int i = 0; i < num && opened < INPUT_SOURCES_MAX; i++) {
        if (SDL_IsGameController(i)) {
            controllers[opened] = SDL_GameControllerOpen(i);
            if (controllers[opened]) {
                OSReport("[3SX] SDLPad: Opened controller %d: %s\n",
                    opened, SDL_GameControllerName(controllers[opened]));
                opened++;
            }
        }
    }

    initialized = true;
}

/* ======================================
 * Stubs for SDL-specific events
 * ====================================== */

void SDLPad_HandleGamepadDeviceEvent(SDL_GamepadDeviceEvent* event) {
    /* Check for new controllers on hotplug */
    if (!initialized) return;

    int num = SDL_NumJoysticks();
    for (int i = 0; i < num && i < INPUT_SOURCES_MAX; i++) {
        if (SDL_IsGameController(i) && !controllers[i]) {
            controllers[i] = SDL_GameControllerOpen(i);
            if (controllers[i]) {
                OSReport("[3SX] SDLPad: Hotplug controller %d: %s\n",
                    i, SDL_GameControllerName(controllers[i]));
            }
        }
    }
}

/* ======================================
 * Connection check
 * ====================================== */

bool SDLPad_IsGamepadConnected(int id) {
    if (id >= 0 && id < INPUT_SOURCES_MAX && controllers[id]) {
        return SDL_GameControllerGetAttached(controllers[id]);
    }
    return false;
}

/* ======================================
 * SDL GameController → ButtonState mapping
 * ====================================== */

void SDLPad_GetButtonState(int id, SDLPad_ButtonState* state) {
    memset(state, 0, sizeof(*state));

    if (id < 0 || id >= INPUT_SOURCES_MAX || !controllers[id]) return;

    SDL_GameController* gc = controllers[id];
    if (!SDL_GameControllerGetAttached(gc)) return;

    /* Update internal state */
    SDL_GameControllerUpdate();

    /* D-pad */
    state->dpad_up    = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP);
    state->dpad_down  = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    state->dpad_left  = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    state->dpad_right = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

    /* Face buttons — map to PS2 layout:
     * PS2 Cross    → SDL B (south) → Wii U B
     * PS2 Circle   → SDL A (east)  → Wii U A
     * PS2 Square   → SDL X (west)  → Wii U Y
     * PS2 Triangle → SDL Y (north) → Wii U X */
    state->south = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B);
    state->east  = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A);
    state->west  = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X);
    state->north = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y);

    /* Shoulders / triggers */
    state->left_shoulder  = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    state->right_shoulder = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);

    Sint16 lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    Sint16 rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    state->left_trigger  = lt > 8000 ? INT16_MAX : 0;
    state->right_trigger = rt > 8000 ? INT16_MAX : 0;

    /* Sticks */
    state->left_stick_x  = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    state->left_stick_y  = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    state->right_stick_x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
    state->right_stick_y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);

    /* Meta buttons */
    state->start      = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START);
    state->back       = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK);
    state->left_stick  = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    state->right_stick = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
}

void SDLPad_RumblePad(int id, bool low_freq_enabled, Uint8 high_freq_rumble) {
    if (id < 0 || id >= INPUT_SOURCES_MAX || !controllers[id]) return;
    
    if (low_freq_enabled || high_freq_rumble > 0) {
        SDL_GameControllerRumble(controllers[id], 0xFFFF, 0xFFFF, 250);
    } else {
        SDL_GameControllerRumble(controllers[id], 0, 0, 0);
    }
}
