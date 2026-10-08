/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Optional physical-framebuffer capture helper for the native Dreamcast PVR fixtures.
*/

#ifndef PVR_HARDWARE_SCREENSHOT_H
#define PVR_HARDWARE_SCREENSHOT_H

#include <SDL.h>

#ifdef PVR_HARDWARE_SCREENSHOT

#include <stdint.h>
#include <stdio.h>
#include <dc/video.h>
#include <dc/pvr.h>

/* Capture the page the PVR is displaying (or about to display).

 * vid_screen_shot() reads vram_l, which is not tied to the PVR's own page flipping,
 * so it can return the wrong page depending on flip parity. pvr_get_front_buffer()
 * returns the PVR front page; its address is in 64-bit memory space, so convert it
 * back to the linear 32-bit-area address for CPU reads.

 * SDL_RenderPresent no longer waits for the PVR to finish rendering the frame
 * (texture VRAM is retired by scene generation instead of waiting), so a capture
 * taken immediately can show a previous frame. The PVR path is three-buffered, so
 * wait for the last present to render and flip before reading. Only RGB565 display
 * modes are handled. */
static SDL_bool pvr_hardware_capture(const char *path)
{
    const uintptr_t base = 0xa5000000u; /* PVR_RAM_BASE: 32-bit area, P2 */
    uintptr_t front64;
    const uint16_t *fb;
    FILE *fh;
    int w, h, x, y;

    if (vid_mode->pm != PM_RGB565) {
        SDL_SetError("capture supports RGB565 display only (pm=%d)", (int)vid_mode->pm);
        return SDL_FALSE;
    }
    SDL_Delay(150); /* several vblanks: let the last present render and flip */
    front64 = (uintptr_t)pvr_get_front_buffer();
    fb = (const uint16_t *)(base + (front64 - base) / 2);
    w = vid_mode->width;
    h = vid_mode->height;
    SDL_Log("capture: front64=%p linear=%p back64=%p vram_l=%p", (void *)front64, (const void *)fb,
            (void *)pvr_get_back_buffer(), (void *)vram_l);

    fh = fopen(path, "wb");
    if (!fh) {
        SDL_SetError("Could not open %s", path);
        return SDL_FALSE;
    }
    fprintf(fh, "P6\n#PVR front buffer capture\n%d %d\n255\n", w, h);
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            const uint16_t p = fb[y * w + x];
            const uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 0x1f) << 3),
                                     (uint8_t)(((p >> 5) & 0x3f) << 2),
                                     (uint8_t)((p & 0x1f) << 3) };
            fwrite(rgb, 1, 3, fh);
        }
    }
    fclose(fh);
    SDL_Log("Saved hardware screenshot to %s", path);
    return SDL_TRUE;
}

#endif /* PVR_HARDWARE_SCREENSHOT */

#endif /* PVR_HARDWARE_SCREENSHOT_H */
