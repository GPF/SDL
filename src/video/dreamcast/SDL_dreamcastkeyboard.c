
/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

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

#ifdef SDL_VIDEO_DRIVER_DREAMCAST


#include "../../events/SDL_events_c.h"
#include "../../events/SDL_keyboard_c.h"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_mouse.h>
#include <kos.h>
#include "SDL_dreamcastvideo.h"


const static unsigned short sdl_key[] = {
    /*0*/    0, 0, 0, 0, SDL_SCANCODE_A, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E, SDL_SCANCODE_F, 
             SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_I,
             SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O, 
             SDL_SCANCODE_P, SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
             SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X, SDL_SCANCODE_Y, SDL_SCANCODE_Z,
    /*1e*/   SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6, 
             SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0,
    /*28*/   SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB, SDL_SCANCODE_SPACE, 
             SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, SDL_SCANCODE_LEFTBRACKET, 
             SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_BACKSLASH, 0, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE,
    /*35*/   SDL_SCANCODE_GRAVE, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_CAPSLOCK, 
             SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_F6, 
             SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12,
    /*46*/   SDL_SCANCODE_PRINTSCREEN, SDL_SCANCODE_SCROLLLOCK, SDL_SCANCODE_PAUSE, SDL_SCANCODE_INSERT, 
             SDL_SCANCODE_HOME, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_DELETE, SDL_SCANCODE_END, SDL_SCANCODE_PAGEDOWN, 
             SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_UP,
    /*53*/   SDL_SCANCODE_NUMLOCKCLEAR, SDL_SCANCODE_KP_DIVIDE, SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_KP_MINUS, 
             SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_ENTER, 
             SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6,
    /*5f*/   SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_APPLICATION /* S3 */
};

// const static unsigned short sdl_shift[] = {s
//     SDL_SCANCODE_LCTRL, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT, SDL_SCANCODE_MODE /* S1 */,
//     SDL_SCANCODE_RCTRL, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RALT, SDL_SCANCODE_MENU /* S2 */,
// };

void DREAMCAST_InitKeyboard(void)
{
    SDL_AddKeyboard(SDL_DEFAULT_KEYBOARD_ID, NULL, true);
}


// void DREAMCAST_PollKeyboard(void *unused) {
//     maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
//     if (!dev) return;

//     kbd_state_t *state = maple_dev_status(dev);
//     if (!state) return;

//     SDL_KeyboardID keyboard_id = SDL_DEFAULT_KEYBOARD_ID;
//     if (keyboard_id == 0) return;

//     int ch;
//     while ((ch = kbd_queue_pop(dev, 0)) != KBD_QUEUE_END) {
//         kbd_key_t raw_key = ch & 0xFF;
//         kbd_mods_t mods = { .raw = (ch >> 8) & 0xFF };
//         kbd_leds_t leds = { .raw = (ch >> 16) & 0xFF };

//         // Look up SDL scancode
//         int scancode = (raw_key < SDL_arraysize(sdl_key)) ? sdl_key[raw_key] : 0;
//         if (!scancode) continue;

//         // Explicitly handle modifier key states
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_LCTRL, mods.lctrl);
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_LSHIFT, mods.lshift);
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_LALT, mods.lalt);
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_RCTRL, mods.rctrl);
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_RSHIFT, mods.rshift);
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_RALT, mods.ralt);

//         // Optionally map S1/S2
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_MODE, mods.s1);  // S1 → MODE
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_MENU, mods.s2);  // S2 → MENU

//         // Send primary key press/release
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, scancode, true);
//         SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, scancode, false);

//         // ASCII/Text input
//         char ascii = kbd_key_to_ascii(raw_key, state->region, mods, leds);
//         if (ascii) {
//             SDL_Window *win = SDL_GetKeyboardFocus();
//             if (!win) {
//                 int count = 0;
//                 SDL_Window **windows = SDL_GetWindows(&count);
//                 if (windows && count > 0) {
//                     win = windows[0];
//                     SDL_free(windows);
//                     if (win) SDL_StartTextInput(win);
//                 }
//             }

//             if (win && SDL_TextInputActive(win)) {
//                 char text[2] = { ascii, '\0' };
//                 SDL_SendKeyboardText(text);
//             }
//         }
//     }
// }

void DREAMCAST_PollKeyboard(void *unused) {
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
    if (!dev) return;

    kbd_state_t *state = maple_dev_status(dev);
    if (!state) return;

    SDL_KeyboardID keyboard_id = SDL_DEFAULT_KEYBOARD_ID;
    if (keyboard_id == 0) return;

    // Handle modifier key states using current state
    kbd_mods_t mods = state->last_modifiers;
    SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_LCTRL, mods.lctrl);
    SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_LSHIFT, mods.lshift);
    SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_LALT, mods.lalt);
    SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_RCTRL, mods.rctrl);
    SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_RSHIFT, mods.rshift);
    SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_RALT, mods.ralt);
    SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_MODE, mods.s1);
    SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, 0, SDL_SCANCODE_MENU, mods.s2);

    // Handle ALL key state transitions using state tracking
    for (int i = 0; i < SDL_arraysize(sdl_key); ++i) {
        if (!sdl_key[i]) continue;

        bool was_down = state->key_states[i].was_down;
        bool is_down = state->key_states[i].is_down;

        // Send current state every poll - SDL will handle state change detection
        SDL_SendKeyboardKey(SDL_GetTicksNS(), keyboard_id, mods.raw, sdl_key[i], is_down);
    }

    // Handle text input from queue (for printable characters only)
    int ch;
    while ((ch = kbd_queue_pop(dev, 0)) != KBD_QUEUE_END) {
        kbd_key_t raw_key = ch & 0xFF;
        kbd_mods_t queue_mods = { .raw = (ch >> 8) & 0xFF };
        kbd_leds_t leds = { .raw = (ch >> 16) & 0xFF };

        // Generate ASCII text input for printable characters only
        char ascii = kbd_key_to_ascii(raw_key, state->region, queue_mods, leds);
        if (ascii) {
            SDL_Window *win = SDL_GetKeyboardFocus();
            if (!win) {
                int count = 0;
                SDL_Window **windows = SDL_GetWindows(&count);
                if (windows && count > 0) {
                    win = windows[0];
                    SDL_free(windows);
                    if (win) SDL_StartTextInput(win);
                }
            }

            if (win && SDL_TextInputActive(win)) {
                char text[2] = { ascii, '\0' };
                SDL_SendKeyboardText(text);
            }
        }
    }
}

// static void DREAMCAST_QuitKeyboard(void *unused)
// {
//     // Clean up if needed (e.g., release device memory or reset state)
// }

#endif /* SDL_VIDEO_DRIVER_DREAMCAST */

