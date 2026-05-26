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

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "SDL_hints.h"
#include "../../events/SDL_events_c.h"

#include "SDL_dreamcastvideo.h"
#include "SDL_dreamcastevents_c.h"
#include "SDL_dreamcastframebuffer_c.h"
#include "SDL_hints.h"
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

void SDL_DC_ShowAskHz(SDL_bool value)
{
	sdl_dc_no_ask_60hz=!value;
}

void SDL_DC_Default60Hz(SDL_bool value)
{
	sdl_dc_default_60hz=value;
}

/* Initialization/Query functions */
static int DREAMCAST_VideoInit(_THIS);
static void DREAMCAST_VideoQuit(_THIS);
void DREAMCAST_GetDisplayModes(_THIS, SDL_VideoDisplay *display);
int DREAMCAST_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
#ifdef SDL_INPUT_LINUXEV
static int evdev = 0;
static void DREAMCAST_EVDEV_Poll(_THIS);
#endif


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
int dreamcast_text_input_enabled=SDL_FALSE;
void DREAMCAST_StartTextInput(SDL_VideoDevice *device)
{
    if (!dreamcast_text_input_enabled)
    {
        dreamcast_text_input_enabled = SDL_TRUE;
        // printf("DREAMCAST_StartTextInput: Text input mode enabled.\n");
    }
}

void DREAMCAST_StopTextInput(SDL_VideoDevice *device)
{
    if (dreamcast_text_input_enabled)
    {
        dreamcast_text_input_enabled = SDL_FALSE;
        // printf("DREAMCAST_StopTextInput: Text input mode disabled.\n");
    }
}

static void DREAMCAST_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *DREAMCAST_CreateDevice(void)
{
    SDL_VideoDevice *device;
    if (!DREAMCAST_Available()) {
        SDL_SetError("Dreamcast video driver is not available.");
        return 0;
    }

    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return 0;
    }

    // Set SDL function pointers
    device->VideoInit = DREAMCAST_VideoInit;
    device->VideoQuit = DREAMCAST_VideoQuit;
    device->PumpEvents = DREAMCAST_PumpEvents;
    device->GetDisplayModes = DREAMCAST_GetDisplayModes;
    device->SetDisplayMode = DREAMCAST_SetDisplayMode;
    device->StartTextInput = DREAMCAST_StartTextInput;
    device->StopTextInput = DREAMCAST_StopTextInput;
    device->CreateSDLWindow = DREAMCAST_CreateWindow;
    device->SetWindowTitle = DREAMCAST_SetWindowTitle;
    device->DestroyWindow = DREAMCAST_DestroyWindow;

#ifdef SDL_INPUT_LINUXEV
    if (evdev) {
        device->PumpEvents = DREAMCAST_EVDEV_Poll;
    }
#endif

#ifndef SDL_VIDEO_OPENGL
    device->CreateWindowFramebuffer = SDL_DREAMCAST_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = SDL_DREAMCAST_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = SDL_DREAMCAST_DestroyWindowFramebuffer;
#endif

#ifdef SDL_VIDEO_OPENGL
    device->GL_LoadLibrary = DREAMCAST_GL_LoadLibrary;
    device->GL_GetProcAddress = DREAMCAST_GL_GetProcAddress;
    device->GL_MakeCurrent = DREAMCAST_GL_MakeCurrent;
    device->GL_SwapWindow = DREAMCAST_GL_SwapBuffers;
    device->GL_CreateContext = DREAMCAST_GL_CreateContext;
    device->GL_DeleteContext = DREAMCAST_GL_DeleteContext;
#endif

    // ✅ FIX: Allocate driverdata
    SDL_VideoData *video_data = SDL_calloc(1, sizeof(SDL_VideoData));
    if (!video_data) {
        SDL_free(device);
        SDL_OutOfMemory();
        return NULL;
    }

    video_data->w = 640;
    video_data->h = 480;
    device->driverdata = video_data;

    device->free = DREAMCAST_DeleteDevice;
    device->quirk_flags = VIDEO_DEVICE_QUIRK_FULLSCREEN_ONLY;
    return device;
}


VideoBootStrap DREAMCAST_bootstrap = {
    "DREAMCASTVID_DRIVER_NAME", "SDL dreamcast video driver",
    DREAMCAST_CreateDevice,
    NULL /* no ShowMessageBox implementation */
};

#ifdef SDL_INPUT_LINUXEV
VideoBootStrap DREAMCAST_evdev_bootstrap = {
    DREAMCASTVID_DRIVER_EVDEV_NAME, "SDL dreamcast video driver with evdev",
    DREAMCAST_CreateDevice,
    NULL /* no ShowMessageBox implementation */
};
void SDL_EVDEV_Init(void);
void SDL_EVDEV_Poll();
void SDL_EVDEV_Quit(void);
static void DREAMCAST_EVDEV_Poll(_THIS)
{
    (void)_this;
    SDL_EVDEV_Poll();
}
#endif

int __sdl_dc_is_60hz = 0;

int DREAMCAST_VideoInit(_THIS) {
    SDL_DisplayMode current_mode;
    SDL_VideoDisplay display;

    // Set default pixel format to 16-bit RGB565
    Uint32 Rmask = 0x0000f800;
    Uint32 Gmask = 0x000007e0;
    Uint32 Bmask = 0x0000001f;
    Uint32 Amask = 0; // No alpha channel
    int width = 640;
    int height = 480;   
    __sdl_dc_mouse_shift=640/width; 
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    if (video_mode_hint != NULL && strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
        SDL_Log("Initializing SDL_DC_TEXTURED_VIDEO");
        width = 320;
        height = 240;
        __sdl_dc_mouse_shift=640/width;
    }
    SDL_zero(current_mode);

    // Initialize the default display mode
    current_mode.format = SDL_PIXELFORMAT_ARGB1555;
    current_mode.w = width;
    current_mode.h = height;
    current_mode.refresh_rate = 60; // Assume 60Hz for simplicity
    current_mode.driverdata = NULL;

    // Set up the display
    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = NULL;

    // Add the display to SDL's list of displays
    if (SDL_AddVideoDisplay(&display, SDL_FALSE) < 0) {
        SDL_SetError("Failed to add video display");
        return -1;
    }
#ifndef SDL_VIDEO_OPENGL    
    if (video_mode_hint == NULL){
        SDL_Log("No video mode hint set, defaulting to SDL_DC_DMA_VIDEO mode, with SDL_HINT_VIDEO_DOUBLE_BUFFER enabled");
        SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DMA_VIDEO"); // Default video mode
        SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1"); // Default double buffering
        // SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    }
#endif    
    // SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
    SDL_Log("SDL2 Dreamcast video initialized: %dx%d, ARGB1555", width, height);
    return 0;
}


void DREAMCAST_GetDisplayModes(_THIS, SDL_VideoDisplay *display) {
    SDL_DisplayMode mode;
    int refresh_rate = __sdl_dc_is_60hz ? 60 : 50;

    // Adding supported modes
    const int supported_modes[][2] = {
        {320, 240},
        {640, 480},
        {768, 480}
    };

    for (int i = 0; i < sizeof(supported_modes) / sizeof(supported_modes[0]); i++) {
        SDL_zero(mode);
        mode.w = supported_modes[i][0];
        mode.h = supported_modes[i][1];
        mode.format = SDL_PIXELFORMAT_ARGB1555; // Change as necessary
        mode.refresh_rate = refresh_rate;
        SDL_AddDisplayMode(display, &mode);
    }

    SDL_Log("Display modes for Dreamcast retrieved successfully.");
}

int DREAMCAST_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode) {
    int pixel_mode;
    int disp_mode = -1;
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    const char *double_buffer_hint = SDL_GetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER);
    if (video_mode_hint != NULL && strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
        SDL_Log("Setting SDL_DC_TEXTURED_VIDEO mode");
        mode->w = 640;
        mode->h = 480;
    }
  
#ifdef SDL_VIDEO_OPENGL
    // Enforce OpenGL requirement for 640x480 if SDL_VIDEO_OPENGL is disabled
    // if (SDL_GetCurrentVideoDriver() && strcmp(SDL_GetCurrentVideoDriver(), "opengl") == 0) {
        mode->w = 640;
        mode->h = 480;
        mode->format = SDL_PIXELFORMAT_ARGB1555;
    // }
#endif
  

    // Detect cable and region
    if (!vid_check_cable()) {
        __sdl_dc_is_60hz = 1;
    } else if (flashrom_get_region() != FLASHROM_REGION_US && !sdl_dc_ask_60hz()) {
        __sdl_dc_is_60hz = 0;
    } else {
        __sdl_dc_is_60hz = 1;
    }

    // Determine display mode based on resolution and refresh rate
    if (__sdl_dc_is_60hz) {
        if (mode->w == 320 && mode->h == 240) {
            disp_mode = DM_320x240;
        } else if (mode->w == 640 && mode->h == 480) {
            disp_mode = DM_640x480;
        } else if (mode->w == 768 && mode->h == 480) {
            disp_mode = DM_768x480;
        }
    } else {
        if (mode->w == 320 && mode->h == 240) {
            disp_mode = DM_320x240_PAL;
        } else if (mode->w == 640 && mode->h == 480) {
            disp_mode = DM_640x480_PAL_IL;
        } else if (mode->w == 768 && mode->h == 576) {
            disp_mode = DM_768x576_PAL_IL;
        }
    }

    if (disp_mode < 0) {
        SDL_SetError("Unsupported display mode: %dx%d", mode->w, mode->h);
        return -1;
    }

    if (double_buffer_hint && SDL_GetHintBoolean(SDL_HINT_VIDEO_DOUBLE_BUFFER, SDL_TRUE)) {
            SDL_Log("Double Buffer video enabled");
            if (video_mode_hint != NULL && strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0) {
                SDL_Log("Setting SDL_DC_DIRECT_VIDEO mode with double buffer");        
                disp_mode |= DM_MULTIBUFFER; // Enable double buffering
            }
    }  

    // Ensure mode->format is set
    if (mode->format == 0) {
        mode->format = SDL_PIXELFORMAT_ARGB1555;  // Default pixel format
    }

    // Map SDL pixel format to Dreamcast pixel mode
    switch (mode->format) {
        case SDL_PIXELFORMAT_ARGB1555:
            pixel_mode = PM_RGB555;
            break;
        case SDL_PIXELFORMAT_RGB565:
            pixel_mode = PM_RGB565;
            break;
        case SDL_PIXELFORMAT_RGB888:
            pixel_mode = PM_RGB888;
            break;
        default:
            SDL_SetError("Unsupported pixel format: %s", SDL_GetPixelFormatName(mode->format));
            return -1;
    }

    // Set video mode in hardware
    vid_set_mode(disp_mode, pixel_mode);

    if (double_buffer_hint && SDL_GetHintBoolean(SDL_HINT_VIDEO_DOUBLE_BUFFER, SDL_TRUE)) {
     vid_mode->fb_count =2; // Enable double buffering
    }
    return 0;
}

void DREAMCAST_VideoQuit(_THIS)
{
#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Quit();
#endif
}

#endif /* SDL_VIDEO_DRIVER_DREAMCAST */

/* vi: set ts=4 sw=4 expandtab: */
