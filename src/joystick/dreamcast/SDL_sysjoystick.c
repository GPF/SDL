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
#include "SDL_internal.h"

#ifdef SDL_JOYSTICK_DREAMCAST
#include <SDL3/SDL.h>
#include "../SDL_joystick_c.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"
#include "../SDL_gamepad_c.h"
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/purupuru.h>

#define MAX_JOYSTICKS 4
#define MAX_AXES 6
#define MAX_BUTTONS 8
#define MAX_HATS 2

static maple_device_t *SYS_Joystick_addr[MAX_JOYSTICKS];
static maple_device_t *SYS_Rumble_device[MAX_JOYSTICKS];
static SDL_JoystickID instance_ids[MAX_JOYSTICKS];
static SDL_JoystickID next_instance_id = 1; 

typedef struct joystick_hwdata {
    cont_state_t prev_state;
    bool opened;
    int device_index;
} jhwdata_t;

static const int sdl_buttons[] = {
    CONT_C, CONT_B, CONT_A, CONT_START, CONT_Z, CONT_Y, CONT_X, CONT_D
};

static int NormalizeAxis(int value)
{
    if (value < -128) value = -128;
    if (value > 127) value = 127;

    int result = (value * SDL_JOYSTICK_AXIS_MAX) / 127;

    // Clamp final result explicitly
    if (result < -32768) result = -32768;
    if (result > 32767) result = 32767;

    return result;
}


static int NormalizeTrigger(int value)
{
    // const int RAW_DEADZONE = 10;  // Raw Dreamcast trigger range is 0–255

    // Clamp raw value to valid range
    if (value < 0) value = 0;
    if (value > 255) value = 255;

    // Apply deadzone: treat lightly pressed triggers as fully unpressed
    // if (value <= RAW_DEADZONE) {
    //     return -SDL_JOYSTICK_AXIS_MAX;  // Fully released
    // }

    // Normalize to SDL range: -32767 (idle) to +32767 (fully pressed)
    int result = (value * 2 * SDL_JOYSTICK_AXIS_MAX / 255) - SDL_JOYSTICK_AXIS_MAX;

    // Clamp top end for symmetry
    // if (value == 255) result = SDL_JOYSTICK_AXIS_MAX;

    return result;
}


static const char *DREAMCAST_JoystickGetDeviceName(int index);
static SDL_GUID DREAMCAST_JoystickGetDeviceGUID(int device_index);
static uint8_t numdevs = 0;

static bool DREAMCAST_JoystickInit(void) {
    maple_device_t *dev;
    maple_device_t *rumble_dev;
    numdevs = 0;

    printf("Initializing Dreamcast Controllers...\n");

    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
        if (dev) {
            SYS_Joystick_addr[numdevs] = dev;
            rumble_dev = maple_enum_type(i, MAPLE_FUNC_PURUPURU);
            SYS_Rumble_device[numdevs] = rumble_dev;

            printf("%srumble device at port %d\n", rumble_dev ? "Found " : "No ", i);
            printf("Controller found at port %d (index %d)\n", i, numdevs);
            SDL_JoystickID instance_id = next_instance_id++;
            instance_ids[numdevs] = instance_id;

            // GET AND LOG GUID **BEFORE** SDL_PrivateJoystickAdded()
            // SDL_GUID full_guid = DREAMCAST_JoystickGetDeviceGUID(instance_id);
            // SDL_GUID stripped_guid = full_guid;
            // SDL_SetJoystickGUIDCRC(&stripped_guid, 0);

            // char full_guid_str[33];
            // char stripped_guid_str[33];
            // SDL_GUIDToString(full_guid, full_guid_str, sizeof(full_guid_str));
            // SDL_GUIDToString(stripped_guid, stripped_guid_str, sizeof(stripped_guid_str));

            // Uint16 crc;
            // SDL_GetJoystickGUIDInfo(full_guid, NULL, NULL, NULL, &crc);

            // SDL_Log("Dreamcast Full GUID: %s", full_guid_str);
            // SDL_Log("DB-Matchable GUID (CRC stripped): %s", stripped_guid_str);
            // SDL_Log("Example SDL_gamepad_db.h entry:");
            // SDL_Log("\"%s,Sega Dreamcast Controller,crc:%.4x,a:b2,b:b1,x:b6,y:b5,start:b3,"
            //         "dpup:h0.1,dpleft:h0.8,dpdown:h0.4,dpright:h0.2,dpup2:h1.1,dpleft2:h1.8,"
            //         "dpdown2:h1.4,dpright2:h1.2,righttrigger:a2~,lefttrigger:a3~,leftx:a0,"
            //         "lefty:a1,rightx:a4,righty:a5,\",",
            //         stripped_guid_str, crc);

            // Now register with SDL
            SDL_PrivateJoystickAdded(instance_id);
            numdevs++;
        } else {
            printf("No Controller at port %d\n", i);
        }
    }

    // if (numdevs == 0) {
    //     printf("No joysticks detected, defaulting to port 0\n");
    //     dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    //     if (dev) {
    //         numdevs = 1;
    //         SYS_Joystick_addr[0] = dev;
    //         SYS_Rumble_device[0] = maple_enum_type(0, MAPLE_FUNC_PURUPURU);
    //         instance_ids[numdevs] = next_instance_id; 
    //         SDL_PrivateJoystickAdded(0);

    //     }
    // }
    // SDL_AddGamepadMapping(
    //     "000085f15365676120447265616d6300,"
    //     "Sega Dreamcast Controller,"
    //     "a:b2,b:b1,x:b6,y:b5,start:b3,"
    //     "dpup:h0.1,dpleft:h0.8,dpdown:h0.4,dpright:h0.2,"
    //     "righttrigger:a2~,lefttrigger:a3~,leftx:a0,lefty:a1,rightx:a4,righty:a5"
    // );
    printf("Number of Controllers initialized: %d\n", numdevs);
    return numdevs > 0;
}

static int DREAMCAST_JoystickGetCount(void)
{
    return numdevs;
}

static void DREAMCAST_JoystickDetect(void)
{
    // static bool detected = false;
    // if (detected) return;  // only run once

    // maple_device_t *dev;
    // maple_device_t *rumble_dev;

    // for (int i = 0; i < MAX_JOYSTICKS; ++i) {
    //     dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
    //     if (dev && SYS_Joystick_addr[i] == NULL) {
    //         SYS_Joystick_addr[i] = dev;
    //         rumble_dev = maple_enum_type(i, MAPLE_FUNC_PURUPURU);
    //         SYS_Rumble_device[i] = rumble_dev;

    //         SDL_Log("Joystick detected on port %d", i);
    //         SDL_PrivateJoystickAdded(i);
    //         ++numdevs;
    //     }
    // }

    // detected = true;
}


static bool DREAMCAST_JoystickIsDevicePresent(Uint16 vendor_id, Uint16 product_id, Uint16 version, const char *name)
{
    return false;
}

static const char *DREAMCAST_JoystickGetDeviceName(int index)
{
    if (index >= 0 && index < 4) {
        return "Sega Dreamcast Controller";
    }

    SDL_SetError("No Controller available with that index");
    return NULL;
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

// static SDL_GUID DREAMCAST_JoystickGetDeviceGUID(int device_index)
// {
//     // the GUID is just the name for now
//     const char *name = DREAMCAST_JoystickGetDeviceName(device_index);
//     return SDL_CreateJoystickGUIDForName(name);
// }

static SDL_GUID DREAMCAST_JoystickGetDeviceGUID(int device_index)
{
    const Uint16 bus = 0x06;                      // Arbitrary, e.g., USB or platform-defined
    const Uint16 vendor = 0x1212;                 // Arbitrary stable Dreamcast vendor ID
    const Uint16 product = 0xDC01;                // Dreamcast controller ID
    const Uint16 version = 0x0001;                // Version
    const char *vendor_name = "Sega";
    const char *product_name = "Dreamcast Controller";
    const Uint8 driver_signature = 0x00;          // Optional driver-specific tag (0 is fine)
    const Uint8 driver_data = 0; // Index (or port) can go here if useful

    return SDL_CreateJoystickGUID(bus, vendor, product, version,
                                  vendor_name, product_name,
                                  driver_signature, driver_data);
}


static SDL_JoystickID DREAMCAST_JoystickGetDeviceInstanceID(int device_index)
{
    if (device_index < 0 || device_index >= MAX_JOYSTICKS) {
        return -1;
    }
    // SDL_Log("DREAMCAST_JoystickGetDeviceInstanceID:instance_ids[%d] = %d\n", device_index, instance_ids[device_index]);
    return instance_ids[device_index];
}

static bool DREAMCAST_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    // SDL_Log("DREAMCAST_JoystickOpen: device_index = %d", device_index);

    if (device_index < 0 || device_index >= numdevs) {
        SDL_SetError("Invalid joystick device index: %d", device_index);
        return false;
    }

    joystick->nbuttons = MAX_BUTTONS;
    joystick->naxes = MAX_AXES;
    joystick->nhats = MAX_HATS;

    jhwdata_t *hwdata = SDL_calloc(1, sizeof(jhwdata_t));
    if (!hwdata) {
        SDL_Log("Failed to allocate joystick hwdata");
        return false;
    }

    hwdata->device_index = device_index;
    hwdata->opened = true;
    joystick->hwdata = hwdata;

    // SDL_Log("DREAMCAST_JoystickOpen: joystick %d opened successfully", device_index);
    return true;
}

// Convert a rumble value (0–32767) to a 3‑bit intensity (0–7)
// Using rounding so that 32767 maps to 7.
static inline uint32_t convert_rumble_intensity(Uint16 value) {
    return (uint32_t)(((unsigned long)value * 7 + (32767 / 2)) / 32767);
}

static bool DREAMCAST_JoystickRumble(SDL_Joystick *joystick,
                                     Uint16 low_frequency_rumble,
                                     Uint16 high_frequency_rumble)
{
    maple_device_t *dev = SYS_Joystick_addr[joystick->instance_id];
    maple_device_t *rumble_dev = SYS_Rumble_device[joystick->instance_id];

    // SDL_Log("DREAMCAST_JoystickRumble: instance_id=%ld, low_frequency_rumble=%d, high_frequency_rumble=%d\n",
    //         (long)joystick->instance_id, low_frequency_rumble, high_frequency_rumble);

    /* Check if the device supports rumble */
    if (!dev || !rumble_dev) {
        // SDL_Log("Device or rumble device not found or not open. instance_id=%ld\n", (long)joystick->instance_id);
        return SDL_Unsupported();
    }

    SDL_Log("Sending rumble command to device %ld\n", (long)joystick->instance_id);

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
        // SDL_Log("Rumble command sent successfully to device %ld.\n", (long)joystick->instance_id);
        return true;
    } else {
        SDL_Log("Failed to send rumble command to device %ld. Error: %d\n", (long)joystick->instance_id, result);
        SDL_SetError("Failed to send rumble command");
        return false;
    }
}



static bool DREAMCAST_JoystickRumbleTriggers(SDL_Joystick *joystick,
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

static bool DREAMCAST_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static bool DREAMCAST_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static bool DREAMCAST_JoystickSetSensorsEnabled(SDL_Joystick *joystick, bool enabled)
{
    return SDL_Unsupported();
}

static void DREAMCAST_JoystickUpdate(SDL_Joystick *joystick)
{
    // SDL_Log("DREAMCAST_JoystickUpdate called %d\n", joystick->instance_id);
    jhwdata_t *hwdata = (jhwdata_t *)joystick->hwdata;
    maple_device_t *dev = SYS_Joystick_addr[hwdata->device_index];
    cont_state_t *state = (cont_state_t *)maple_dev_status(dev);
    if (!state) return;
    cont_state_t *prev_state = &hwdata->prev_state;

    Uint64 timestamp = SDL_GetTicksNS();
    int buttons = state->buttons;
    int changed = buttons ^ prev_state->buttons;

    // Process D-Pad (hat 0)
    if (changed & (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)) {
        Uint8 hat = SDL_HAT_CENTERED;
        if (buttons & CONT_DPAD_UP) hat |= SDL_HAT_UP;
        if (buttons & CONT_DPAD_DOWN) hat |= SDL_HAT_DOWN;
        if (buttons & CONT_DPAD_LEFT) hat |= SDL_HAT_LEFT;
        if (buttons & CONT_DPAD_RIGHT) hat |= SDL_HAT_RIGHT;
        SDL_SendJoystickHat(timestamp, joystick, 0, hat);
    }

    // Process second hat (hat 1) if present
    if (changed & (CONT_DPAD2_UP | CONT_DPAD2_DOWN | CONT_DPAD2_LEFT | CONT_DPAD2_RIGHT)) {
        Uint8 hat = SDL_HAT_CENTERED;
        if (buttons & CONT_DPAD2_UP) hat |= SDL_HAT_UP;
        if (buttons & CONT_DPAD2_DOWN) hat |= SDL_HAT_DOWN;
        if (buttons & CONT_DPAD2_LEFT) hat |= SDL_HAT_LEFT;
        if (buttons & CONT_DPAD2_RIGHT) hat |= SDL_HAT_RIGHT;
        SDL_SendJoystickHat(timestamp, joystick, 1, hat);
    }

    // Process buttons
    for (int i = 0; i < MAX_BUTTONS; ++i) {
        if (changed & sdl_buttons[i]) {
            SDL_SendJoystickButton(timestamp, joystick, i, (buttons & sdl_buttons[i]) ? true : false);
        }
    }

    // Axis updates
    if (state->joyy != prev_state->joyy)
        SDL_SendJoystickAxis(timestamp, joystick, 1, NormalizeAxis(state->joyy));
    if (state->joyx != prev_state->joyx)
        SDL_SendJoystickAxis(timestamp, joystick, 0, NormalizeAxis(state->joyx));

    if (state->joy2x != prev_state->joy2x)
        SDL_SendJoystickAxis(timestamp, joystick, 4, NormalizeAxis(state->joy2x));
    if (state->joy2y != prev_state->joy2y)
        SDL_SendJoystickAxis(timestamp, joystick, 5, NormalizeAxis(state->joy2y));

    // Triggers
    int rtrig = NormalizeTrigger(state->rtrig);
    int ltrig = NormalizeTrigger(state->ltrig);
    int prev_rtrig = NormalizeTrigger(prev_state->rtrig);
    int prev_ltrig = NormalizeTrigger(prev_state->ltrig);

    if (rtrig != prev_rtrig)
        SDL_SendJoystickAxis(timestamp, joystick, 2, rtrig);
    if (ltrig != prev_ltrig)
        SDL_SendJoystickAxis(timestamp, joystick, 3, ltrig);


    // SDL_Log("Prev LTRIG: %d  Prev RTRIG: %d  (Norm: %d / %d)",
    //         NormalizeTrigger(prev_state->ltrig),
    //         NormalizeTrigger(prev_state->rtrig),
    //         ltrig, rtrig);

    hwdata->prev_state = *state;
}


static void DREAMCAST_JoystickClose(SDL_Joystick *joystick)
{
    jhwdata_t *hwdata = (jhwdata_t *)joystick->hwdata;
    if (hwdata && hwdata->opened) {
        hwdata->opened = false;
        joystick->hwdata = NULL;
    }
}

static void DREAMCAST_JoystickQuit(void)
{
    // Cleanup if needed
}

static bool DREAMCAST_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    SDL_Log("DREAMCAST_JoystickGetGamepadMapping(%d) called", device_index);
    *out = (SDL_GamepadMapping){
        .a = { EMappingKind_Button, 2 },
        .b = { EMappingKind_Button, 1 },
        .x = { EMappingKind_Button, 6 },
        .y = { EMappingKind_Button, 5 },
        .back = { EMappingKind_None, 255 }, // No 'back' button on Dreamcast
        .guide = { EMappingKind_None, 255 }, // No 'guide' button
        .start = { EMappingKind_Button, 3 },
        .leftstick = { EMappingKind_None, 255 }, // No stick press
        .rightstick = { EMappingKind_None, 255 }, // No stick press
        .leftshoulder = { EMappingKind_None, 255 }, // Dreamcast has triggers, not shoulders
        .rightshoulder = { EMappingKind_None, 255 },
        .dpup = { EMappingKind_Hat, 0, 1 },    // Hat0, position "up" (value 1)
        .dpdown = { EMappingKind_Hat, 0, 4 },  // Hat0, position "down" (value 4)
        .dpleft = { EMappingKind_Hat, 0, 8 },  // Hat0, position "left" (value 8)
        .dpright = { EMappingKind_Hat, 0, 2 }, // Hat0, position "right" (value 2)
        .misc1 = { EMappingKind_None, 255 },   // No extra buttons
        .leftx = { EMappingKind_Axis, 0 },     // Left stick X (a0)
        .lefty = { EMappingKind_Axis, 1 },     // Left stick Y (a1)
        .rightx = { EMappingKind_Axis, 4 },    // Right stick X (a4)
        .righty = { EMappingKind_Axis, 5 },    // Right stick Y (a5)
        .lefttrigger = { EMappingKind_Axis, 3  }, // Left trigger (a3~, now not inverted)
        .righttrigger = { EMappingKind_Axis, 2  }, // Right trigger (a2~, now not inverted)
    };
    return true;
    // return false;
}

SDL_JoystickDriver SDL_DREAMCAST_JoystickDriver = {
    DREAMCAST_JoystickInit,
    DREAMCAST_JoystickGetCount,
    DREAMCAST_JoystickDetect,
    DREAMCAST_JoystickIsDevicePresent,
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
    DREAMCAST_JoystickSetLED,
    DREAMCAST_JoystickSendEffect,
    DREAMCAST_JoystickSetSensorsEnabled,
    DREAMCAST_JoystickUpdate,
    DREAMCAST_JoystickClose,
    DREAMCAST_JoystickQuit,
    DREAMCAST_JoystickGetGamepadMapping,
};

#endif /* SDL_JOYSTICK_DREAMCAST */
