/* Phase 0 hardware smoke 2: textured quad.
 *
 * Expected on hardware at 640x480 VGA:
 *   - 128x128 checkerboard quad at pixel (256,176), i.e. 8x8 logical cells of
 *     16 px each, red/blue alternating
 *   - nearest filtering for SDL_SCALEMODE_NEAREST, bilinear for LINEAR
 *   - untextured clear uses PVR_TXRENV_REPLACE; textured geometry uses
 *     MODULATEALPHA, so the draw color modulates the texture
 *
 * Failure signatures:
 *   - black quad -> texture never uploaded, or the VRAM pointer is wrong
 *   - quad drawn in the flat draw color -> texture env flag wrong
 *   - checkerboard sheared -> twiddling/format mismatch
 *   - ARGB1555 requested but core handed a different format -> check the
 *     printed format line before blaming the backend
 */

#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

static bool require_pvr(SDL_Renderer *renderer, const char *gate)
{
    const char *name = SDL_GetRendererName(renderer);
    SDL_Log("%s renderer=%s", gate, name ? name : "(null)");
    if (!name || SDL_strcmp(name, "Dreamcast PVR") != 0) {
        SDL_Log("%s native_renderer=FAIL expected 'Dreamcast PVR', got '%s'", gate, name ? name : "(null)");
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    /* The PVR renderer is opt-in, so select the PVR video mode explicitly. */
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_smoke2 SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0: textured quad", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        SDL_Log("pvr_smoke2 renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!require_pvr(renderer, "pvr_smoke2")) {
        SDL_Quit();
        return 1;
    }

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                             SDL_TEXTUREACCESS_STATIC, 64, 64);
    if (!texture) {
        SDL_Log("pvr_smoke2 CreateTexture=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Log("pvr_smoke2 texture_format=%s backing=64x64 (POT)",
            SDL_GetPixelFormatName(texture->format));

    static Uint16 pixels[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool checker = (((x >> 3) + (y >> 3)) & 1) != 0;
            /* ARGB1555 requires bit 15 set for opaque texels. */
            pixels[y * 64 + x] = checker ? 0x801fu : 0xfc00u;
        }
    }

    const SDL_Rect full = {0, 0, 64, 64};
    if (!SDL_UpdateTexture(texture, &full, pixels, 64 * 2)) {
        SDL_Log("pvr_smoke2 UpdateTexture=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const SDL_FRect dst = {256.0f, 176.0f, 128.0f, 128.0f};
    int failures = 0;

    for (int frame = 0; frame < 10; ++frame) {
        const SDL_ScaleMode mode = (frame % 2) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
        if (!SDL_SetTextureScaleMode(texture, mode)) {
            SDL_Log("pvr_smoke2 SetTextureScaleMode=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        SDL_SetRenderDrawColorFloat(renderer, 1.0f, 1.0f, 1.0f, 1.0f);
        if (!SDL_RenderClear(renderer) || !SDL_RenderTexture(renderer, texture, NULL, &dst)) {
            SDL_Log("pvr_smoke2 draw=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        if (!SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_smoke2 present=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
        if (frame == 8 && !pvr_phase0_capture("/pc/pvr_phase0_textured_quad_nearest.ppm")) {
            SDL_Log("pvr_smoke2 screenshot=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        if (frame == 9 && !pvr_phase0_capture("/pc/pvr_phase0_textured_quad_linear.ppm")) {
            SDL_Log("pvr_smoke2 screenshot=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
#endif
        SDL_Log("pvr_smoke2 frame=%d scale_mode=%s", frame,
                mode == SDL_SCALEMODE_LINEAR ? "LINEAR" : "NEAREST");
    }
    SDL_Log("pvr_smoke2 result=%s", failures ? "FAIL" : "PASS (visual check)");

    SDL_Quit();
    return failures ? 1 : 0;
}
