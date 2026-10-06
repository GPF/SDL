/* Phase 1 hardware gate: RGB565 target preservation and SDL3 ordering. */
#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

static bool require_pvr(SDL_Renderer *renderer)
{
    const char *name = SDL_GetRendererName(renderer);
    SDL_Log("pvr_targets renderer=%s", name ? name : "(null)");
    return name && SDL_strcmp(name, "Dreamcast PVR") == 0;
}

static bool color(SDL_Renderer *r, float red, float green, float blue)
{
    return SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE) &&
           SDL_SetRenderDrawColorFloat(r, red, green, blue, 1.0f);
}

static bool rect(SDL_Renderer *r, float x, float y, float w, float h)
{
    const SDL_FRect area = { x, y, w, h };
    return SDL_RenderFillRect(r, &area);
}

static bool capture_page(SDL_Renderer *r, const char *path)
{
    if (!SDL_RenderPresent(r)) {
        SDL_Log("pvr_targets present=FAIL %s", SDL_GetError());
        return false;
    }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (!pvr_phase0_capture(path)) {
        SDL_Log("pvr_targets capture=FAIL %s", SDL_GetError());
        return false;
    }
#else
    (void)path;
#endif
    return true;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *a = NULL, *b = NULL, *npot = NULL, *rollback_target = NULL;
    SDL_Texture *unsupported_target = NULL;
    SDL_PropertiesID target_props = 0;
    bool ok = false;
    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_targets SDL_Init=FAIL %s", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("PVR Phase 1 targets", 640, 480, 0);
    renderer = window ? SDL_CreateRenderer(window, "Dreamcast PVR") : NULL;
    if (!renderer || !require_pvr(renderer)) {
        SDL_Log("pvr_targets renderer=FAIL %s", SDL_GetError());
        goto done;
    }
    unsupported_target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                           SDL_TEXTUREACCESS_TARGET, 32, 32);
    if (unsupported_target) {
        SDL_Log("pvr_targets unsupported_target_format=FAIL accepted ARGB1555");
        goto done;
    }
    SDL_ClearError();
    SDL_Log("pvr_targets unsupported_target_format=PASS rejected ARGB1555");
    target_props = SDL_CreateProperties();
    if (!target_props ||
        !SDL_SetNumberProperty(target_props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                               SDL_PIXELFORMAT_RGB565) ||
        !SDL_SetNumberProperty(target_props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,
                               SDL_TEXTUREACCESS_TARGET) ||
        !SDL_SetNumberProperty(target_props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, 80) ||
        !SDL_SetNumberProperty(target_props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, 64) ||
        !SDL_SetFloatProperty(target_props, SDL_PROP_TEXTURE_CREATE_SDR_WHITE_POINT_FLOAT,
                              2.0f)) {
        SDL_Log("pvr_targets rollback_target_properties=FAIL %s", SDL_GetError());
        goto done;
    }
    rollback_target = SDL_CreateTextureWithProperties(renderer, target_props);
    SDL_DestroyProperties(target_props);
    target_props = 0;
    a = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                          SDL_TEXTUREACCESS_TARGET, 80, 64);
    b = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                          SDL_TEXTUREACCESS_TARGET, 64, 48);
    npot = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                             SDL_TEXTUREACCESS_TARGET, 127, 93);
    if (!rollback_target || !a || !b || !npot) {
        SDL_Log("pvr_targets target_create=FAIL %s", SDL_GetError());
        goto done;
    }

    for (int frame = 0; frame < 3; ++frame) {
        if (!color(renderer, 0, 0, 0) || !SDL_RenderClear(renderer) ||
            !color(renderer, 1, 1, 1) || !rect(renderer, 32, 32, 64, 64) ||
            !SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_targets warmup=%d FAIL %s", frame, SDL_GetError());
            goto done;
        }
    }
    SDL_Log("pvr_targets warmup=PASS frames=3");

    /* Exercise SDL core rollback when the backend rejects a switch. */
#ifdef PVR_PHASE1_TEST_INJECTION
    if (!SDL_SetBooleanProperty(SDL_GetRendererProperties(renderer),
                                "SDL.renderer.dreamcast.pvr.test_fail_next_target_switch",
                                true)) {
        SDL_Log("pvr_targets failure_injection_setup=FAIL %s", SDL_GetError());
        goto done;
    }
    SDL_ClearError();
    if (SDL_SetRenderTarget(renderer, rollback_target) ||
        SDL_GetRenderTarget(renderer) != NULL ||
        !SDL_GetError()[0]) {
        SDL_Log("pvr_targets failed_switch_rollback=FAIL target=%p error=%s",
                (void *)SDL_GetRenderTarget(renderer), SDL_GetError());
        goto done;
    }
    SDL_ClearError();
    SDL_Log("pvr_targets failed_switch_rollback=PASS target=screen");
#else
    SDL_Log("pvr_targets failed_switch_rollback=SKIP injection disabled");
#endif

    /* Screen S1 survives an excursion to A; S2 is drawn after returning. */
    if (!color(renderer, 0, 0, 0) || !SDL_RenderClear(renderer) ||
        !color(renderer, 1, 0, 0) || !rect(renderer, 40, 40, 100, 100) ||
        !SDL_SetRenderTarget(renderer, a) || !color(renderer, 0, 1, 0) ||
        !SDL_RenderClear(renderer) || !SDL_SetRenderTarget(renderer, NULL) ||
        !color(renderer, 1, 1, 0) || !rect(renderer, 300, 100, 100, 100) ||
        !color(renderer, 0.5f, 0.5f, 0.5f) || !rect(renderer, 540, 40, 40, 40) ||
        !capture_page(renderer, "/pc/pvr_phase1_targets_page1.ppm")) {
        SDL_Log("pvr_targets screen_target_screen=FAIL %s", SDL_GetError());
        goto done;
    }
    SDL_Log("pvr_targets screen_target_screen=PASS");

    /* A -> B -> A preserves both targets; then sample both on screen. */
    if (!color(renderer, 0, 0, 0) || !SDL_RenderClear(renderer) ||
        !SDL_SetRenderTarget(renderer, a) || !color(renderer, 0, 0, 1) ||
        !SDL_RenderClear(renderer) || !color(renderer, 0, 1, 0) ||
        !rect(renderer, 8, 8, 20, 20) || !SDL_SetRenderTarget(renderer, b) ||
        !color(renderer, 1, 0, 1) || !SDL_RenderClear(renderer) ||
        !color(renderer, 1, 1, 0) || !rect(renderer, 8, 8, 20, 20) ||
        !SDL_SetRenderTarget(renderer, a) || !color(renderer, 1, 1, 1) ||
        !rect(renderer, 48, 8, 20, 20) || !SDL_SetRenderTarget(renderer, NULL) ||
        !color(renderer, 0, 0, 0) || !SDL_RenderClear(renderer) ||
        !SDL_SetTextureScaleMode(a, SDL_SCALEMODE_NEAREST) ||
        !SDL_SetTextureScaleMode(b, SDL_SCALEMODE_NEAREST) ||
        !SDL_RenderTexture(renderer, a, NULL, &(SDL_FRect){ 80, 160, 240, 192 }) ||
        !SDL_RenderTexture(renderer, b, NULL, &(SDL_FRect){ 360, 160, 192, 144 }) ||
        !capture_page(renderer, "/pc/pvr_phase1_targets_page2.ppm")) {
        SDL_Log("pvr_targets target_a_b_a=FAIL %s", SDL_GetError());
        goto done;
    }
    SDL_Log("pvr_targets target_a_b_a_sample=PASS");

    /* NPOT extreme texels expose target UV endpoint errors. */
    if (!SDL_SetRenderTarget(renderer, npot) || !color(renderer, 0, 0, 0) ||
        !SDL_RenderClear(renderer) || !color(renderer, 1, 0, 0) ||
        !rect(renderer, 0, 0, 127, 2) || !color(renderer, 0, 1, 0) ||
        !rect(renderer, 125, 0, 2, 93) || !color(renderer, 0, 0, 1) ||
        !rect(renderer, 0, 91, 125, 2) || !color(renderer, 1, 1, 1) ||
        !rect(renderer, 0, 2, 2, 89) || !SDL_SetRenderTarget(renderer, NULL) ||
        !color(renderer, 0, 0, 0) || !SDL_RenderClear(renderer) ||
        !SDL_SetTextureScaleMode(npot, SDL_SCALEMODE_NEAREST) ||
        !SDL_RenderTexture(renderer, npot, NULL, &(SDL_FRect){ 230, 180, 310, 190 }) ||
        !capture_page(renderer, "/pc/pvr_phase1_targets_page3.ppm")) {
        SDL_Log("pvr_targets npot_edges=FAIL %s", SDL_GetError());
        goto done;
    }
    SDL_Log("pvr_targets npot_edges=PASS");

    /* SDL3 rejects present on a selected target before flushing its queue. */
    if (!SDL_SetRenderTarget(renderer, npot) || !color(renderer, 0, 0, 1) ||
        !SDL_RenderClear(renderer)) {
        SDL_Log("pvr_targets selected_target_setup=FAIL %s", SDL_GetError());
        goto done;
    }
    SDL_ClearError();
    if (SDL_RenderPresent(renderer) || SDL_GetRenderTarget(renderer) != npot ||
        !SDL_GetError()[0]) {
        SDL_Log("pvr_targets selected_target_present_rejection=FAIL");
        goto done;
    }
    SDL_Log("pvr_targets selected_target_present_rejected=PASS");
    if (!color(renderer, 1, 0, 0) || !rect(renderer, 8, 8, 8, 8) ||
        !SDL_SetRenderTarget(renderer, NULL) || !color(renderer, 0, 0, 0) ||
        !SDL_RenderClear(renderer) ||
        !SDL_RenderTexture(renderer, npot, NULL, &(SDL_FRect){ 230, 180, 310, 190 }) ||
        !capture_page(renderer, "/pc/pvr_phase1_targets_page4.ppm")) {
        SDL_Log("pvr_targets target_usable_after_rejection=FAIL %s", SDL_GetError());
        goto done;
    }
    SDL_Log("pvr_targets target_usable_after_rejection=PASS");
    ok = true;

done:
    SDL_Log("pvr_targets result=%s", ok ? "PASS (inspect pages 1-4)" : "FAIL");
    if (npot) SDL_DestroyTexture(npot);
    if (b) SDL_DestroyTexture(b);
    if (a) SDL_DestroyTexture(a);
    if (rollback_target) SDL_DestroyTexture(rollback_target);
    if (unsupported_target) SDL_DestroyTexture(unsupported_target);
    if (target_props) SDL_DestroyProperties(target_props);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
