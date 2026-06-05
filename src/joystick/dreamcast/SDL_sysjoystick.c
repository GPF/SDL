/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"
#include <SDL_events.h>
#if defined(SDL_JOYSTICK_DREAMCAST)

/* This is the Dreamcast implementation of the SDL joystick API */

#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"
#include "dc/maple/purupuru.h"
#include <dc/maple.h>
#include <dc/maple/controller.h>

#define MAX_JOYSTICKS 8
#define MAX_AXES 6
#define MAX_BUTTONS 8
#define MAX_HATS 2

static maple_device_t *SYS_Joystick_addr[MAX_JOYSTICKS];
static maple_device_t *SYS_Rumble_device[MAX_JOYSTICKS];  // Use maple_device_t for rumble device

typedef struct joystick_hwdata {
    cont_state_t prev_state;
} jhwdata_t;

static const int sdl_buttons[] = {
    CONT_C,
    CONT_B,
    CONT_A,
    CONT_START,
    CONT_Z,
    CONT_Y,
    CONT_X,
    CONT_D
};

static int DREAMCAST_JoystickInit(void) {
    int numdevs = 0, i;
    maple_device_t *dev;
    maple_device_t *rumble_dev;

    printf("Initializing joysticks...\n");

    // Loop through all potential ports
    for (i = 0; i < MAX_JOYSTICKS; ++i) {
        dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
        if (dev) {
            SYS_Joystick_addr[numdevs++] = dev;
            rumble_dev = maple_enum_type(i, MAPLE_FUNC_PURUPURU);  // Check for rumble functionality
            if (rumble_dev) {
                SYS_Rumble_device[numdevs - 1] = rumble_dev;  // Store rumble device if present
                printf("Rumble device found at port %d\n", i);
            } else {
                SYS_Rumble_device[numdevs - 1] = NULL;  // No rumble support at this port
                printf("No rumble device at port %d\n", i);
            }
            printf("Joystick found at port %d\n", i);
        } else {
            printf("No joystick at port %d\n", i);
        }
    }

    // Fallback to a default controller if no joystick is found
    if (numdevs == 0) {
        printf("No joysticks detected, defaulting to 1 controller.\n");
        SYS_Joystick_addr[0] = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        rumble_dev = maple_enum_type(0, MAPLE_FUNC_PURUPURU);
        if (SYS_Joystick_addr[0]) {
            SYS_Rumble_device[0] = rumble_dev;
            numdevs = 1;
        }
    }

    for (i = 0; i < numdevs; ++i) {
        SDL_PrivateJoystickAdded(i);
    }

    printf("Number of joysticks initialized: %d\n", numdevs);
    return numdevs;
}

static int DREAMCAST_JoystickGetCount(void) {
    int count = 0;
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        if (SYS_Joystick_addr[i]) {
            count++;
        }
    }
    // printf("Number of joysticks detected: %d\n", count);
    return count;
}

static void DREAMCAST_JoystickDetect(void)
{
    // Dreamcast does not dynamically detect joysticks
}

static const char *DREAMCAST_JoystickGetDeviceName(int device_index)
{
    maple_device_t *dev;
    if (device_index >= MAX_JOYSTICKS || !(dev = SYS_Joystick_addr[device_index])) {
        return NULL;
    }
    return dev->info.product_name;
}

static const char *DREAMCAST_JoystickGetDevicePath(int device_index)
{
    return NULL; // No specific path for Dreamcast joysticks
}

static int DREAMCAST_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    return -1;
}

static int DREAMCAST_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void DREAMCAST_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_JoystickGUID DREAMCAST_JoystickGetDeviceGUID(int device_index)
{
    const char *guid_string = "ff0013db5669727475616c2043007601"; // Your Dreamcast GUID
    return SDL_JoystickGetGUIDFromString(guid_string);
}

static SDL_JoystickID DREAMCAST_JoystickGetDeviceInstanceID(int device_index)
{
    return device_index;
}

static int DREAMCAST_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    maple_device_t *dev = SYS_Joystick_addr[device_index];
    if (!dev) {
        return SDL_SetError("No joystick found at index %d", device_index);
    }

    joystick->hwdata = (struct joystick_hwdata *)SDL_malloc(sizeof(jhwdata_t));
    if (joystick->hwdata == NULL) {
        return SDL_OutOfMemory();
    }

    SDL_memset(joystick->hwdata, 0, sizeof(jhwdata_t));

    joystick->nbuttons = MAX_BUTTONS;
    joystick->naxes = MAX_AXES;
    joystick->nhats = MAX_HATS;

    return 0;
}

// Convert a rumble value (0–32767) to a 3‑bit intensity (0–7)
// Using rounding so that 32767 maps to 7.
static inline uint32_t convert_rumble_intensity(Uint16 value) {
    return (uint32_t)(((unsigned long)value * 7 + (32767 / 2)) / 32767);
}

static int DREAMCAST_JoystickRumble(SDL_Joystick *joystick,
                                      Uint16 low_frequency_rumble,
                                      Uint16 high_frequency_rumble)
{
    maple_device_t *dev = SYS_Joystick_addr[joystick->instance_id];
    maple_device_t *rumble_dev = SYS_Rumble_device[joystick->instance_id];

    SDL_Log("DREAMCAST_JoystickRumble: instance_id=%d, low_frequency_rumble=%d, high_frequency_rumble=%d\n",
            joystick->instance_id, low_frequency_rumble, high_frequency_rumble);

    /* Check if the device supports rumble */
    if (!dev || !rumble_dev) {
        // SDL_Log("Device or rumble device not found or not open. instance_id=%d\n", joystick->instance_id);
        return SDL_Unsupported();
    }

    SDL_Log("Sending rumble command to device %d\n", joystick->instance_id);

    purupuru_effect_t effect;
    SDL_memset(&effect, 0, sizeof(effect));

    /* Set up the rumble effect:
     * - We'll use a fixed duration (e.g., 255).
     * - Convert the low- and high-frequency values to 3-bit intensities (0–7).
     * - Combine these using the appropriate macros.
     *
     * Note: The macros PURUPURU_EFFECT1_INTENSITY, PURUPURU_EFFECT2_LINTENSITY, 
     * PURUPURU_EFFECT2_UINTENSITY, and PURUPURU_SPECIAL_MOTOR1 are defined in dc/maple/purupuru.h.
     */
    effect.duration = 255;  // Adjust duration as needed

    uint32_t low_intensity = convert_rumble_intensity(low_frequency_rumble);
    uint32_t high_intensity = convert_rumble_intensity(high_frequency_rumble);

    effect.effect1 = PURUPURU_EFFECT1_INTENSITY(low_intensity);
    effect.effect2 = PURUPURU_EFFECT2_LINTENSITY(low_intensity) | PURUPURU_EFFECT2_UINTENSITY(high_intensity);
    effect.special = PURUPURU_SPECIAL_MOTOR1;  // Use motor 1

    int result = purupuru_rumble(rumble_dev, &effect);
    if (result == MAPLE_EOK) {
        // SDL_Log("Rumble command sent successfully to device %d.\n", joystick->instance_id);
        return 0;
    } else {
        SDL_Log("Failed to send rumble command to device %d. Error: %d\n", joystick->instance_id, result);
        return SDL_SetError("Failed to send rumble command");
    }
}



static int DREAMCAST_JoystickRumbleTriggers(SDL_Joystick *joystick,
                                              Uint16 left_rumble,
                                              Uint16 right_rumble)
{
    /* Combine the two rumble values into one.
       Since the hardware only supports one rumble channel,
       we can simply average the two values. */
    Uint16 combined_rumble = (left_rumble + right_rumble) / 2;
// SDL_Log("DREAMCAST_JoystickRumbleTriggers: left_rumble=%d, right_rumble=%d, combined_rumble=%d\n",
//             left_rumble, right_rumble, combined_rumble);
    /* Forward the combined value to the main rumble function */
    return DREAMCAST_JoystickRumble(joystick, combined_rumble, combined_rumble);
}



static Uint32 DREAMCAST_JoystickGetCapabilities(SDL_Joystick *joystick)
{
return SDL_JOYCAP_RUMBLE;
}

static int DREAMCAST_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static int DREAMCAST_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static int DREAMCAST_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled)
{
    return SDL_Unsupported();
}

int normalize_trigger(int value) {
    // Ensure the value is within the expected range (0-255)
    if (value < 0) value = 0;
    if (value > 255) value = 255;

    // Scale the value to the SDL2 range (0-32767) with proper rounding
    return (32767 - (value *256));
}

static void DREAMCAST_JoystickUpdate(SDL_Joystick *joystick) {
    maple_device_t *dev;
    cont_state_t *state;
    int buttons, i, changed, hat;

    dev = SYS_Joystick_addr[joystick->instance_id];

    // Get current state from the device
    if (!(state = (cont_state_t *)maple_dev_status(dev))) {
        return;
    }

    // SDL_Log("Raw trigger values: rtrig=%d, ltrig=%d\n", state->rtrig, state->ltrig);

    buttons = state->buttons;
    cont_state_t *prev_state = &joystick->hwdata->prev_state;
    changed = buttons ^ prev_state->buttons;

    // Process D-Pad
    if (changed & (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)) {
        hat = SDL_HAT_CENTERED;
        if (buttons & CONT_DPAD_UP) hat |= SDL_HAT_UP;
        if (buttons & CONT_DPAD_DOWN) hat |= SDL_HAT_DOWN;
        if (buttons & CONT_DPAD_LEFT) hat |= SDL_HAT_LEFT;
        if (buttons & CONT_DPAD_RIGHT) hat |= SDL_HAT_RIGHT;
        SDL_PrivateJoystickHat(joystick, 0, hat);
    }

    if (changed & (CONT_DPAD2_UP | CONT_DPAD2_DOWN | CONT_DPAD2_LEFT | CONT_DPAD2_RIGHT)) {
        hat = SDL_HAT_CENTERED;
        if (buttons & CONT_DPAD2_UP) hat |= SDL_HAT_UP;
        if (buttons & CONT_DPAD2_DOWN) hat |= SDL_HAT_DOWN;
        if (buttons & CONT_DPAD2_LEFT) hat |= SDL_HAT_LEFT;
        if (buttons & CONT_DPAD2_RIGHT) hat |= SDL_HAT_RIGHT;
        SDL_PrivateJoystickHat(joystick, 1, hat);
    }

    // Process buttons
    for (i = 0; i < MAX_BUTTONS; ++i) {
        if (changed & sdl_buttons[i]) {
            SDL_PrivateJoystickButton(joystick, i, (buttons & sdl_buttons[i]) ? SDL_PRESSED : SDL_RELEASED);
        }
    }

    // Scale Joystick Axes for main sticks
    if (state->joyx != prev_state->joyx)
        SDL_PrivateJoystickAxis(joystick, 0, state->joyx * 256);
    if (state->joyy != prev_state->joyy)
        SDL_PrivateJoystickAxis(joystick, 1, state->joyy * 256);
    if (state->joy2x != prev_state->joy2x)
        SDL_PrivateJoystickAxis(joystick, 4, state->joy2x * 256);
    if (state->joy2y != prev_state->joy2y)
        SDL_PrivateJoystickAxis(joystick, 5, state->joy2y * 256);

    // Process triggers as axes
    int rtrig = normalize_trigger(state->rtrig);
    int ltrig = normalize_trigger(state->ltrig);
    int prev_rtrig = normalize_trigger(prev_state->rtrig);
    int prev_ltrig = normalize_trigger(prev_state->ltrig);

    if (rtrig != prev_rtrig)
        SDL_PrivateJoystickAxis(joystick, 2, rtrig);
    if (ltrig != prev_ltrig)
        SDL_PrivateJoystickAxis(joystick, 3, ltrig);

    // SDL_Log("Normalized trigger values: rtrig=%d, ltrig=%d\n", rtrig, ltrig);

    // Treat triggers as buttons:
// if (rtrig >= (32000) && prev_rtrig < (32000)) {
//     SDL_PrivateJoystickButton(joystick, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_PRESSED);
//     // SDL_Log("Right trigger pressed\n");
// } else if (rtrig < (32000) && prev_rtrig >= (32000)) {
//     SDL_PrivateJoystickButton(joystick, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_RELEASED);
//     // SDL_Log("Right trigger released\n");
// }

// if (ltrig >= (32000) && prev_ltrig < (32000)) {
//     SDL_PrivateJoystickButton(joystick, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_PRESSED);
//     // SDL_Log("Left trigger pressed\n");
// } else if (ltrig < (32000) && prev_ltrig >= (32000)) {
//     SDL_PrivateJoystickButton(joystick, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_RELEASED);
//     // SDL_Log("Left trigger released\n");
// }

    // Save the raw trigger state for the next frame
    prev_state->rtrig = state->rtrig;
    prev_state->ltrig = state->ltrig;
    joystick->hwdata->prev_state = *state;
}

static void DREAMCAST_JoystickClose(SDL_Joystick *joystick)
{
    if (joystick->hwdata != NULL) {
        SDL_free(joystick->hwdata);
    }
}

static void DREAMCAST_JoystickQuit(void)
{
    // Cleanup if needed
}

static SDL_bool DREAMCAST_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    return SDL_FALSE;
}

SDL_JoystickDriver SDL_DREAMCAST_JoystickDriver = {
    DREAMCAST_JoystickInit,
    DREAMCAST_JoystickGetCount,
    DREAMCAST_JoystickDetect,
    DREAMCAST_JoystickGetDeviceName,
    DREAMCAST_JoystickGetDevicePath,
    DREAMCAST_JoystickGetDeviceSteamVirtualGamepadSlot,
    DREAMCAST_JoystickGetDevicePlayerIndex,
    DREAMCAST_JoystickSetDevicePlayerIndex,
    DREAMCAST_JoystickGetDeviceGUID,
    DREAMCAST_JoystickGetDeviceInstanceID,
    DREAMCAST_JoystickOpen,
    DREAMCAST_JoystickRumble,
    DREAMCAST_JoystickRumbleTriggers,
    DREAMCAST_JoystickGetCapabilities,
    DREAMCAST_JoystickSetLED,
    DREAMCAST_JoystickSendEffect,
    DREAMCAST_JoystickSetSensorsEnabled,
    DREAMCAST_JoystickUpdate,
    DREAMCAST_JoystickClose,
    DREAMCAST_JoystickQuit,
    DREAMCAST_JoystickGetGamepadMapping,
};

#endif /* SDL_JOYSTICK_DREAMCAST */
