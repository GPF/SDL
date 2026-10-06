/* Phase 0 hardware gate 8: deferred texture destruction.
 *
 * Flush a textured draw so the PVR scene/list references its VRAM, destroy the
 * SDL texture before presentation, then present and inspect the captured frame.
 */

#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

static bool require_pvr(SDL_Renderer *renderer)
{
    const char *name = SDL_GetRendererName(renderer);
    SDL_Log("pvr_deferred_texture renderer=%s", name ? name : "(null)");
    if (!name || SDL_strcmp(name, "Dreamcast PVR") != 0) {
        SDL_Log("pvr_deferred_texture native_renderer=FAIL expected 'Dreamcast PVR'");
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_deferred_texture SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0: deferred texture free", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        SDL_Log("pvr_deferred_texture renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!require_pvr(renderer)) {
        SDL_Quit();
        return 1;
    }

    bool success = true;

    SDL_Texture *texture = NULL;
    SDL_Texture *control_texture = NULL;
    if (success) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                    SDL_TEXTUREACCESS_STATIC, 64, 64);
        if (!texture) {
            SDL_Log("pvr_deferred_texture CreateTexture=FAIL %s", SDL_GetError());
            success = false;
        }
    }
    if (success) {
        control_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                            SDL_TEXTUREACCESS_STATIC, 64, 64);
        if (!control_texture) {
            SDL_Log("pvr_deferred_texture control_CreateTexture=FAIL %s", SDL_GetError());
            success = false;
        }
    }

    static Uint16 pixels[64 * 64];
    static Uint16 control_pixels[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool checker = (((x >> 3) + (y >> 3)) & 1) != 0;
            pixels[y * 64 + x] = checker ? 0x801fu : 0xfc00u;
            control_pixels[y * 64 + x] = checker ? 0x83e0u : 0xffe0u;
        }
    }
    const SDL_Rect full = { 0, 0, 64, 64 };
    const SDL_FRect dst = { 128.0f, 176.0f, 128.0f, 128.0f };
    const SDL_FRect control_dst = { 384.0f, 176.0f, 128.0f, 128.0f };
    if (success && !SDL_UpdateTexture(texture, &full, pixels, 64 * (int)sizeof(Uint16))) {
        SDL_Log("pvr_deferred_texture upload=FAIL %s", SDL_GetError());
        success = false;
    }
    if (success && !SDL_UpdateTexture(control_texture, &full, control_pixels,
                                      64 * (int)sizeof(Uint16))) {
        SDL_Log("pvr_deferred_texture control_upload=FAIL %s", SDL_GetError());
        success = false;
    }
    if (success && (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST) ||
                    !SDL_SetTextureScaleMode(control_texture, SDL_SCALEMODE_NEAREST))) {
        SDL_Log("pvr_deferred_texture scale_mode=FAIL %s", SDL_GetError());
        success = false;
    }
    /* Warm the page with actual textured scenes, like the proven textured
     * quad gate. Empty PVR scenes after drawing can replace the visible page. */
    for (int frame = 0; success && frame < 3; ++frame) {
        if (!SDL_SetRenderDrawColorFloat(renderer, 1.0f, 1.0f, 1.0f, 1.0f) ||
            !SDL_RenderClear(renderer) ||
            !SDL_RenderTexture(renderer, texture, NULL, &dst) ||
            !SDL_RenderTexture(renderer, control_texture, NULL, &control_dst) ||
            !SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_deferred_texture warmup frame=%d FAIL %s", frame, SDL_GetError());
            success = false;
        }
    }
    if (success) {
        SDL_Log("pvr_deferred_texture warmup=PASS frames=3");
    }
    if (success &&
        (!SDL_SetRenderDrawColorFloat(renderer, 1.0f, 1.0f, 1.0f, 1.0f) ||
         !SDL_RenderClear(renderer) ||
         !SDL_RenderTexture(renderer, texture, NULL, &dst) ||
         !SDL_RenderTexture(renderer, control_texture, NULL, &control_dst))) {
        SDL_Log("pvr_deferred_texture draw=FAIL %s", SDL_GetError());
        success = false;
    }
    if (success) {
        /* Match SDL2's queued-destroy probe: SDL_DestroyTexture flushes the
         * queued draw, opening the PVR scene before the backend callback. */
        SDL_DestroyTexture(texture);
        texture = NULL;
        SDL_Log("pvr_deferred_texture destroyed_before_present=PASS");
    }
    if (success && !SDL_RenderPresent(renderer)) {
        SDL_Log("pvr_deferred_texture present_after_destroy=FAIL %s", SDL_GetError());
        success = false;
    } else if (success) {
        SDL_Log("pvr_deferred_texture present_after_destroy=PASS");
    }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (success && !pvr_phase0_capture("/pc/pvr_phase0_deferred_texture.ppm")) {
        SDL_Log("pvr_deferred_texture screenshot=FAIL %s", SDL_GetError());
        success = false;
    }
#endif

    SDL_Log("pvr_deferred_texture exiting_in=3s");
    SDL_Delay(3000);

    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (control_texture) {
        SDL_DestroyTexture(control_texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_Log("pvr_deferred_texture result=%s", success ? "PASS (inspect retained textured draw)" : "FAIL");
    SDL_Quit();
    return success ? 0 : 1;
}
