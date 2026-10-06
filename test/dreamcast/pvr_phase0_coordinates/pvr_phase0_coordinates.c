/* Phase 0 hardware smoke 3: coordinate transform.
 *
 * Logical presentation 320x240 on a 640x480 output, so current_scale is 2.0.
 * Expected pixel positions on hardware:
 *   red    logical (  0,  0) -> pixel (  0,  0)
 *   green  logical (310,230) -> pixel (620,460)
 *   blue   logical (160,120) -> pixel (320,240)
 *   yellow logical (100,100) viewport origin -> pixel (200,200)
 *   cyan   viewport-relative (50,50) inside clip -> pixel (200+100, 200+100) = (300,300)
 *   nothing from the magenta rect at viewport-relative (95,95): fully clipped
 *
 * Failure signatures:
 *   - blocks at half distance -> current_scale applied twice, or not applied
 *   - yellow block at (100,100) instead of (200,200) -> pixel viewport origin
 *     not added at emission
 *   - magenta block visible -> clip rect treated as framebuffer-relative
 *     instead of viewport-relative
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
        SDL_Log("pvr_smoke3 SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0: coordinate transform", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        SDL_Log("pvr_smoke3 renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!require_pvr(renderer, "pvr_smoke3")) {
        SDL_Quit();
        return 1;
    }

    if (!SDL_SetRenderLogicalPresentation(renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        SDL_Log("pvr_smoke3 logical=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const SDL_Rect viewport = {100, 100, 100, 100};
    const SDL_Rect clip = {50, 50, 20, 20};
    const SDL_FRect top_left = {0.0f, 0.0f, 10.0f, 10.0f};
    const SDL_FRect bottom_right = {310.0f, 230.0f, 10.0f, 10.0f};
    const SDL_FRect viewport_origin = {0.0f, 0.0f, 10.0f, 10.0f};
    const SDL_FRect inside = {50.0f, 50.0f, 10.0f, 10.0f};
    const SDL_FRect outside = {95.0f, 95.0f, 10.0f, 10.0f};

    int failures = 0;
    for (int frame = 0; frame < 10; ++frame) {
        SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        if (!SDL_RenderClear(renderer)) {
            SDL_Log("pvr_smoke3 clear=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }

        SDL_SetRenderDrawColorFloat(renderer, 1.0f, 0.0f, 0.0f, 1.0f);
        SDL_RenderFillRect(renderer, &top_left);
        SDL_SetRenderDrawColorFloat(renderer, 0.0f, 1.0f, 0.0f, 1.0f);
        SDL_RenderFillRect(renderer, &bottom_right);
        SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 1.0f, 1.0f);
        SDL_RenderPoint(renderer, 160.0f, 120.0f);

        if (!SDL_SetRenderViewport(renderer, &viewport)) {
            SDL_Log("pvr_smoke3 viewport=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        SDL_SetRenderDrawColorFloat(renderer, 1.0f, 1.0f, 0.0f, 1.0f);
        SDL_RenderFillRect(renderer, &viewport_origin);

        if (!SDL_SetRenderClipRect(renderer, &clip)) {
            SDL_Log("pvr_smoke3 clip=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        SDL_SetRenderDrawColorFloat(renderer, 0.0f, 1.0f, 1.0f, 1.0f);
        SDL_RenderFillRect(renderer, &inside);
        SDL_SetRenderDrawColorFloat(renderer, 1.0f, 0.0f, 1.0f, 1.0f);
        SDL_RenderFillRect(renderer, &outside);

        if (!SDL_SetRenderClipRect(renderer, NULL) || !SDL_SetRenderViewport(renderer, NULL)) {
            SDL_Log("pvr_smoke3 reset=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        if (!SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_smoke3 present=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
    }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (!failures && !pvr_phase0_capture("/pc/pvr_phase0_coordinates.ppm")) {
        SDL_Log("pvr_smoke3 screenshot=FAIL %s", SDL_GetError());
        ++failures;
    }
#endif
    SDL_Log("pvr_smoke3 result=%s", failures ? "FAIL" : "PASS (visual check against expected pixels)");

    SDL_Quit();
    return failures ? 1 : 0;
}
