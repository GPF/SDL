/* Phase 0 hardware smoke 1: clear + solid triangle with non-trivial float colors.
 *
 * The float-to-byte policy is nearest-integer after color_scale, clamped, never
 * wrapped. The values below are the golden table the backend must produce; the
 * screen is the confirmation.
 *
 * Golden packed colors (color_scale = 1.0):
 *   vertex A  0.25,0.50,0.75 ->  64,128,191
 *   vertex B  0.75,0.50,0.25 -> 191,128, 64
 *   vertex C  0.50,0.25,0.75 -> 128, 64,191
 *   clear     0.10,0.20,0.30 ->  26, 51, 77
 * Note the display quantizes to RGB565, so on-screen values are the 565
 * approximation of those bytes, not the bytes themselves.
 *
 * Expected geometry: triangle with apex at pixel (320,120) and base corners
 * (160,360) and (480,360). Smooth interpolation, no banding, no truncation.
 *
 * Failure signatures:
 *   - banding or flat facets -> interpolation/shading flag wrong
 *   - triangle offset by a whole viewport -> coordinate contract wrong
 *   - colors one LSB low -> nearest-integer policy became truncation
 */

#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

static const float vertex_color[3][3] = {
    {0.25f, 0.50f, 0.75f},
    {0.75f, 0.50f, 0.25f},
    {0.50f, 0.25f, 0.75f}
};

static const int golden[3][3] = {
    { 64, 128, 191},
    {191, 128,  64},
    {128,  64, 191}
};

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
        SDL_Log("pvr_smoke1 SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("PVR Phase 0: clear + solid triangle", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer) {
        SDL_Log("pvr_smoke1 renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!require_pvr(renderer, "pvr_smoke1")) {
        SDL_Quit();
        return 1;
    }

    for (int i = 0; i < 3; ++i) {
        SDL_Log("pvr_smoke1 golden[%d]=%d,%d,%d from %.2f,%.2f,%.2f",
                i, golden[i][0], golden[i][1], golden[i][2],
                vertex_color[i][0], vertex_color[i][1], vertex_color[i][2]);
    }

    const float xy[6] = {320.0f, 120.0f, 160.0f, 360.0f, 480.0f, 360.0f};
    SDL_FColor colors[3];

    for (int i = 0; i < 3; ++i) {
        colors[i].r = vertex_color[i][0];
        colors[i].g = vertex_color[i][1];
        colors[i].b = vertex_color[i][2];
        colors[i].a = 1.0f;
    }

    int failures = 0;
    for (int frame = 0; frame < 10; ++frame) {
        if (!SDL_SetRenderDrawColorFloat(renderer, 0.10f, 0.20f, 0.30f, 1.0f)) {
            SDL_Log("pvr_smoke1 drawcolor=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        if (!SDL_RenderClear(renderer)) {
            SDL_Log("pvr_smoke1 clear=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        if (!SDL_RenderGeometryRaw(renderer, NULL, xy, (int)sizeof(float) * 2, colors, (int)sizeof(SDL_FColor), NULL, 0, 3, NULL, 0, 0)) {
            SDL_Log("pvr_smoke1 geometry=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
        if (!SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_smoke1 present=FAIL %s", SDL_GetError());
            ++failures;
            break;
        }
    }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (!failures && !pvr_phase0_capture("/pc/pvr_phase0_clear_triangle.ppm")) {
        SDL_Log("pvr_smoke1 screenshot=FAIL %s", SDL_GetError());
        ++failures;
    }
#endif
    SDL_Log("pvr_smoke1 result=%s", failures ? "FAIL" : "PASS (visual check against golden table)");

    SDL_Quit();
    return failures ? 1 : 0;
}
