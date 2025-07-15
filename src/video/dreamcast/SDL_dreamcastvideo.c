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

/* Dummy SDL video driver implementation; this is just enough to make an
 *  SDL-based application THINK it's got a working video driver, for
 *  applications that call SDL_Init(SDL_INIT_VIDEO) when they don't need it,
 *  and also for use as a collection of stubs when porting SDL to a new
 *  platform for which you haven't yet written a valid video driver.
 *
 * This is also a great way to determine bottlenecks: if you think that SDL
 *  is a performance problem for a given platform, enable this driver, and
 *  then see if your application runs faster without video overhead.
 *
 * Initial work by Ryan C. Gordon (icculus@icculus.org). A good portion
 *  of this was cut-and-pasted from Stephane Peter's work in the AAlib
 *  SDL video driver.  Renamed to "DUMMY" by Sam Lantinga.
 */

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_mouse.h>
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include <SDL3/SDL_hints.h>
#include "../../events/SDL_events_c.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "SDL_dreamcastvideo.h"
// #include "SDL_dreamcastevents_c.h"
#include "SDL_dreamcastkeyboard.h"
#include "SDL_dreamcastmouse.h"
#include "SDL_dreamcastframebuffer_c.h"
#include <kos.h>
#include <dc/video.h>
#include "SDL_dreamcastwindow.h"
#define DREAMCASTVID_DRIVER_NAME       "dcvideo"
#define DREAMCASTVID_DRIVER_EVDEV_NAME "dcevdev"

#include "SDL_dreamcastopengl.h"
//Custom code for 60Hz
static int sdl_dc_no_ask_60hz=0;
static int sdl_dc_default_60hz=0;
unsigned int __sdl_dc_mouse_shift=1;
#include "60hz.h"

void SDL_DC_ShowAskHz(bool value)
{
	sdl_dc_no_ask_60hz=!value;
}

void SDL_DC_Default60Hz(bool value)
{
	sdl_dc_default_60hz=value;
}

/* Initialization/Query functions */
static bool DREAMCAST_VideoInit(SDL_VideoDevice *_this);
void DREAMCAST_VideoQuit(SDL_VideoDevice *_this);
// static bool DREAMCAST_GetDisplayModes(SDL_VideoDevice *_this, SDL_VideoDisplay *display);
static bool DREAMCAST_SetDisplayMode(SDL_VideoDevice *_this, SDL_VideoDisplay *display, SDL_DisplayMode *mode);


/* DREAMCAST driver bootstrap functions */
static int DREAMCAST_Available(void)
{
//     const char *envr = SDL_GetHint(SDL_HINT_VIDEODRIVER);
//     if (envr) {
//         if (SDL_strcmp(envr, DREAMCASTVID_DRIVER_NAME) == 0) {
//             return 1;
//         }
// #ifdef SDL_INPUT_LINUXEV
//         if (SDL_strcmp(envr, DREAMCASTVID_DRIVER_EVDEV_NAME) == 0) {
//             evdev = 1;
//             return 1;
//         }
// #endif
//     }
    return 1;

}

// int dreamcast_text_input_enabled = false;
static bool DREAMCAST_StartTextInput(SDL_VideoDevice *device, SDL_Window *window, SDL_PropertiesID props)
{
    // if (!dreamcast_text_input_enabled) {
    //     dreamcast_text_input_enabled = true;
    //     // SDL_Log("DREAMCAST_StartTextInput: enabled");
    // }
    SDL_Log("Text input started for window: %p", (void*)window);
    return true;
}

static bool DREAMCAST_StopTextInput(SDL_VideoDevice *device, SDL_Window *window)
{
    // if (dreamcast_text_input_enabled) {
    //     dreamcast_text_input_enabled = false;
    //     // SDL_Log("DREAMCAST_StopTextInput: disabled");
    // }
    SDL_Log("Text input stopped for window: %p", (void*)window);
    return true;
}

static void DREAMCAST_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

#define MIN_FRAME_UPDATE 16
void DREAMCAST_PumpEvents(SDL_VideoDevice *_this)
{
    // printf("DREAMCAST_PumpEvents() called\n");
    static Uint32 last_time = 0;
    Uint32 now = SDL_GetTicks();

    if ((now - last_time) >= MIN_FRAME_UPDATE) {
        DREAMCAST_PollKeyboard();
        DREAMCAST_PollMouse();
        last_time = now;
    }
}



static SDL_VideoDevice *DREAMCAST_CreateDevice(void)
{
    SDL_VideoDevice *device;
    if (!DREAMCAST_Available()) {
        SDL_SetError("Dreamcast video driver is not available.");
        return 0;
    }

    /* Initialize all variables that we clean on shutdown */
    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return 0;
    }


    /* Set the function pointers */
    device->VideoInit = DREAMCAST_VideoInit;
    device->VideoQuit = DREAMCAST_VideoQuit;
    device->PumpEvents = DREAMCAST_PumpEvents;
    // device->GetDisplayModes = DREAMCAST_GetDisplayModes;
    // device->SetDisplayMode = DREAMCAST_SetDisplayMode;
    // device->HasScreenKeyboardSupport = DREAMCAST_HasScreenKeyboardSupport;
    device->StartTextInput = DREAMCAST_StartTextInput;
    device->StopTextInput = DREAMCAST_StopTextInput;    

    device->CreateSDLWindow = DREAMCAST_CreateWindow;
    device->SetWindowTitle = NULL;
    device->SetWindowSize = DREAMCAST_SetWindowSize;
    device->DestroyWindow = DREAMCAST_DestroyWindow;

    device->CreateWindowFramebuffer = SDL_DREAMCAST_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = SDL_DREAMCAST_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = SDL_DREAMCAST_DestroyWindowFramebuffer;

#ifdef SDL_VIDEO_OPENGL
    device->GL_LoadLibrary = DREAMCAST_GL_LoadLibrary;
    device->GL_GetProcAddress = DREAMCAST_GL_GetProcAddress;
    device->GL_MakeCurrent = DREAMCAST_GL_MakeCurrent;
    device->GL_SwapWindow = DREAMCAST_GL_SwapBuffers;
    device->GL_CreateContext = DREAMCAST_GL_CreateContext;
    device->GL_DestroyContext = DREAMCAST_GL_DestroyContext;
#endif

    device->free = DREAMCAST_DeleteDevice;
    // device->quirk_flags = VIDEO_DEVICE_QUIRK_FULLSCREEN_ONLY;
    return device;
}

VideoBootStrap DREAMCAST_bootstrap = {
    "DREAMCASTVID_DRIVER_NAME", "SDL dreamcast video driver",
    DREAMCAST_CreateDevice,
    NULL /* no ShowMessageBox implementation */
};

int __sdl_dc_is_60hz = 0;
static bool DREAMCAST_VideoInit(SDL_VideoDevice *_this) {
    SDL_DisplayMode mode;
    int pixel_mode = PM_RGB555;
    int disp_mode = -1;
    int default_w = 640, default_h = 480; // Default resolution for display mode
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);

    // Determine NTSC (60Hz) or PAL (50Hz)
    if (!vid_check_cable()) {
        __sdl_dc_is_60hz = 1;
    } else if (flashrom_get_region() != FLASHROM_REGION_US && !sdl_dc_ask_60hz()) {
        __sdl_dc_is_60hz = 0;
    } else {
        __sdl_dc_is_60hz = 1;
    }

#ifndef SDL_VIDEO_OPENGL
    if (!video_mode_hint) {
        SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
        SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
        video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
        bool double_buffer = SDL_GetHintBoolean(SDL_HINT_VIDEO_DOUBLE_BUFFER, true);
        SDL_Log("No video mode hint set. Using %s with %s buffering.",
                video_mode_hint, double_buffer ? "double" : "single");
    }
#endif

    // Initialize default display mode
    SDL_zero(mode);
    mode.pixel_density = 1.0f;
    mode.refresh_rate = __sdl_dc_is_60hz ? 60.0f : 50.0f;

    // Set pixel format and default resolution based on video mode
    if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
        SDL_Log("SDL_DC_DMA_VIDEO selected — forcing double buffering");
        SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
        pixel_mode = PM_RGB888; // For BGR888 BMP
        mode.format = SDL_PIXELFORMAT_XRGB8888;
        default_w = 640;
        default_h = 480;
    } else if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0) {
        SDL_Log("Initializing SDL_DC_DIRECT_VIDEO mode");
        pixel_mode = PM_RGB555;
        mode.format = SDL_PIXELFORMAT_ARGB1555;
        default_w = 640;
        default_h = 480;
    } 
else if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
    SDL_Log("Initializing SDL_DC_TEXTURED_VIDEO mode");

    // Default texture size if not already specified
    if (!SDL_GetHint(SDL_HINT_DC_SCREEN_WIDTH_TEXTURED)) {
        SDL_SetHint(SDL_HINT_DC_SCREEN_WIDTH_TEXTURED, "640");
    }
    if (!SDL_GetHint(SDL_HINT_DC_SCREEN_HEIGHT_TEXTURED)) {
        SDL_SetHint(SDL_HINT_DC_SCREEN_HEIGHT_TEXTURED, "480");
    }

    const char *width_hint = SDL_GetHint(SDL_HINT_DC_SCREEN_WIDTH_TEXTURED);
    const char *height_hint = SDL_GetHint(SDL_HINT_DC_SCREEN_HEIGHT_TEXTURED);
    SDL_Log("Default texture resolution set to %sx%s", width_hint, height_hint);

    pixel_mode = PM_RGB565;
    mode.format = SDL_PIXELFORMAT_RGB565;
    default_w = 640;
    default_h = 480;

    // Override framebuffer ops for textured framebuffer
    _this->CreateWindowFramebuffer = DREAMCAST_CreateWindowTexture;
    _this->UpdateWindowFramebuffer = DREAMCAST_UpdateWindowTexture;
    _this->DestroyWindowFramebuffer = DREAMCAST_DestroyWindowTexture;

    // Optional if you later add vsync control
    // _this->SetWindowFramebufferVSync = DREAMCAST_SetWindowTextureVSync;
    // _this->GetWindowFramebufferVSync = DREAMCAST_GetWindowTextureVSync;
}
    // else {
    //     SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Unknown video mode: %s", video_mode_hint);
    //     return false;
    // }

#ifdef SDL_VIDEO_OPENGL
    // For OpenGL, enforce 640x480 hardware resolution
    disp_mode = __sdl_dc_is_60hz ? DM_640x480 : DM_640x480_PAL_IL;
    pixel_mode = PM_RGB555; // OpenGL converts to ARGB1555
    mode.format = SDL_PIXELFORMAT_ARGB1555;
    mode.w = 640;
    mode.h = 480;
    SDL_Log("OpenGL mode: Setting hardware resolution to 640x480");
    vid_set_mode(disp_mode, pixel_mode);
#else
    // For software modes, use 640x480 for display mode
    mode.w = default_w;
    mode.h = default_h;
#endif
    //     // Assume we have a mouse and keyboard
    // SDL_AddKeyboard(SDL_DEFAULT_KEYBOARD_ID, NULL, true);
    // SDL_AddMouse(SDL_DEFAULT_MOUSE_ID, NULL, true);
    DREAMCAST_InitMouse();
    DREAMCAST_InitKeyboard();
    // Add the primary display mode
    if (!SDL_AddBasicVideoDisplay(&mode)) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed to add video display");
        return false;
    }

    // Store default resolution in display data
    SDL_VideoDisplay *display = _this->displays[0];
    SDL_zero(display->desktop_mode);
    display->desktop_mode.w = default_w;
    display->desktop_mode.h = default_h;
    display->desktop_mode.format = mode.format;
    display->desktop_mode.refresh_rate = mode.refresh_rate;
    display->desktop_mode.pixel_density = 1.0f;

    SDL_Log("SDL3 Dreamcast video initialized: %dx%d (default), texture hints: %sx%s",
            mode.w, mode.h, SDL_GetHint(SDL_HINT_DC_SCREEN_WIDTH_TEXTURED), SDL_GetHint(SDL_HINT_DC_SCREEN_HEIGHT_TEXTURED));
    return true;
}

static bool DREAMCAST_SetDisplayMode(SDL_VideoDevice *_this, SDL_VideoDisplay *display, SDL_DisplayMode *mode) {
        SDL_Log("DREAMCAST_SetDisplayMode: not used in SDL3, using default mode %dx%d",
                mode->w, mode->h);
    return true;
}



// static bool DREAMCAST_SetDisplayMode(SDL_VideoDevice *_this, SDL_VideoDisplay *display, SDL_DisplayMode *mode) {
//     int pixel_mode;
//     int disp_mode = -1;
//     SDL_Log("DREAMCAST_SetDisplayMode: %dx%d", mode->w, mode->h);
//     // Ensure any previously set hints are re-fetched for accurate usage
//     const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
//     const char *double_buffer_hint = SDL_GetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER);

//     if (!video_mode_hint) {
//         SDL_Log("No video mode hint set, defaulting to SDL_DC_DIRECT_VIDEO with double buffering");
//         SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
//         SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");

//         // RE-FETCH updated hint values
//         video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
//         double_buffer_hint = SDL_GetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER);
//     }

//     if (video_mode_hint && strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
//         SDL_Log("DREAMCAST_SetDisplayMode: Setting SDL_DC_TEXTURED_VIDEO mode (320x240)"); 
//         mode->w = 320;
//         mode->h = 240;
//     } else {
//         mode->w = 640;
//         mode->h = 480;
//     }

//     // Detect cable and region
//     if (!vid_check_cable()) {
//         __sdl_dc_is_60hz = 1;
//     } else if (flashrom_get_region() != FLASHROM_REGION_US && !sdl_dc_ask_60hz()) {
//         __sdl_dc_is_60hz = 0;
//     } else {
//         __sdl_dc_is_60hz = 1;
//     }

//     // Determine display mode based on resolution and refresh rate
//     if (__sdl_dc_is_60hz) {
//         if (mode->w == 320 && mode->h == 240) disp_mode = DM_320x240;
//         else if (mode->w == 640 && mode->h == 480) disp_mode = DM_640x480;
//         else if (mode->w == 768 && mode->h == 480) disp_mode = DM_768x480;
//     } else {
//         if (mode->w == 320 && mode->h == 240) disp_mode = DM_320x240_PAL;
//         else if (mode->w == 640 && mode->h == 480) disp_mode = DM_640x480_PAL_IL;
//         else if (mode->w == 768 && mode->h == 576) disp_mode = DM_768x576_PAL_IL;
//     }

//     if (disp_mode < 0) {
//         SDL_SetError("Unsupported display mode: %dx%d", mode->w, mode->h);
//         return false;
//     }

    // if (SDL_GetHintBoolean(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1")) {
    //     SDL_Log("Double Buffer video enabled");

        // if (video_mode_hint && SDL_strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0) {
        //     SDL_Log("Setting SDL_DC_DIRECT_VIDEO mode with double buffer");
        //     disp_mode |= DM_MULTIBUFFER;
        // }
    // }

    // Ensure mode->format is set
//     if (mode->format == 0) {
//         mode->format = SDL_PIXELFORMAT_ARGB1555;
//     }

//     switch (mode->format) {
//         case SDL_PIXELFORMAT_ARGB1555: pixel_mode = PM_RGB555; break;
//         case SDL_PIXELFORMAT_RGB565:   pixel_mode = PM_RGB565; break;
//         case SDL_PIXELFORMAT_XRGB8888: pixel_mode = PM_RGB888; break;
//         default:
//             SDL_SetError("Unsupported pixel format: %s", SDL_GetPixelFormatName(mode->format));
//             return false;
//     }

//     vid_set_mode(disp_mode, pixel_mode);

//    return true;
// }

void DREAMCAST_VideoQuit(SDL_VideoDevice *_this)
{

}

#endif /* SDL_VIDEO_DRIVER_DREAMCAST */

/* vi: set ts=4 sw=4 expandtab: */
