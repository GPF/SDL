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

#include "../SDL_sysvideo.h"

#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "SDL_hints.h"
#include "SDL_dreamcastvideo.h"
#include "SDL_dreamcastwindow.h"

int DREAMCAST_CreateWindow(_THIS, SDL_Window * window)
{
    SDL_VideoData *driverdata = (SDL_VideoData *) _this->driverdata;

    if (!driverdata) {
        return SDL_SetError("DREAMCAST_CreateWindow: driverdata is NULL");
    }

    if (driverdata->window) {
        return SDL_SetError("Dreamcast only supports one window");
    }

    driverdata->window = window;

    // Set window size based on driverdata resolution
    window->x = 0;
    window->y = 0;
    window->w = driverdata->w;
    window->h = driverdata->h;

    // Modify window flags for Dreamcast
    window->flags &= ~SDL_WINDOW_RESIZABLE;     // Not resizable
    window->flags &= ~SDL_WINDOW_HIDDEN;        // Ensure visible
    window->flags |= SDL_WINDOW_SHOWN;          // One always-visible window
    window->flags |= SDL_WINDOW_INPUT_FOCUS;    // Always has input focus

    // Only set OpenGL flag when explicitly requested via hint
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    int use_opengl = (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_OPENGL_VIDEO") == 0);
    if (use_opengl) {
        window->flags |= SDL_WINDOW_OPENGL;
        SDL_Log("DREAMCAST_CreateWindow: window %dx%d created with OpenGL flag", window->w, window->h);
    } else {
        window->flags &= ~SDL_WINDOW_OPENGL;
        SDL_Log("DREAMCAST_CreateWindow: window %dx%d created with framebuffer", window->w, window->h);
    }

    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);

    return 0;
}


void DREAMCAST_SetWindowTitle(_THIS, SDL_Window * window)
{
    /* TODO */
}

void DREAMCAST_DestroyWindow(_THIS, SDL_Window * window)
{
    SDL_VideoData *driverdata = (SDL_VideoData *) _this->driverdata;
    if (window == driverdata->window) {
        driverdata->window = NULL;
    }
}

#endif /* SDL_VIDEO_DRIVER_DREAMCAST */

/* vi: set ts=4 sw=4 expandtab: */
