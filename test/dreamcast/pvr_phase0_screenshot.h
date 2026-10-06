/* Optional physical-framebuffer capture helper for the Phase 0 PVR fixtures. */
#ifndef PVR_PHASE0_SCREENSHOT_H
#define PVR_PHASE0_SCREENSHOT_H

#include <SDL3/SDL.h>

#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
#include <dc/video.h>

static bool pvr_phase0_capture(const char *path)
{
    /* RenderPresent waits for rendering; the visible-page flip is vblank-driven. */
    vid_waitvbl();
    if (vid_screen_shot(path) < 0) {
        return SDL_SetError("Could not save hardware screenshot to %s", path);
    }
    SDL_Log("Saved hardware screenshot to %s", path);
    return true;
}
#endif

#endif /* PVR_PHASE0_SCREENSHOT_H */
