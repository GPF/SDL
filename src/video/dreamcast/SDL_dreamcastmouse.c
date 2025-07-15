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

#ifdef SDL_VIDEO_DRIVER_DREAMCAST

#include "../../events/SDL_events_c.h"
#include "../../events/SDL_keyboard_c.h"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_mouse.h>
#include <kos.h>
#include "SDL_dreamcastvideo.h"

extern unsigned int __sdl_dc_mouse_shift;

const static char sdl_mousebtn[] = {
    0,                     // No button maps to SDL_BUTTON_WHEELUP
    SDL_BUTTON_RIGHT ,     // Right button maps to SDL_BUTTON_RIGHT
    SDL_BUTTON_LEFT  ,     // Left button maps to SDL_BUTTON_LEFT
    SDL_BUTTON_MIDDLE      // Side button maps to SDL_BUTTON_MIDDLE
};

static bool DREAMCAST_SetRelativeMouseMode(bool enabled);
void DREAMCAST_PollMouse();
// SDL_Mouse *dreamcast_mouse = NULL;

void DREAMCAST_InitMouse(void)
{
    SDL_AddMouse(SDL_DEFAULT_MOUSE_ID, NULL, true);  // Must call this first
    SDL_Mouse *mouse = SDL_GetMouse();

    // mouse->Poll = DREAMCAST_PollMouse;  // Your update logic
    mouse->SetRelativeMouseMode = DREAMCAST_SetRelativeMouseMode;

    // Optional cursor handlers (or set to NULL)
    mouse->CreateCursor = NULL;
    mouse->ShowCursor = NULL;
    mouse->FreeCursor = NULL;
    mouse->CreateSystemCursor = NULL;

    SDL_SetDefaultCursor(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT));
}
static bool is_relative_mode = false;

static bool DREAMCAST_SetRelativeMouseMode(bool enabled)
{
     is_relative_mode = enabled;
     return is_relative_mode;

}

void DREAMCAST_PollMouse()
{
    static int abs_x = 0, abs_y = 0;
    static Uint8 prev_buttons = 0;

    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_MOUSE);
    if (!dev) return;

    mouse_state_t *state = maple_dev_status(dev);
    if (!state) return;

    static float relative_sensitivity_x = 8.0f;  // Start with 8x, adjust as needed
    static float relative_sensitivity_y = 8.0f;
    static float accumulated_dx = 0.0f, accumulated_dy = 0.0f;

    if (is_relative_mode) {
        // Apply sensitivity scaling
        accumulated_dx += state->dx * relative_sensitivity_x;
        accumulated_dy += state->dy * relative_sensitivity_y;
        
        int final_dx = (int)accumulated_dx;
        int final_dy = (int)accumulated_dy;
        
        if (final_dx != 0 || final_dy != 0) {
            SDL_SendMouseMotion(SDL_GetTicksNS(), NULL, SDL_DEFAULT_MOUSE_ID, true, final_dx, final_dy);
            accumulated_dx -= final_dx;
            accumulated_dy -= final_dy;
        }
    } else {
        // Scale to screen size for absolute mode
        float mouse_scale_x = 640.0f / 320.0f;
        float mouse_scale_y = 480.0f / 240.0f;

        int scaled_dx = (int)(state->dx * mouse_scale_x);
        int scaled_dy = (int)(state->dy * mouse_scale_y);

        abs_x = SDL_clamp(abs_x + scaled_dx, 0, 639);
        abs_y = SDL_clamp(abs_y + scaled_dy, 0, 479);

        SDL_SendMouseMotion(SDL_GetTicksNS(), NULL, SDL_DEFAULT_MOUSE_ID, false, abs_x, abs_y);
    }

    // Mouse buttons
    Uint8 changed_buttons = state->buttons ^ prev_buttons;
    for (int i = 0; i < SDL_arraysize(sdl_mousebtn); ++i) {
        if (changed_buttons & (1 << i)) {
            SDL_SendMouseButton(SDL_GetTicksNS(), NULL, SDL_DEFAULT_MOUSE_ID, sdl_mousebtn[i], (state->buttons & (1 << i)) != 0);
        }
    }

    // Mouse wheel
    if (state->dz != 0) {
        SDL_SendMouseWheel(SDL_GetTicksNS(), NULL, SDL_DEFAULT_MOUSE_ID, 0, (state->dz < 0) ? 1 : -1, SDL_MOUSEWHEEL_NORMAL);
    }

    prev_buttons = state->buttons;
}

// static void DREAMCAST_QuitMouse(SDL_Mouse *mouse) {
//     dreamcast_mouse = NULL;
// }

#endif /* SDL_VIDEO_DRIVER_DREAMCAST */

