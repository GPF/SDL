/* Optional physical-framebuffer capture helper for the Phase 0 PVR fixtures. */
#ifndef PVR_PHASE0_SCREENSHOT_H
#define PVR_PHASE0_SCREENSHOT_H

#include <SDL3/SDL.h>

#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
#include <stdint.h>
#include <stdio.h>
#include <dc/video.h>
#include <dc/pvr.h>

/* Capture the page the PVR is displaying (or about to display).
 *
 * vid_screen_shot() reads vram_l, which is not tied to the PVR's own page
 * flipping, so it can return the wrong page depending on flip parity.
 * pvr_get_front_buffer() returns the PVR front page; its address is for 64-bit
 * memory, so convert it back to the linear 32-bit-area address for CPU reads.
 *
 * NOTE: SDL_RenderPresent no longer waits for the PVR to finish rendering the
 * frame (texture VRAM is retired by scene generation instead of waiting), so a
 * capture taken immediately can show the previous frame. Wait for the frame to be
 * rendered and flipped first. Only RGB565 display modes are handled. */
static bool pvr_phase0_capture(const char *path)
{
    const uintptr_t base = 0xa5000000u; /* PVR_RAM_BASE: 32-bit area, P2 */
    uintptr_t front64;
    const uint16_t *fb;
    FILE *fh;
    int w, h, x, y;

    if (vid_mode->pm != PM_RGB565) {
        return SDL_SetError("capture supports RGB565 display only (pm=%d)", (int)vid_mode->pm);
    }
    SDL_Delay(100); /* several vblanks: let the last present render and flip */
    front64 = (uintptr_t)pvr_get_front_buffer();
    fb = (const uint16_t *)(base + (front64 - base) / 2);
    w = vid_mode->width;
    h = vid_mode->height;
    SDL_Log("capture: front64=%p linear=%p back64=%p vram_l=%p", (void *)front64, (const void *)fb,
            (void *)pvr_get_back_buffer(), (void *)vram_l);

    fh = fopen(path, "wb");
    if (!fh) {
        return SDL_SetError("Could not open %s", path);
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
    return true;
}
#endif

#endif /* PVR_PHASE0_SCREENSHOT_H */
