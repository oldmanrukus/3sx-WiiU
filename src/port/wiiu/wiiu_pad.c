/**
 * @file wiiu_pad.c
 * @brief Wii U gamepad input — replaces sdl_pad.c
 *
 * Maps Wii U VPAD (GamePad) and KPAD (Pro Controller) to the
 * SDLPad_ButtonState struct that sdk_libpad2.c expects.
 *
 * Slot 0 = VPAD (always the Wii U GamePad)
 * Slot 1 = First connected WPAD/KPAD Pro Controller
 */
#include "port/wiiu/wiiu_pad.h"

#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <coreinit/debug.h>

#include <string.h>

#define INPUT_SOURCES_MAX 2

static bool kpad_initialized = false;
static bool vpad_connected = true;  /* GamePad is always connected in practice */
static bool kpad_connected[4] = { false };

/* ======================================
 * Initialization
 * ====================================== */

void SDLPad_Init(void) {
    /* VPAD is auto-initialized by the system */
    VPADInit();

    /* Initialize KPAD for Pro Controller / Classic Controller support */
    KPADInit();
    WPADEnableURCC(true);
    WPADEnableWiiRemote(true);
    kpad_initialized = true;
}

/* ======================================
 * Stubs for SDL-specific events (not used on Wii U)
 * ====================================== */

void SDLPad_HandleGamepadDeviceEvent(SDL_GamepadDeviceEvent* event) {
    /* No-op: Wii U polls controllers directly each frame */
    (void)event;
}

/* ======================================
 * Connection check
 * ====================================== */

bool SDLPad_IsGamepadConnected(int id) {
    if (id == 0) {
        return vpad_connected;
    }

    if (id == 1 && kpad_initialized) {
        /* Check if any KPAD channel has a Pro Controller connected */
        for (int ch = 0; ch < 4; ch++) {
            WPADExtensionType ext;
            int32_t err = WPADProbe((WPADChan)ch, &ext);
            if (err == 0 && ext == WPAD_EXT_PRO_CONTROLLER) {
                return true;
            }
        }
        /* Also accept Classic Controller */
        for (int ch = 0; ch < 4; ch++) {
            WPADExtensionType ext;
            int32_t err = WPADProbe((WPADChan)ch, &ext);
            if (err == 0 && ext == WPAD_EXT_CLASSIC) {
                return true;
            }
        }
    }

    return false;
}

/* ======================================
 * VPAD → ButtonState mapping
 * ====================================== */

static void get_vpad_state(SDLPad_ButtonState* state) {
    VPADStatus vpad;
    VPADReadError error = VPAD_READ_SUCCESS;

    memset(state, 0, sizeof(*state));

    int vret = VPADRead(VPAD_CHAN_0, &vpad, 1, &error);
    { static int fc = 0; if (fc % 120 == 0) OSReport("[3SX] VPAD: ret=%d err=%d\n", vret, error); fc++; }
    if (vret <= 0 || error != VPAD_READ_SUCCESS) {
        vpad_connected = false;
        return;
    }

    vpad_connected = true;
    uint32_t hold = vpad.hold;
    { static int vdbg = 0; if (hold && vdbg < 10) { OSReport("[3SX] VPAD hold=0x%08X\n", hold); vdbg++; } }

    /* D-pad */
    state->dpad_up    = (hold & VPAD_BUTTON_UP) != 0;
    state->dpad_down  = (hold & VPAD_BUTTON_DOWN) != 0;
    state->dpad_left  = (hold & VPAD_BUTTON_LEFT) != 0;
    state->dpad_right = (hold & VPAD_BUTTON_RIGHT) != 0;

    /* Face buttons — map to SNES-style layout (3rd Strike convention):
     * PS2 Cross    → Wii U B      (south)
     * PS2 Circle   → Wii U A      (east)
     * PS2 Square   → Wii U Y      (west)
     * PS2 Triangle → Wii U X      (north) */
    state->south = (hold & VPAD_BUTTON_B) != 0;
    state->east  = (hold & VPAD_BUTTON_A) != 0;
    state->west  = (hold & VPAD_BUTTON_Y) != 0;
    state->north = (hold & VPAD_BUTTON_X) != 0;

    /* Shoulders / triggers */
    state->left_shoulder  = (hold & VPAD_BUTTON_L) != 0;
    state->right_shoulder = (hold & VPAD_BUTTON_R) != 0;
    state->left_trigger   = (hold & VPAD_BUTTON_ZL) ? INT16_MAX : 0;
    state->right_trigger  = (hold & VPAD_BUTTON_ZR) ? INT16_MAX : 0;

    /* Sticks (VPAD gives float -1.0..1.0, convert to Sint16 range) */
    state->left_stick_x  = (Sint16)(vpad.leftStick.x  * 32767.0f);
    state->left_stick_y  = (Sint16)(vpad.leftStick.y  * 32767.0f);
    state->right_stick_x = (Sint16)(vpad.rightStick.x * 32767.0f);
    state->right_stick_y = (Sint16)(vpad.rightStick.y * 32767.0f);

    /* Meta buttons */
    state->start      = (hold & VPAD_BUTTON_PLUS) != 0;
    state->back       = (hold & VPAD_BUTTON_MINUS) != 0;
    state->left_stick  = (hold & VPAD_BUTTON_STICK_L) != 0;
    state->right_stick = (hold & VPAD_BUTTON_STICK_R) != 0;
}

/* ======================================
 * KPAD Pro Controller → ButtonState mapping
 * ====================================== */

static int find_pro_controller_channel(void) {
    for (int ch = 0; ch < 4; ch++) {
        WPADExtensionType ext;
        int32_t err = WPADProbe((WPADChan)ch, &ext);
        if (err == 0 && (ext == WPAD_EXT_PRO_CONTROLLER || ext == WPAD_EXT_CLASSIC)) {
            return ch;
        }
    }
    return -1;
}

static void get_kpad_state(SDLPad_ButtonState* state) {
    memset(state, 0, sizeof(*state));

    int ch = find_pro_controller_channel();
    if (ch < 0) return;

    KPADStatus kpad;
    int32_t err;

    if (KPADReadEx((KPADChan)ch, &kpad, 1, &err) <= 0) {
        return;
    }

    if (kpad.extensionType == WPAD_EXT_PRO_CONTROLLER) {
        uint32_t hold = kpad.pro.hold;

        state->dpad_up    = (hold & WPAD_PRO_BUTTON_UP) != 0;
        state->dpad_down  = (hold & WPAD_PRO_BUTTON_DOWN) != 0;
        state->dpad_left  = (hold & WPAD_PRO_BUTTON_LEFT) != 0;
        state->dpad_right = (hold & WPAD_PRO_BUTTON_RIGHT) != 0;

        state->south = (hold & WPAD_PRO_BUTTON_B) != 0;
        state->east  = (hold & WPAD_PRO_BUTTON_A) != 0;
        state->west  = (hold & WPAD_PRO_BUTTON_Y) != 0;
        state->north = (hold & WPAD_PRO_BUTTON_X) != 0;

        state->left_shoulder  = (hold & WPAD_PRO_TRIGGER_L) != 0;
        state->right_shoulder = (hold & WPAD_PRO_TRIGGER_R) != 0;
        state->left_trigger   = (hold & WPAD_PRO_TRIGGER_ZL) ? INT16_MAX : 0;
        state->right_trigger  = (hold & WPAD_PRO_TRIGGER_ZR) ? INT16_MAX : 0;

        state->start      = (hold & WPAD_PRO_BUTTON_PLUS) != 0;
        state->back       = (hold & WPAD_PRO_BUTTON_MINUS) != 0;
        state->left_stick  = (hold & WPAD_PRO_BUTTON_STICK_L) != 0;
        state->right_stick = (hold & WPAD_PRO_BUTTON_STICK_R) != 0;

        /* Pro Controller sticks: KPAD gives float -1..1 */
        state->left_stick_x  = (Sint16)(kpad.pro.leftStick.x  * 32767.0f);
        state->left_stick_y  = (Sint16)(kpad.pro.leftStick.y  * 32767.0f);
        state->right_stick_x = (Sint16)(kpad.pro.rightStick.x * 32767.0f);
        state->right_stick_y = (Sint16)(kpad.pro.rightStick.y * 32767.0f);

    } else if (kpad.extensionType == WPAD_EXT_CLASSIC) {
        uint32_t hold = kpad.classic.hold;

        state->dpad_up    = (hold & WPAD_CLASSIC_BUTTON_UP) != 0;
        state->dpad_down  = (hold & WPAD_CLASSIC_BUTTON_DOWN) != 0;
        state->dpad_left  = (hold & WPAD_CLASSIC_BUTTON_LEFT) != 0;
        state->dpad_right = (hold & WPAD_CLASSIC_BUTTON_RIGHT) != 0;

        state->south = (hold & WPAD_CLASSIC_BUTTON_B) != 0;
        state->east  = (hold & WPAD_CLASSIC_BUTTON_A) != 0;
        state->west  = (hold & WPAD_CLASSIC_BUTTON_Y) != 0;
        state->north = (hold & WPAD_CLASSIC_BUTTON_X) != 0;

        state->left_shoulder  = (hold & WPAD_CLASSIC_BUTTON_L) != 0;
        state->right_shoulder = (hold & WPAD_CLASSIC_BUTTON_R) != 0;
        state->left_trigger   = (hold & WPAD_CLASSIC_BUTTON_ZL) ? INT16_MAX : 0;
        state->right_trigger  = (hold & WPAD_CLASSIC_BUTTON_ZR) ? INT16_MAX : 0;

        state->start = (hold & WPAD_CLASSIC_BUTTON_PLUS) != 0;
        state->back  = (hold & WPAD_CLASSIC_BUTTON_MINUS) != 0;

        state->left_stick_x  = (Sint16)(kpad.classic.leftStick.x  * 32767.0f);
        state->left_stick_y  = (Sint16)(kpad.classic.leftStick.y  * 32767.0f);
        state->right_stick_x = (Sint16)(kpad.classic.rightStick.x * 32767.0f);
        state->right_stick_y = (Sint16)(kpad.classic.rightStick.y * 32767.0f);
    }
}

/* ======================================
 * Public API
 * ====================================== */

void SDLPad_GetButtonState(int id, SDLPad_ButtonState* state) {
    if (id == 0) {
        get_vpad_state(state);
    } else if (id == 1) {
        get_kpad_state(state);
    } else {
        memset(state, 0, sizeof(*state));
    }
}

void SDLPad_RumblePad(int id, bool low_freq_enabled, Uint8 high_freq_rumble) {
    /* VPAD rumble — GamePad has a single rumble motor via VPADControlMotor */
    if (id == 0) {
        if (low_freq_enabled || high_freq_rumble > 0) {
            VPADControlMotor(VPAD_CHAN_0, NULL, 250); /* 250ms rumble */
        } else {
            VPADStopMotor(VPAD_CHAN_0);
        }
    }

    /* Pro Controller rumble is not easily accessible via KPAD;
     * would require low-level WPAD rumble commands. Skip for now. */
}
