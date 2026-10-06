/* Phase 0 hardware gate 1: default driver selection and PVR lifecycle.
 *
 * This is the gate. Everything else assumes it passes.
 *
 * Expected lines on hardware at 640x480 VGA:
 *   pvr_gate renderer=Dreamcast PVR
 *   pvr_gate output=640x480
 *   pvr_gate formats=RGB565,ARGB1555,ARGB4444
 *   pvr_gate vsync=PASS default=0 toggle=0,1,0 unsupported=2
 *   pvr_gate clear_present=PASS
 *   pvr_gate destroy_returned=PASS
 *   pvr_gate lifecycle=PASS
 *   Program returned 0
 *
 * The PVR renderer is opt-in: the default Dreamcast video mode is still OpenGL,
 * so this gate selects SDL_DC_DREAMCAST_PVR_VIDEO explicitly. If renderer is not
 * Dreamcast PVR, the video-mode path is not selecting the PVR driver.
 *
 * A hang here means PVR ownership is wrong (the video layer already called
 * pvr_init_defaults(), or the scene/list lifetime is unbalanced). SDL has no
 * return value for renderer destruction, so the lifecycle criterion is the clean
 * exit code plus the absence of a hang, not the destroy_returned line alone.
 */

#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

int main(int argc, char **argv)
{
    /* Opt-in path: select the PVR video mode, which sets the render driver. */
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_gate SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0 gate", 640, 480, 0);
    if (!window) {
        SDL_Log("pvr_gate CreateWindow=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* No driver name: this is the default-driver path. */
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_Log("pvr_gate CreateRenderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const char *name = SDL_GetRendererName(renderer);
    SDL_Log("pvr_gate renderer=%s", name ? name : "(null)");
    if (!name || SDL_strcmp(name, "Dreamcast PVR") != 0) {
        SDL_Log("pvr_gate default_driver=FAIL expected 'Dreamcast PVR'");
        SDL_Quit();
        return 1;
    }

    int failures = 0;
    int vsync = -1;
    SDL_ClearError();
    if (!SDL_SetRenderVSync(renderer, 0) || *SDL_GetError() ||
        !SDL_GetRenderVSync(renderer, &vsync) || vsync != 0) {
        SDL_Log("pvr_gate vsync_default=FAIL expected 0 without error (%s)", SDL_GetError());
        ++failures;
    } else if (!SDL_SetRenderVSync(renderer, 1) ||
               !SDL_GetRenderVSync(renderer, &vsync) || vsync != 1 ||
               !SDL_SetRenderVSync(renderer, 0) ||
               !SDL_GetRenderVSync(renderer, &vsync) || vsync != 0) {
        SDL_Log("pvr_gate vsync_toggle=FAIL %s", SDL_GetError());
        ++failures;
    } else {
        SDL_Log("pvr_gate vsync=PASS default=0 toggle=0,1,0");
    }
    SDL_ClearError();
    if (SDL_SetRenderVSync(renderer, 2)) {
        SDL_Log("pvr_gate vsync_interval_rejected=FAIL expected unsupported interval");
        ++failures;
    } else {
        SDL_Log("pvr_gate vsync_interval_rejected=PASS value=2");
    }
    SDL_ClearError();

    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer, &w, &h);
    SDL_Log("pvr_gate output=%dx%d", w, h);

    /* Supported formats are observable through the public API: ask for each one
     * and report what the core actually handed the backend. */
    static const Uint32 probed[] = {
        SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_ARGB1555, SDL_PIXELFORMAT_ARGB4444
    };
    for (size_t i = 0; i < SDL_arraysize(probed); ++i) {
        SDL_Texture *texture = SDL_CreateTexture(renderer, probed[i],
                                                 SDL_TEXTUREACCESS_STATIC, 16, 16);
        if (!texture) {
            SDL_Log("pvr_gate format[%zu]=FAIL %s", i, SDL_GetError());
            ++failures;
            continue;
        }
        SDL_Log("pvr_gate format[%zu]=%s", i, SDL_GetPixelFormatName(texture->format));
        SDL_DestroyTexture(texture);
    }

    for (int frame = 0; frame < 3; ++frame) {
        if (!SDL_SetRenderDrawColorFloat(renderer, 0.25f, 0.50f, 0.75f, 1.0f)) {
            SDL_Log("pvr_gate drawcolor=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        if (!SDL_RenderClear(renderer)) {
            SDL_Log("pvr_gate clear=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        if (!SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_gate present=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
    }
    SDL_Log("pvr_gate clear_present=%s", failures ? "FAIL" : "PASS");

#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (!failures && !pvr_phase0_capture("/pc/pvr_phase0_lifecycle.ppm")) {
        SDL_Log("pvr_gate screenshot=FAIL %s", SDL_GetError());
        ++failures;
    }
#endif
    SDL_DestroyRenderer(renderer);
    /* SDL has no return value for renderer destruction. This line only records
     * that destruction returned; the lifecycle criterion is the exit code and
     * the absence of a hang. */
    SDL_Log("pvr_gate destroy_returned=PASS");
    SDL_Log("pvr_gate lifecycle=%s", failures ? "FAIL" : "PASS");
    SDL_Quit();
    return failures ? 1 : 0;
}
