/* Phase 0 hardware gate 6: SDL3 copy, rotation, and flip conversion.
 *
 * Each textured quad has distinct corner colors. Compare the captured panels
 * with the orientation table printed below to verify QueueGeometry's UV and
 * winding behavior on the PVR.
 */

#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

typedef struct CopyCase
{
    const char *name;
    SDL_FRect dst;
    double angle;
    SDL_FlipMode flip;
    const char *expected;
} CopyCase;

static bool require_pvr(SDL_Renderer *renderer)
{
    const char *name = SDL_GetRendererName(renderer);
    SDL_Log("pvr_copy_transform renderer=%s", name ? name : "(null)");
    if (!name || SDL_strcmp(name, "Dreamcast PVR") != 0) {
        SDL_Log("pvr_copy_transform native_renderer=FAIL expected 'Dreamcast PVR'");
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_copy_transform SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0: copy transforms", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        SDL_Log("pvr_copy_transform renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!require_pvr(renderer)) {
        SDL_Quit();
        return 1;
    }

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                             SDL_TEXTUREACCESS_STATIC, 64, 64);
    if (!texture) {
        SDL_Log("pvr_copy_transform CreateTexture=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* SDL/PVR corner order, each color opaque in ARGB1555:
     *   red    green
     *   blue   yellow
     */
    static Uint16 pixels[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool top = y < 32;
            const bool left = x < 32;
            pixels[y * 64 + x] = top ? (left ? 0xFC00u : 0x83E0u)
                                     : (left ? 0x801Fu : 0xFFE0u);
        }
    }

    bool success = true;
    const SDL_Rect full = { 0, 0, 64, 64 };
    if (!SDL_UpdateTexture(texture, &full, pixels, 64 * (int)sizeof(Uint16))) {
        SDL_Log("pvr_copy_transform UpdateTexture=FAIL %s", SDL_GetError());
        success = false;
    }
    if (success && !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) {
        SDL_Log("pvr_copy_transform scale_mode=FAIL %s", SDL_GetError());
        success = false;
    }

    static const CopyCase cases[] = {
        { "copy",      { 112.0f,  48.0f, 128.0f, 128.0f },   0.0, SDL_FLIP_NONE,
          "TL red, TR green, BL blue, BR yellow" },
        { "rotate90",  { 400.0f,  48.0f, 128.0f, 128.0f },  90.0, SDL_FLIP_NONE,
          "TL blue, TR red, BL yellow, BR green" },
        { "flip_h",    { 112.0f, 280.0f, 128.0f, 128.0f },   0.0, SDL_FLIP_HORIZONTAL,
          "TL green, TR red, BL yellow, BR blue" },
        { "flip_v",    { 400.0f, 280.0f, 128.0f, 128.0f },   0.0, SDL_FLIP_VERTICAL,
          "TL blue, TR yellow, BL red, BR green" },
    };

    /* Match the other hardware gates: render several frames before capture so
     * the first vblank/page transition cannot leave a stale page in the shot. */
    for (int frame = 0; success && frame < 3; ++frame) {
        if (!SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 0.0f, 1.0f) ||
            !SDL_RenderClear(renderer)) {
            SDL_Log("pvr_copy_transform clear=FAIL %s", SDL_GetError());
            success = false;
            break;
        }
        for (size_t i = 0; success && i < SDL_arraysize(cases); ++i) {
            const CopyCase *test = &cases[i];
            if (!SDL_RenderTextureRotated(renderer, texture, NULL, &test->dst,
                                          test->angle, NULL, test->flip)) {
                SDL_Log("pvr_copy_transform %s=FAIL %s", test->name, SDL_GetError());
                success = false;
                break;
            }
            if (frame == 0) {
                SDL_Log("pvr_copy_transform %s queued; expected %s",
                        test->name, test->expected);
            }
        }
        if (success && !SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_copy_transform present frame=%d FAIL %s", frame, SDL_GetError());
            success = false;
        }
    }

#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (success && !pvr_phase0_capture("/pc/pvr_phase0_copy_transform.ppm")) {
        SDL_Log("pvr_copy_transform screenshot=FAIL %s", SDL_GetError());
        success = false;
    }
#endif

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_Log("pvr_copy_transform result=%s", success ? "PASS (visual check against orientation table)" : "FAIL");
    SDL_Quit();
    return success ? 0 : 1;
}
