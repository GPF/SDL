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

static bool DREAMCAST_IsOpenGLVideoMode(const char *video_mode_hint)
{
    return video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_OPENGL_VIDEO") == 0;
}

static bool DREAMCAST_IsTexturedVideoMode(const char *video_mode_hint)
{
    return video_mode_hint &&
        (SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0 ||
         SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_STRIDED_VIDEO") == 0);
}

bool DREAMCAST_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props) {
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    SDL_VideoDisplay *display = _this->displays[0];

    bool direct_video =
        !video_mode_hint ||
        SDL_strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0;
    bool opengl_video = DREAMCAST_IsOpenGLVideoMode(video_mode_hint);

    DCWIN_PROBE("CreateWindow enter window=0x%08lx internal=0x%08lx flags=0x%08lx size=%dx%d pending=%dx%d\n",
                (unsigned long)window,
                (unsigned long)window->internal,
                (unsigned long)window->flags,
                window->w, window->h,
                window->pending.w, window->pending.h);

    SDL_Log("DREAMCAST_CreateWindow: Client requested window at %d,%d with size %dx%d",
            window->x, window->y, window->w, window->h);

    int requested_w = window->w;
    int requested_h = window->h;

    int target_w = display->desktop_mode.w > 0 ? display->desktop_mode.w : 640;
    int target_h = display->desktop_mode.h > 0 ? display->desktop_mode.h : 480;

    if (opengl_video) {
        target_w = 640;
        target_h = 480;
        SDL_Log("DREAMCAST_CreateWindow: Forcing 640x480 for SDL_DC_OPENGL_VIDEO");
    } else if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
        // DMA video always renders at full 640x480
        target_w = 640;
        target_h = 480;
        SDL_Log("DREAMCAST_CreateWindow: Forcing native resolution %dx%d for SDL_DC_DMA_VIDEO",
                target_w, target_h);
    } else if (DREAMCAST_IsTexturedVideoMode(video_mode_hint)) {
        // Textured video keeps the requested logical size; hardware always outputs 640x480
        // via the PVR scaler. Keep requested_w/h as target.
        target_w = requested_w;
        target_h = requested_h;
        SDL_Log("DREAMCAST_CreateWindow: Keeping logical resolution %dx%d for %s",
                target_w, target_h, video_mode_hint);
    } else if (direct_video) {
        if (__sdl_dc_is_60hz) {
            if (requested_w == 320 && requested_h == 240) {
                target_w = 320;
                target_h = 240;
            } else if (requested_w == 768 && requested_h == 480) {
                target_w = 768;
                target_h = 480;
            } else {
                target_w = 640;
                target_h = 480;
            }
        } else {
            if (requested_w == 320 && requested_h == 240) {
                target_w = 320;
                target_h = 240;
            } else if (requested_w == 768 && requested_h == 576) {
                target_w = 768;
                target_h = 576;
            } else {
                target_w = 640;
                target_h = 480;
            }
        }
    }

    window->w = target_w;
    window->h = target_h;
    window->pending.w = target_w;
    window->pending.h = target_h;

    DCWIN_PROBE("CreateWindow using target=%dx%d requested=%dx%d\n",
                target_w, target_h, requested_w, requested_h);

    int disp_mode = -1;
    int pixel_mode = PM_RGB555;

    if (opengl_video) {
        disp_mode = __sdl_dc_is_60hz ? DM_640x480 : DM_640x480_PAL_IL;
        pixel_mode = PM_RGB555;
    } else if (DREAMCAST_IsTexturedVideoMode(video_mode_hint)) {
        disp_mode = __sdl_dc_is_60hz ? DM_640x480 : DM_640x480_PAL_IL;
        pixel_mode = PM_RGB565;
    } else if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
        disp_mode = __sdl_dc_is_60hz ? DM_640x480 : DM_640x480_PAL_IL;
        pixel_mode = PM_RGB888;
    } else if (direct_video) {
        pixel_mode = PM_RGB555;

        if (__sdl_dc_is_60hz) {
            if (target_w == 320 && target_h == 240) {
                disp_mode = DM_320x240;
            } else if (target_w == 640 && target_h == 480) {
                disp_mode = DM_640x480;
            } else if (target_w == 768 && target_h == 480) {
                disp_mode = DM_768x480;
            }
        } else {
            if (target_w == 320 && target_h == 240) {
                disp_mode = DM_320x240_PAL;
            } else if (target_w == 640 && target_h == 480) {
                disp_mode = DM_640x480_PAL_IL;
            } else if (target_w == 768 && target_h == 576) {
                disp_mode = DM_768x576_PAL_IL;
            }
        }
    }

    if (opengl_video || (window->flags & SDL_WINDOW_OPENGL)) {
        disp_mode = __sdl_dc_is_60hz ? DM_640x480 : DM_640x480_PAL_IL;
        pixel_mode = PM_RGB555;
        target_w = 640;
        target_h = 480;
        window->w = target_w;
        window->h = target_h;
        window->pending.w = target_w;
        window->pending.h = target_h;
        SDL_Log("OpenGL mode: Setting hardware resolution to 640x480");
    }

    if (disp_mode < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Unsupported display mode for %dx%d", target_w, target_h);
        return false;
    }

    vid_set_mode(disp_mode, pixel_mode);
    DCWIN_PROBE("CreateWindow vid_set_mode final disp=%d pixel=%d\n", disp_mode, pixel_mode);

    SDL_ResetFullscreenDisplayModes(display);

    SDL_DisplayMode mode;
    SDL_zero(mode);

    int refresh_rate = __sdl_dc_is_60hz ? 60 : 50;

    if (opengl_video) {
        SDL_zero(mode);
        mode.w = 640;
        mode.h = 480;
        mode.format = SDL_PIXELFORMAT_ARGB1555;
        mode.refresh_rate = refresh_rate;
        mode.pixel_density = 1.0f;
    } else if (DREAMCAST_IsTexturedVideoMode(video_mode_hint)) {
        const int textured_modes[][2] = {
            {320, 240}, {512, 256}, {640, 480}, {1024, 512}
        };

        for (int i = 0; i < SDL_arraysize(textured_modes); ++i) {
            SDL_zero(mode);
            mode.w = textured_modes[i][0];
            mode.h = textured_modes[i][1];
            mode.format = SDL_PIXELFORMAT_RGB565;
            mode.refresh_rate = refresh_rate;
            mode.pixel_density = 1.0f;
            SDL_AddFullscreenDisplayMode(display, &mode);
        }
    } else if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
        SDL_zero(mode);
        mode.w = 640;
        mode.h = 480;
        mode.format = SDL_PIXELFORMAT_XRGB8888;
        mode.refresh_rate = refresh_rate;
        mode.pixel_density = 1.0f;
        SDL_AddFullscreenDisplayMode(display, &mode);
    } else if (direct_video) {
        const int direct_modes[][2] = {
            {320, 240}, {640, 480}, {768, 480}
        };

        for (int i = 0; i < SDL_arraysize(direct_modes); ++i) {
            SDL_zero(mode);
            mode.w = direct_modes[i][0];
            mode.h = direct_modes[i][1];
            mode.format = SDL_PIXELFORMAT_ARGB1555;
            mode.refresh_rate = refresh_rate;
            mode.pixel_density = 1.0f;
            SDL_AddFullscreenDisplayMode(display, &mode);
        }
    }

    SDL_WindowData *dreamcast_window = SDL_calloc(1, sizeof(SDL_WindowData));
    if (!dreamcast_window) {
        return SDL_OutOfMemory();
    }

    window->internal = dreamcast_window;
    dreamcast_window->sdl_window = window;

    if (window->x == SDL_WINDOWPOS_UNDEFINED) {
        window->x = 0;
    }
    if (window->y == SDL_WINDOWPOS_UNDEFINED) {
        window->y = 0;
    }

    window->flags |= SDL_WINDOW_INPUT_FOCUS;

    SDL_zero(mode);
    // For textured video, fullscreen mode reports logical size; hardware scaler handles upscale
    mode.w = target_w;
    mode.h = target_h;
    mode.format =
        (DREAMCAST_IsTexturedVideoMode(video_mode_hint)) ? SDL_PIXELFORMAT_RGB565 :
        (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) ? SDL_PIXELFORMAT_XRGB8888 :
        SDL_PIXELFORMAT_ARGB1555;
    mode.refresh_rate = refresh_rate;
    mode.pixel_density = 1.0f;

    if (SDL_SetWindowFullscreenMode(window, &mode) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed to set fullscreen mode: %s", SDL_GetError());
        SDL_free(dreamcast_window);
        window->internal = NULL;
        return false;
    }

    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, target_w, target_h);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, target_w, target_h);
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
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    SDL_VideoDisplay *display = _this->displays[0];

    bool direct_video =
        !video_mode_hint ||
        SDL_strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0;
    bool opengl_video = DREAMCAST_IsOpenGLVideoMode(video_mode_hint);

    int requested_w = window->pending.w;
    int requested_h = window->pending.h;

    int target_w = display->desktop_mode.w > 0 ? display->desktop_mode.w : 640;
    int target_h = display->desktop_mode.h > 0 ? display->desktop_mode.h : 480;

    if (opengl_video) {
        target_w = 640;
        target_h = 480;
    } else if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
        target_w = 640;
        target_h = 480;
    } else if (DREAMCAST_IsTexturedVideoMode(video_mode_hint)) {
        // Keep logical size for textured video; hardware scaler handles upscale to 640x480
        target_w = requested_w;
        target_h = requested_h;
    } else if (direct_video) {
        if (__sdl_dc_is_60hz) {
            if (requested_w == 320 && requested_h == 240) {
                target_w = 320;
                target_h = 240;
            } else if (requested_w == 768 && requested_h == 480) {
                target_w = 768;
                target_h = 480;
            } else {
                target_w = 640;
                target_h = 480;
            }
        } else {
            if (requested_w == 320 && requested_h == 240) {
                target_w = 320;
                target_h = 240;
            } else if (requested_w == 768 && requested_h == 576) {
                target_w = 768;
                target_h = 576;
            } else {
                target_w = 640;
                target_h = 480;
            }
        }
    }

    if (opengl_video || (window->flags & SDL_WINDOW_OPENGL)) {
        target_w = 640;
        target_h = 480;
    }

    DCWIN_PROBE("SetWindowSize requested=%dx%d applying=%dx%d flags=0x%08lx\n",
                requested_w, requested_h, target_w, target_h,
                (unsigned long)window->flags);

    window->w = target_w;
    window->h = target_h;
    window->pending.w = target_w;
    window->pending.h = target_h;

    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, target_w, target_h);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, target_w, target_h);
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
