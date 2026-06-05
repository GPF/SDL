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
#include <kos.h>
#include <dc/video.h>
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_windowevents_c.h"
#include "SDL_dreamcastvideo.h"
#include "SDL_dreamcastwindow.h"

#include <kos/dbglog.h>
#define DCWIN_PROBE(...) dbglog(DBG_INFO, "[dcwin_probe] " __VA_ARGS__)

extern int __sdl_dc_is_60hz;

bool DREAMCAST_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props) {
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    SDL_VideoDisplay *display = _this->displays[0];

    DCWIN_PROBE("CreateWindow enter window=0x%08lx internal=0x%08lx flags=0x%08lx size=%dx%d pending=%dx%d\n",
                (unsigned long)window,
                (unsigned long)window->internal,
                (unsigned long)window->flags,
                window->w, window->h,
                window->pending.w, window->pending.h);
    SDL_Log("DREAMCAST_CreateWindow: Client requested window at %d,%d with size %dx%d",
            window->x, window->y, window->w, window->h);

    // Default to desktop size if window size invalid
    int target_w = (window->w > 0) ? window->w : display->desktop_mode.w;
    int target_h = (window->h > 0) ? window->h : display->desktop_mode.h;

    // Override size for textured or DMA video
    if (video_mode_hint && 
       (SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0 || SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0)) {
        target_w = 512;
        target_h = 256;
        SDL_Log("DREAMCAST_CreateWindow: Forcing resolution to 640x480 for %s", video_mode_hint);
    }

    window->w = target_w;
    window->h = target_h;

    DCWIN_PROBE("CreateWindow before SDL_SetWindowSize target=%dx%d\n", target_w, target_h);
    if (SDL_SetWindowSize(window, target_w, target_h) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed to set window size: %s", SDL_GetError());
        return false;
    }
    DCWIN_PROBE("CreateWindow after SDL_SetWindowSize flags=0x%08lx size=%dx%d pending=%dx%d\n",
                (unsigned long)window->flags, window->w, window->h, window->pending.w, window->pending.h);

    // Determine display + pixel mode
    int disp_mode = -1;
    int pixel_mode = PM_RGB555;
    if (video_mode_hint) {
        if (SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
            disp_mode = __sdl_dc_is_60hz ? DM_640x480 : DM_640x480_PAL_IL;
            pixel_mode = PM_RGB565;
        } else if (SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
            disp_mode = __sdl_dc_is_60hz ? DM_640x480 : DM_640x480_PAL_IL;
            pixel_mode = PM_RGB888;
        } else if (SDL_strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0) {
            pixel_mode = PM_RGB555;
            if (__sdl_dc_is_60hz) {
                if (target_w == 320 && target_h == 240) disp_mode = DM_320x240;
                else if (target_w == 640 && target_h == 480) disp_mode = DM_640x480;
                else if (target_w == 768 && target_h == 480) disp_mode = DM_768x480;
            } else {
                if (target_w == 320 && target_h == 240) disp_mode = DM_320x240_PAL;
                else if (target_w == 640 && target_h == 480) disp_mode = DM_640x480_PAL_IL;
                else if (target_w == 768 && target_h == 576) disp_mode = DM_768x576_PAL_IL;
            }
        }
    }
#ifdef SDL_VIDEO_OPENGL
    // For OpenGL, enforce 640x480 hardware resolution
    disp_mode = __sdl_dc_is_60hz ? DM_640x480 : DM_640x480_PAL_IL;
    pixel_mode = PM_RGB555; // OpenGL converts to ARGB1555
    SDL_Log("OpenGL mode: Setting hardware resolution to 640x480");
    DCWIN_PROBE("CreateWindow vid_set_mode pre-opengl disp=%d pixel=%d\n", disp_mode, pixel_mode);
    vid_set_mode(disp_mode, pixel_mode);
#endif
    if (disp_mode < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Unsupported display mode for %dx%d", target_w, target_h);
        return false;
    }

    vid_set_mode(disp_mode, pixel_mode);
    DCWIN_PROBE("CreateWindow vid_set_mode final disp=%d pixel=%d\n", disp_mode, pixel_mode);

    // Add valid fullscreen display modes
    SDL_ResetFullscreenDisplayModes(display);
    SDL_DisplayMode mode;
    SDL_zero(mode);
    int refresh_rate = __sdl_dc_is_60hz ? 60 : 50;

    if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
        const int textured_modes[][2] = {
            {320, 240}, {512, 256}, {640, 480}, {1024, 512}
        };
        for (int i = 0; i < sizeof(textured_modes) / sizeof(textured_modes[0]); ++i) {
            SDL_zero(mode);
            mode.w = textured_modes[i][0];
            mode.h = textured_modes[i][1];
            mode.format = SDL_PIXELFORMAT_RGB565;
            mode.refresh_rate = refresh_rate;
            mode.pixel_density = 1.0f;
            SDL_AddFullscreenDisplayMode(display, &mode);
            SDL_Log("Added TEXTURED mode: %dx%d", mode.w, mode.h);
        }
    } else if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
        SDL_zero(mode);
        mode.w = 640;
        mode.h = 480;
        mode.format = SDL_PIXELFORMAT_XRGB8888;
        mode.refresh_rate = refresh_rate;
        mode.pixel_density = 1.0f;
        SDL_AddFullscreenDisplayMode(display, &mode);
        SDL_Log("Added TEXTURED mode: %dx%d", mode.w, mode.h);
    } else if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0) {
        const int direct_modes[][2] = {
            {320, 240}, {640, 480}, {768, 480}
        };
        for (int i = 0; i < sizeof(direct_modes) / sizeof(direct_modes[0]); ++i) {
            SDL_zero(mode);
            mode.w = direct_modes[i][0];
            mode.h = direct_modes[i][1];
            mode.format = SDL_PIXELFORMAT_ARGB1555;
            mode.refresh_rate = refresh_rate;
            mode.pixel_density = 1.0f;
            SDL_AddFullscreenDisplayMode(display, &mode);
            SDL_Log("Added TEXTURED mode: %dx%d", mode.w, mode.h);
        }
    }

    // Allocate window internals
    SDL_WindowData *dreamcast_window = SDL_calloc(1, sizeof(SDL_WindowData));
    if (!dreamcast_window) {
        return SDL_OutOfMemory();
    }
    window->internal = dreamcast_window;
    dreamcast_window->sdl_window = window;

    if (window->x == SDL_WINDOWPOS_UNDEFINED) window->x = 0;
    if (window->y == SDL_WINDOWPOS_UNDEFINED) window->y = 0;

    // window->flags &= ~SDL_WINDOW_RESIZABLE;
    // window->flags &= ~SDL_WINDOW_HIDDEN;
    window->flags |= SDL_WINDOW_INPUT_FOCUS;

    // Set fullscreen mode
    SDL_zero(mode);
    mode.w = target_w;
    mode.h = target_h;
    mode.format = (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) ? SDL_PIXELFORMAT_RGB565 :
                  (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) ? SDL_PIXELFORMAT_XRGB8888 :
                  SDL_PIXELFORMAT_ARGB1555;
    mode.refresh_rate = refresh_rate;
    mode.pixel_density = 1.0f;

    if (SDL_SetWindowFullscreenMode(window, &mode) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed to set fullscreen mode: %s", SDL_GetError());
        SDL_free(dreamcast_window);
        return false;
    }

    // Ensure window matches display
    int pixel_w, pixel_h;
    SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);
    if (pixel_w != window->w || pixel_h != window->h) {
        SDL_Log("DREAMCAST_CreateWindow: Correcting window pixel size to %dx%d", window->w, window->h);
        if (SDL_SetWindowSize(window, window->w, window->h) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed to set corrected size: %s", SDL_GetError());
            SDL_free(dreamcast_window);
            return false;
        }
    }

    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->w, window->h);
    SDL_SetKeyboardFocus(window);
    SDL_SetMouseFocus(window);
    DCWIN_PROBE("CreateWindow return true window=0x%08lx internal=0x%08lx flags=0x%08lx size=%dx%d\n",
                (unsigned long)window,
                (unsigned long)window->internal,
                (unsigned long)window->flags,
                window->w, window->h);
    return true;
}


void DREAMCAST_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    DCWIN_PROBE("SetWindowSize window=0x%08lx flags=0x%08lx size=%dx%d pending=%dx%d\n",
                (unsigned long)window,
                (unsigned long)window->flags,
                window->w, window->h,
                window->pending.w, window->pending.h);
    SDL_Log("DREAMCAST_SetWindowSize: applying resize to %dx%d", window->pending.w, window->pending.h);

    // Send a resize event so SDL3 updates internal pixel size state
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->pending.w, window->pending.h);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, window->pending.w, window->pending.h);
    // If scaling logic is needed later, you could apply it here
}



void DREAMCAST_DestroyWindow(SDL_VideoDevice *_this, SDL_Window * window)
{
    SDL_WindowData *dreamcast_window = window->internal;
    if (dreamcast_window) {
        SDL_free(dreamcast_window);
    }

    window->internal = NULL;
}

#endif /* SDL_VIDEO_DRIVER_DREAMCAST */

/* vi: set ts=4 sw=4 expandtab: */
