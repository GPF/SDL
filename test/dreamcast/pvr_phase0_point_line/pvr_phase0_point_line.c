/* Phase 0 hardware gate 7: direct scale-1 points and explicit line rejection.
 *
 * SDL3 selects QueueDrawPoints only when current_scale is exactly 1.0. The
 * line-method hint is set before renderer creation so SDL3 selects the
 * QueueDrawLines path, which Phase 0 rejects synchronously.
 */

#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

static bool require_pvr(SDL_Renderer *renderer)
{
    const char *name = SDL_GetRendererName(renderer);
    SDL_Log("pvr_point_line renderer=%s", name ? name : "(null)");
    if (!name || SDL_strcmp(name, "Dreamcast PVR") != 0) {
        SDL_Log("pvr_point_line native_renderer=FAIL expected 'Dreamcast PVR'");
        return false;
    }
    return true;
}

static void make_point_block(SDL_FPoint points[25], float x, float y)
{
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 5; ++col) {
            points[row * 5 + col].x = x + (float)(col * 2);
            points[row * 5 + col].y = y + (float)(row * 2);
        }
    }
}

int main(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    /* SDL core snapshots this hint in CreateRenderer. */
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "2");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_point_line SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0: points and lines", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        SDL_Log("pvr_point_line renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!require_pvr(renderer)) {
        SDL_Quit();
        return 1;
    }

    bool success = true;
    if (!SDL_SetRenderScale(renderer, 1.0f, 1.0f)) {
        SDL_Log("pvr_point_line scale=FAIL %s", SDL_GetError());
        success = false;
    }
    float scale_x = 0.0f;
    float scale_y = 0.0f;
    SDL_RendererLogicalPresentation logical_mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (success && (!SDL_GetRenderScale(renderer, &scale_x, &scale_y) ||
                    scale_x != 1.0f || scale_y != 1.0f)) {
        SDL_Log("pvr_point_line scale=FAIL expected 1.0,1.0 got %.2f,%.2f",
                scale_x, scale_y);
        success = false;
    }
    if (success && (!SDL_GetRenderLogicalPresentation(renderer, NULL, NULL, &logical_mode) ||
                    logical_mode != SDL_LOGICAL_PRESENTATION_DISABLED)) {
        SDL_Log("pvr_point_line logical_presentation=FAIL expected disabled");
        success = false;
    }
    if (success) {
        SDL_Log("pvr_point_line line_method=2 scale=%.1f,%.1f logical_presentation=disabled",
                scale_x, scale_y);
    }

    SDL_FPoint red_points[25];
    SDL_FPoint green_points[25];
    SDL_FPoint blue_points[25];
    make_point_block(red_points, 100.0f, 100.0f);
    make_point_block(green_points, 120.0f, 100.0f);
    make_point_block(blue_points, 140.0f, 100.0f);

    if (success &&
        (!SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 0.0f, 1.0f) ||
         !SDL_RenderClear(renderer))) {
        SDL_Log("pvr_point_line clear=FAIL %s", SDL_GetError());
        success = false;
    }
    if (success &&
        (!SDL_SetRenderDrawColorFloat(renderer, 1.0f, 0.0f, 0.0f, 1.0f) ||
         !SDL_RenderPoints(renderer, red_points, SDL_arraysize(red_points)) ||
         !SDL_SetRenderDrawColorFloat(renderer, 0.0f, 1.0f, 0.0f, 1.0f) ||
         !SDL_RenderPoints(renderer, green_points, SDL_arraysize(green_points)) ||
         !SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 1.0f, 1.0f) ||
         !SDL_RenderPoints(renderer, blue_points, SDL_arraysize(blue_points)))) {
        SDL_Log("pvr_point_line points_scale1=FAIL %s", SDL_GetError());
        success = false;
    } else if (success) {
        SDL_Log("pvr_point_line points_scale1=PASS");
    }

    /* Method 2 bypasses the default line-to-rectangle path and must reach the
     * backend's queue-time rejection callback. */
    const SDL_FPoint line[] = { { 100.0f, 140.0f }, { 180.0f, 140.0f } };
    if (success) {
        const bool line_succeeded = SDL_RenderLines(renderer, line, SDL_arraysize(line));
        if (line_succeeded) {
            SDL_Log("pvr_point_line line_method2_rejected=FAIL expected queue rejection");
            success = false;
        } else {
            SDL_Log("pvr_point_line line_method2_rejected=PASS (%s)", SDL_GetError());
        }
    }

    /* Use multiple presents to avoid the initial stale-page capture seen in
     * the first Gate 6 run. This does not change renderer presentation code. */
    for (int frame = 0; success && frame < 3; ++frame) {
        if (frame > 0) {
            if (!SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 0.0f, 1.0f) ||
                !SDL_RenderClear(renderer) ||
                !SDL_SetRenderDrawColorFloat(renderer, 1.0f, 0.0f, 0.0f, 1.0f) ||
                !SDL_RenderPoints(renderer, red_points, SDL_arraysize(red_points)) ||
                !SDL_SetRenderDrawColorFloat(renderer, 0.0f, 1.0f, 0.0f, 1.0f) ||
                !SDL_RenderPoints(renderer, green_points, SDL_arraysize(green_points)) ||
                !SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.0f, 1.0f, 1.0f) ||
                !SDL_RenderPoints(renderer, blue_points, SDL_arraysize(blue_points))) {
                SDL_Log("pvr_point_line repeat_draw=FAIL %s", SDL_GetError());
                success = false;
                break;
            }
        }
        if (!SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_point_line present frame=%d FAIL %s", frame, SDL_GetError());
            success = false;
        }
    }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (success && !pvr_phase0_capture("/pc/pvr_phase0_point_line.ppm")) {
        SDL_Log("pvr_point_line screenshot=FAIL %s", SDL_GetError());
        success = false;
    }
#endif

    SDL_DestroyRenderer(renderer);
    SDL_Log("pvr_point_line result=%s", success ? "PASS (visual point check and line rejection)" : "FAIL");
    SDL_Quit();
    return success ? 0 : 1;
}
