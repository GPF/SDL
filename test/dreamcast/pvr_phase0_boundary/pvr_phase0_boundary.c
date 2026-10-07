/* Phase 0 boundary fixture: unsupported behavior must be rejected, not silently
 * approximated. Each case prints PASS when the documented rejection happens and
 * FAIL when the backend quietly does something else.
 *
 * Expected on hardware:
 *   case1 target_texture=PASS      (render targets are supported now that they are promoted)
 *   case2 lock_static_texture=PASS (lock requires streaming access)
 *   case3 blend_mod_rejected=PASS  (PVR-specific core gate rejects setter)
 *   case4 update_while_scene_open=PASS
 *   case5 update_after_present_allowed=PASS
 *   case6 non_triangle_geometry=PASS
 *   case7 non_finite_geometry=PASS
 *   pvr_boundary result=PASS cases=7
 *
 * Case 3 verifies the Dreamcast PVR-specific SDL core gate rejects MOD from the
 * setter while preserving SDL's required built-in blend modes for other
 * renderers. The backend also keeps a queue-time emission guard as defense in
 * depth.
 *
 * Case 7 is the -ffast-math proof: the release build folds isfinite() to true,
 * so the backend checks IEEE-754 exponent bits. A FAIL here means the guard was
 * optimized away.
 */

#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

static int failures = 0;

static void expect_failure(const char *label, bool succeeded)
{
    if (succeeded) {
        SDL_Log("case %s=FAIL (expected rejection, got success)", label);
        ++failures;
    } else {
        SDL_Log("case %s=PASS (%s)", label, SDL_GetError());
    }
}

static void expect_success(const char *label, bool succeeded)
{
    if (succeeded) {
        SDL_Log("case %s=PASS", label);
    } else {
        SDL_Log("case %s=FAIL (expected success, got %s)", label, SDL_GetError());
        ++failures;
    }
}

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
        SDL_Log("pvr_boundary SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0: boundary fixtures", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        SDL_Log("pvr_boundary renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!require_pvr(renderer, "pvr_boundary")) {
        SDL_Quit();
        return 1;
    }

    /* Case 1: render target. Targets are supported now that they are promoted, so this is
     * a positive check rather than a rejection boundary. */
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                            SDL_TEXTUREACCESS_TARGET, 320, 240);
    expect_success("target_texture_supported", target != NULL);
    if (target) {
        SDL_DestroyTexture(target);
    }

    /* Case 2: locking a static texture. */
    SDL_Texture *static_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                                     SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!static_texture) {
        SDL_Log("case static_texture=FAIL %s", SDL_GetError());
        ++failures;
    } else {
        void *pixels = NULL;
        int pitch = 0;
        const SDL_Rect full = {0, 0, 16, 16};
        expect_failure("lock_static_texture", SDL_LockTexture(static_texture, &full, &pixels, &pitch));
        SDL_DestroyTexture(static_texture);
    }

    /* Case 3: built-in blend modes remain required globally, but the
     * Dreamcast PVR renderer's capability callback rejects MOD at the setter. */
    expect_failure("blend_mod_rejected",
                   SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD));

    /* Case 4: texture update while the scene still references it. */
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                             SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!texture) {
        SDL_Log("case scene_texture=FAIL %s", SDL_GetError());
        ++failures;
    } else {
        static Uint16 pixels[16 * 16];
        const SDL_Rect full = {0, 0, 16, 16};
        const SDL_FRect src = {0.0f, 0.0f, 16.0f, 16.0f};
        const SDL_FRect dst = {100.0f, 100.0f, 32.0f, 32.0f};
        if (!SDL_UpdateTexture(texture, &full, pixels, 32)) {
            SDL_Log("case scene_texture_upload=FAIL %s", SDL_GetError());
            ++failures;
        } else {
            SDL_RenderClear(renderer);
            if (!SDL_RenderTexture(renderer, texture, &src, &dst)) {
                SDL_Log("case scene_texture_draw=FAIL %s", SDL_GetError());
                ++failures;
            }
            expect_failure("update_while_scene_open", SDL_UpdateTexture(texture, &full, pixels, 32));
        }
        /* Case 5: the same update after the scene has been flushed. */
        SDL_RenderPresent(renderer);
        expect_success("update_after_present_allowed", SDL_UpdateTexture(texture, &full, pixels, 32));
        SDL_DestroyTexture(texture);
    }

    /* Case 6: geometry that is not an independent triangle list. */
    const float xy[8] = {100, 100, 200, 100, 200, 200, 100, 200};
    SDL_FColor colors[4];
    for (int i = 0; i < 4; ++i) {
        colors[i].r = 1.0f; colors[i].g = 1.0f; colors[i].b = 1.0f; colors[i].a = 1.0f;
    }
    SDL_RenderClear(renderer);
    expect_failure("non_triangle_geometry", SDL_RenderGeometryRaw(renderer, NULL, xy, (int)sizeof(float) * 2, colors, (int)sizeof(SDL_FColor), NULL, 0, 4, NULL, 0, 0));

    /* Case 7: non-finite position. With -ffast-math the compiler may fold
     * isfinite() to true, so the backend checks IEEE-754 exponent bits; this
     * case is the proof that the check survives the actual release build. */
    float nan_xy[6] = {100.0f, 100.0f, 200.0f, 100.0f, 100.0f, 200.0f};
    const Uint32 nan_bits = 0x7fc00000u;
    float nan_value;

    SDL_memcpy(&nan_value, &nan_bits, sizeof(nan_value));
    nan_xy[3] = nan_value;
    SDL_RenderClear(renderer);
    expect_failure("non_finite_geometry", SDL_RenderGeometryRaw(renderer, NULL, nan_xy, (int)sizeof(float) * 2, colors, (int)sizeof(SDL_FColor), NULL, 0, 3, NULL, 0, 0));

    SDL_RenderPresent(renderer);
    if (!failures && !pvr_phase0_capture("/pc/pvr_phase0_boundary.ppm")) {
        SDL_Log("pvr_boundary screenshot=FAIL %s", SDL_GetError());
        ++failures;
    }
    SDL_Log("pvr_boundary result=%s cases=%d", failures ? "FAIL" : "PASS", 7);
    SDL_Quit();
    return failures ? 1 : 0;
}
