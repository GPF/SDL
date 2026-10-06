/* Phase 0 hardware gate 9: default SDL3 line method at scale 1.0.
 *
 * No SDL_HINT_RENDER_LINE_METHOD is set. SDL3 defaults to POINTS and expands
 * axis-aligned lines to rectangles and diagonal lines to Bresenham points.
 * The PVR backend rejects direct QueueDrawLines, so successful lines also
 * prove that SDL core took the default fallback path.
 */

#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

static bool require_pvr(SDL_Renderer *renderer)
{
    const char *name = SDL_GetRendererName(renderer);
    SDL_Log("pvr_default_lines renderer=%s", name ? name : "(null)");
    if (!name || SDL_strcmp(name, "Dreamcast PVR") != 0) {
        SDL_Log("pvr_default_lines native_renderer=FAIL expected 'Dreamcast PVR'");
        return false;
    }
    return true;
}

static bool draw_line(SDL_Renderer *renderer, float r, float g, float b, float a,
                      float x1, float y1, float x2, float y2)
{
    const SDL_FPoint points[] = { { x1, y1 }, { x2, y2 } };
    return SDL_SetRenderDrawColorFloat(renderer, r, g, b, a) &&
           SDL_RenderLines(renderer, points, SDL_arraysize(points));
}

static bool draw_scene(SDL_Renderer *renderer)
{
    const SDL_FPoint connected[] = {
        { 60.0f, 330.0f }, { 130.0f, 290.0f },
        { 200.0f, 340.0f }, { 270.0f, 300.0f }
    };
    const SDL_FRect zero_marker = { 350.0f, 335.0f, 9.0f, 9.0f };

    if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) ||
        !SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 0.0f, 1.0f) ||
        !SDL_RenderClear(renderer)) {
        return false;
    }

    /* Axis aligned segments exercise SDL's one-pixel rectangle expansion. */
    if (!draw_line(renderer, 1.0f, 0.0f, 0.0f, 1.0f, 40, 45, 210, 45) ||
        !draw_line(renderer, 0.0f, 1.0f, 0.0f, 1.0f, 235, 30, 235, 155) ||
        !draw_line(renderer, 0.0f, 0.7f, 1.0f, 1.0f, 210, 180, 40, 180) ||
        !draw_line(renderer, 1.0f, 1.0f, 0.0f, 1.0f, 285, 165, 285, 35)) {
        return false;
    }

    /* Shallow, steep, and reversed diagonals exercise Bresenham point output. */
    if (!draw_line(renderer, 0.2f, 0.4f, 1.0f, 1.0f, 40, 215, 210, 250) ||
        !draw_line(renderer, 1.0f, 0.0f, 1.0f, 1.0f, 330, 175, 355, 55) ||
        !draw_line(renderer, 1.0f, 0.5f, 0.0f, 1.0f, 430, 55, 405, 175)) {
        return false;
    }

    /* Isolate the zero-length line on a dark marker so its 1x1 result is
     * distinguishable from a no-op in the captured image. */
    if (!SDL_SetRenderDrawColorFloat(renderer, 0.18f, 0.18f, 0.18f, 1.0f) ||
        !SDL_RenderFillRect(renderer, &zero_marker) ||
        !draw_line(renderer, 1.0f, 1.0f, 1.0f, 1.0f, 354, 339, 354, 339)) {
        return false;
    }

    /* With blending enabled, a shared endpoint drawn twice would be brighter.
     * SDL core should assign each joint to one segment in the connected strip. */
    if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) ||
        SDL_SetRenderDrawColorFloat(renderer, 1.0f, 1.0f, 1.0f, 0.5f) == false ||
        !SDL_RenderLines(renderer, connected, SDL_arraysize(connected)) ||
        !SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE)) {
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_default_lines SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    if (SDL_GetHint(SDL_HINT_RENDER_LINE_METHOD) != NULL) {
        SDL_Log("pvr_default_lines line_method_hint=FAIL expected unset, got '%s'",
                SDL_GetHint(SDL_HINT_RENDER_LINE_METHOD));
        SDL_Quit();
        return 1;
    }
    SDL_Log("pvr_default_lines line_method_hint=unset (SDL3 default POINTS)");

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0: default lines", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        SDL_Log("pvr_default_lines renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!require_pvr(renderer)) {
        SDL_Quit();
        return 1;
    }

    bool success = SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    float scale_x = 0.0f;
    float scale_y = 0.0f;
    SDL_RendererLogicalPresentation logical_mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (!success || !SDL_GetRenderScale(renderer, &scale_x, &scale_y) ||
        scale_x != 1.0f || scale_y != 1.0f ||
        !SDL_GetRenderLogicalPresentation(renderer, NULL, NULL, &logical_mode) ||
        logical_mode != SDL_LOGICAL_PRESENTATION_DISABLED) {
        SDL_Log("pvr_default_lines view=FAIL scale=%.2f,%.2f logical=%d",
                scale_x, scale_y, (int)logical_mode);
        success = false;
    } else {
        SDL_Log("pvr_default_lines view=PASS scale=1.0,1.0 logical_presentation=disabled");
    }

    /* Render the same scene repeatedly to warm the PVR page, then capture the
     * final actual line frame rather than an empty settling frame. */
    for (int frame = 0; success && frame < 3; ++frame) {
        if (!draw_scene(renderer) || !SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_default_lines draw_present=FAIL frame=%d %s", frame, SDL_GetError());
            success = false;
        }
    }
    if (success) {
        SDL_Log("pvr_default_lines cases=PASS horizontal vertical shallow steep reversed zero_length connected");
    }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (success && !pvr_phase0_capture("/pc/pvr_phase0_default_lines.ppm")) {
        SDL_Log("pvr_default_lines screenshot=FAIL %s", SDL_GetError());
        success = false;
    }
#endif

    SDL_Log("pvr_default_lines exiting_in=3s");
    SDL_Delay(3000);
    SDL_DestroyRenderer(renderer);
    SDL_Log("pvr_default_lines result=%s (inspect rasterization)", success ? "PASS" : "FAIL");
    SDL_Quit();
    return success ? 0 : 1;
}
