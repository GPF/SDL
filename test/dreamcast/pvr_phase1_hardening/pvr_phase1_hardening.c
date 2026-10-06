/* Guarded Phase 1 stress and VRAM ownership gates. */
#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

#define FAIL_ALLOC "SDL.renderer.dreamcast.pvr.test_fail_vram_allocation"
#define ALLOC_COUNT "SDL.renderer.dreamcast.pvr.test_vram_allocation_count"
#define FAIL_SCREEN "SDL.renderer.dreamcast.pvr.test_fail_screen_allocation"
#define P1C_TARGET_CAP 320
#define P1C_RETAIN_COUNT 3

#if defined(P1C_RUN_REPEAT_CONTROL)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_repeat_control"
#define P1C_RUN_NAME "repeat_control"
#elif defined(P1C_RUN_REPEAT_PRESSURE_1)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_repeat_pressure_1"
#define P1C_RUN_NAME "repeat_pressure_1"
#elif defined(P1C_RUN_REPEAT_PRESSURE_2)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_repeat_pressure_2"
#define P1C_RUN_NAME "repeat_pressure_2"
#elif defined(P1C_RUN_REPEAT_PRESSURE_3)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_repeat_pressure_3"
#define P1C_RUN_NAME "repeat_pressure_3"
#elif defined(P1C_RUN_DIAG3_CONTROL)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_diag3_control"
#define P1C_RUN_NAME "diag3_control"
#elif defined(P1C_RUN_DIAG3_PRESSURE)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_diag3_pressure"
#define P1C_RUN_NAME "diag3_pressure"
#elif defined(P1C_RUN_DIAG2_CONTROL)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_diag2_control"
#define P1C_RUN_NAME "diag2_control"
#elif defined(P1C_RUN_DIAG2_PRESSURE)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_diag2_pressure"
#define P1C_RUN_NAME "diag2_pressure"
#elif defined(P1C_RUN_CONTROL)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_canonical_control"
#define P1C_RUN_NAME "canonical_control"
#elif defined(P1C_RUN_PRESSURE_1)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_canonical_pressure_1"
#define P1C_RUN_NAME "canonical_pressure_1"
#elif defined(P1C_RUN_PRESSURE_2)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_canonical_pressure_2"
#define P1C_RUN_NAME "canonical_pressure_2"
#elif defined(P1C_RUN_PRESSURE_3)
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_canonical_pressure_3"
#define P1C_RUN_NAME "canonical_pressure_3"
#else
#define P1C_CAPTURE_BASE "/pc/pvr_phase1_hardening_p1c_canonical"
#define P1C_RUN_NAME "canonical"
#endif
#if defined(P1C_CAPTURE_FINAL_VBLANKS)
#define P1C_FINAL_PATH P1C_CAPTURE_BASE "_final_vblank_1.ppm"
#else
#define P1C_FINAL_PATH P1C_CAPTURE_BASE "_final.ppm"
#endif

static bool p1c_capture_stage(const char *stage)
{
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
#if defined(P1C_CAPTURE_FINAL_VBLANKS)
    if (SDL_strncmp(stage, "final_vblank_", 13) != 0) {
        return true;
    }
#endif
    char path[128];
    SDL_snprintf(path, sizeof(path), "%s_%s.ppm", P1C_CAPTURE_BASE, stage);
    SDL_Log("pvr_hardening capture_stage=%s path=%s", stage, path);
    return pvr_phase0_capture(path);
#else
    (void)stage;
    return true;
#endif
}

static bool color(SDL_Renderer *r, float red, float green, float blue)
{
    return SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE) &&
           SDL_SetRenderDrawColorFloat(r, red, green, blue, 1.0f);
}

static bool rect(SDL_Renderer *r, float x, float y, float w, float h)
{
    SDL_FRect area = { x, y, w, h };
    return SDL_RenderFillRect(r, &area);
}

static bool same_rect(const SDL_Rect *a, const SDL_Rect *b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

static bool target_marker(SDL_Renderer *r, SDL_Texture *t, float red, float green, float blue)
{
    /* Canonical P1-C texture content: clear the 64x64 target to black, then
     * draw an 18x18 solid marker at (4,4). */
    return SDL_SetRenderTarget(r, t) && color(r, 0, 0, 0) && SDL_RenderClear(r) &&
           color(r, red, green, blue) && rect(r, 4, 4, 18, 18) &&
           SDL_SetRenderTarget(r, NULL);
}

static bool provenance_frame(SDL_Renderer *r, SDL_Texture *retained,
                             SDL_Texture *reallocated, SDL_Texture *fresh,
                             const char *capture_path)
{
    /* Canonical P1-C final scene, on the logical screen with default viewport
     * and clipping disabled. Draw order is retained red, reallocated magenta,
     * fresh yellow, then an untextured cyan rectangle, all over black. */
    if (!color(r, 0, 0, 0) || !SDL_RenderClear(r) ||
        !SDL_RenderTexture(r, retained, NULL, &(SDL_FRect){ 32, 48, 96, 96 }) ||
        !SDL_RenderTexture(r, reallocated, NULL, &(SDL_FRect){ 168, 48, 96, 96 }) ||
        !SDL_RenderTexture(r, fresh, NULL, &(SDL_FRect){ 304, 48, 96, 96 }) ||
        !color(r, 0, 1, 1) || !rect(r, 440, 48, 96, 96) ||
        !SDL_RenderPresent(r)) {
        return false;
    }
    SDL_Log("pvr_hardening canonical_scene=PASS run=%s retained=%p reallocated=%p fresh=%p presents=1 capture=%s",
            P1C_RUN_NAME, (void *)retained, (void *)reallocated, (void *)fresh, capture_path);
#if defined(P1C_CAPTURE_FINAL_VBLANKS)
    for (int i = 1; i <= 3; ++i) {
        char stage[32];
        SDL_snprintf(stage, sizeof(stage), "final_vblank_%d", i);
        if (!p1c_capture_stage(stage)) return false;
    }
#else
    if (!p1c_capture_stage("final")) return false;
#endif
    (void)capture_path;
    return true;
}

static bool control_frame_without_exhaustion(SDL_Renderer *r)
{
    SDL_Texture *retained = NULL, *reallocated = NULL, *fresh = NULL;
    bool ok = false;

    retained = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, 64, 64);
    reallocated = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, 64, 64);
    fresh = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, 64, 64);
    if (!retained || !reallocated || !fresh ||
        !target_marker(r, retained, 1, 0, 0) ||
        !target_marker(r, reallocated, 1, 0, 1) ||
        !target_marker(r, fresh, 1, 1, 0) ||
        !p1c_capture_stage("pre_final")) goto done;
    ok = provenance_frame(r, retained, reallocated, fresh, P1C_FINAL_PATH);
done:
    if (fresh) SDL_DestroyTexture(fresh);
    if (reallocated) SDL_DestroyTexture(reallocated);
    if (retained) SDL_DestroyTexture(retained);
    return ok;
}

static bool failed_create_recovery(SDL_Renderer *r, Sint64 fail_at)
{
    SDL_Texture *bad, *good;
    SDL_PropertiesID p = SDL_CreateProperties();
    bool ok;
    SDL_SetNumberProperty(SDL_GetRendererProperties(r), ALLOC_COUNT, 0);
    SDL_SetNumberProperty(SDL_GetRendererProperties(r), FAIL_ALLOC, fail_at);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGB565);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, 64);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, 64);
    bad = SDL_CreateTextureWithProperties(r, p);
    SDL_DestroyProperties(p);
    if (bad) {
        SDL_DestroyTexture(bad);
        return false;
    }
    ok = SDL_GetNumberProperty(SDL_GetRendererProperties(r), ALLOC_COUNT, 0) == fail_at;
    good = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, 64, 64);
    if (!good) return false;
    ok = ok && target_marker(r, good, 1, 0, 1) && color(r, 0, 0, 0) &&
         SDL_RenderClear(r) && SDL_RenderTexture(r, good, NULL, &(SDL_FRect){ 40, 40, 128, 128 }) &&
         SDL_RenderPresent(r);
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (ok) ok = pvr_phase0_capture("/pc/pvr_phase1_hardening_recovery.ppm");
#endif
    SDL_DestroyTexture(good);
    return ok;
}

static bool vram_exhaustion_recovery(SDL_Renderer *r)
{
    SDL_Texture *targets[P1C_TARGET_CAP] = { NULL };
    SDL_Texture *replacement[P1C_TARGET_CAP] = { NULL };
    SDL_Texture *fresh = NULL;
    int count = 0;
    int replaced = 0;
    bool ok = false;

    for (int i = 0; i < P1C_RETAIN_COUNT; ++i) {
        targets[count] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565,
                                           SDL_TEXTUREACCESS_TARGET, 64, 64);
        if (!targets[count] ||
            !target_marker(r, targets[count], i == 0, i == 1, i == 2)) goto done;
        ++count;
    }
    while (count < P1C_TARGET_CAP) {
        SDL_Texture *texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565,
                                                  SDL_TEXTUREACCESS_TARGET, 64, 64);
        if (!texture) {
            const char *error = SDL_GetError();
            SDL_Log("pvr_hardening vram_exhausted=PASS successful=%d error=%s",
                    count, error);
            if (!error || !SDL_strstr(error, "VRAM")) goto done;
            break;
        }
        targets[count++] = texture;
    }
    if (count == P1C_TARGET_CAP) {
        SDL_Log("pvr_hardening vram_exhausted=FAIL safety_cap=%d", P1C_TARGET_CAP);
        goto done;
    }

    for (int i = P1C_RETAIN_COUNT; i < count; ++i) {
        SDL_DestroyTexture(targets[i]);
        targets[i] = NULL;
    }
    for (int i = 0; i < count - P1C_RETAIN_COUNT; ++i) {
        replacement[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565,
                                            SDL_TEXTUREACCESS_TARGET, 64, 64);
        if (!replacement[i]) goto done;
        ++replaced;
    }
    if (replaced != count - P1C_RETAIN_COUNT) goto done;
    SDL_Log("pvr_hardening vram_reallocation=PASS retained=%d freed=%d reallocated=%d",
            P1C_RETAIN_COUNT, count - P1C_RETAIN_COUNT, replaced);
    SDL_Log("pvr_hardening texture_ids run=%s retained0=%p retained1=%p retained2=%p reallocated0=%p",
            P1C_RUN_NAME, (void *)targets[0], (void *)targets[1],
            (void *)targets[2], (void *)replacement[0]);

    if (!target_marker(r, replacement[0], 1, 0, 1)) goto done;
    for (int i = 1; i < replaced; ++i) {
        SDL_DestroyTexture(replacement[i]);
        replacement[i] = NULL;
    }

    fresh = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, 64, 64);
    if (fresh) {
        SDL_Log("pvr_hardening texture_ids fresh=%p", (void *)fresh);
    }
    if (!fresh || !target_marker(r, fresh, 1, 1, 0) ||
        !p1c_capture_stage("pre_final") ||
        !provenance_frame(r, targets[0], replacement[0], fresh,
                          P1C_FINAL_PATH)) goto done;
    SDL_Log("pvr_hardening retained_reallocated_fresh=PASS");
    ok = true;

done:
    if (fresh) SDL_DestroyTexture(fresh);
    for (int i = 0; i < P1C_TARGET_CAP; ++i) {
        if (replacement[i]) SDL_DestroyTexture(replacement[i]);
        if (targets[i]) SDL_DestroyTexture(targets[i]);
    }
    return ok;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *r = NULL;
    SDL_Texture *a = NULL, *b = NULL;
    bool ok = false, alloc_ok = false;
#if defined(PVR_PHASE1_P1A_ONLY)
    bool run_stress = true, run_alloc = false;
#elif defined(PVR_PHASE1_P1B_ONLY)
    bool run_stress = false, run_alloc = true;
#elif defined(PVR_PHASE1_P1C_ONLY)
    bool run_stress = false, run_alloc = false;
#elif defined(PVR_PHASE1_P1C_CONTROL_ONLY)
    bool run_stress = false, run_alloc = false;
#else
    bool run_stress = true, run_alloc = true;
#endif
    (void)argc; (void)argv;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) goto done;
    window = SDL_CreateWindow("PVR Phase 1 hardening", 640, 480, 0);
    if (!window) goto done;
    if (run_alloc) {
        if (!SDL_SetHint(FAIL_SCREEN, "1") || SDL_CreateRenderer(window, "Dreamcast PVR")) goto done;
        SDL_Log("pvr_hardening screen_allocation_fail=PASS step=1");
        SDL_SetHint(FAIL_SCREEN, "2");
        if (SDL_CreateRenderer(window, "Dreamcast PVR")) goto done;
        SDL_Log("pvr_hardening screen_allocation_fail=PASS step=2");
        SDL_ResetHint(FAIL_SCREEN);
    }
    r = window ? SDL_CreateRenderer(window, "Dreamcast PVR") : NULL;
    if (!r || !SDL_GetRendererName(r) || SDL_strcmp(SDL_GetRendererName(r), "Dreamcast PVR")) goto done;
    SDL_Log("pvr_hardening renderer=Dreamcast PVR");
    /* Match the established Phase 1 capture warmup for the known first-frame
     * black capture behavior; this is setup before the measured stress pass. */
    {
        static const char *warmup_stage[] = { "warmup_1", "warmup_2", "warmup_3" };
    for (int i = 0; i < 3; ++i) {
        if (!color(r, 0, 0, 0) || !SDL_RenderClear(r) || !color(r, 1, 1, 1) ||
            !rect(r, 32, 32, 64, 64) || !SDL_RenderPresent(r) ||
            !p1c_capture_stage(warmup_stage[i])) goto done;
    }
    }
    SDL_Log("pvr_hardening capture_warmup=PASS frames=3");
 #if defined(PVR_PHASE1_P1C_ONLY)
    SDL_Log("pvr_hardening canonical_run=%s", P1C_RUN_NAME);
    ok = vram_exhaustion_recovery(r);
    goto done;
 #endif
 #if defined(PVR_PHASE1_P1C_CONTROL_ONLY)
    SDL_Log("pvr_hardening canonical_run=%s", P1C_RUN_NAME);
    ok = control_frame_without_exhaustion(r);
    SDL_Log("pvr_hardening no_exhaustion_control=%s", ok ? "PASS" : "FAIL");
    goto done;
 #endif
    if (run_stress) {
        a = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, 96, 64);
        b = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, 96, 64);
        if (!a || !b) goto done;
    }

    /* Establish persistent screen/A/B identity markers, then hammer transitions. */
    if (run_stress) {
    bool view_initialized[2] = { false, false };
    const SDL_Rect expected_view[2] = { { 1, 1, 90, 58 }, { 3, 2, 88, 55 } };
    const SDL_Rect expected_clip[2] = { { 2, 2, 80, 50 }, { 4, 3, 70, 40 } };
    if (!color(r, 0, 0, 0) || !SDL_RenderClear(r) || !color(r, 1, 0, 0) ||
        !rect(r, 12, 12, 32, 32) || !target_marker(r, a, 0, 1, 0) ||
        !target_marker(r, b, 0, 0, 1)) goto done;
    for (int i = 0; i < 64; ++i) {
        SDL_Texture *cycle[] = { a, b, NULL, b, a, NULL };
        for (int j = 0; j < 6; ++j) {
            if (!SDL_SetRenderTarget(r, cycle[j])) goto done;
            if (cycle[j]) {
                const int index = cycle[j] == a ? 0 : 1;
                SDL_Rect actual_view, actual_clip;
                if (!SDL_GetRenderViewport(r, &actual_view) ||
                    !SDL_GetRenderClipRect(r, &actual_clip)) goto done;
                if (view_initialized[index]) {
                    if (!same_rect(&actual_view, &expected_view[index]) ||
                        !SDL_RenderClipEnabled(r) ||
                        !same_rect(&actual_clip, &expected_clip[index])) goto done;
                } else {
                    if (actual_view.x != 0 || actual_view.y != 0 ||
                        actual_view.w != 96 || actual_view.h != 64 ||
                        SDL_RenderClipEnabled(r)) goto done;
                    if (!SDL_SetRenderViewport(r, &expected_view[index]) ||
                        !SDL_SetRenderClipRect(r, &expected_clip[index])) goto done;
                    view_initialized[index] = true;
                }
                if (!color(r, 1, 1, 0) || !rect(r, 4, 32, 8, 8)) goto done;
            } else {
                SDL_Rect actual_view;
                if (!SDL_GetRenderViewport(r, &actual_view) || actual_view.x != 0 ||
                    actual_view.y != 0 || actual_view.w != 640 || actual_view.h != 480 ||
                    SDL_RenderClipEnabled(r) || !color(r, 1, 0, 0) ||
                    !rect(r, 12, 12, 8, 8)) goto done;
            }
        }
        if (!SDL_SetRenderTarget(r, NULL) ||
            !SDL_RenderTexture(r, a, NULL, &(SDL_FRect){ 80, 80, 96, 64 }) ||
            !SDL_RenderTexture(r, b, NULL, &(SDL_FRect){ 200, 80, 96, 64 })) goto done;
    }
    if (!SDL_RenderPresent(r)) goto done;
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (!pvr_phase0_capture("/pc/pvr_phase1_hardening_stress.ppm")) goto done;
#endif
    SDL_Log("pvr_hardening switch_stress=PASS cycles=64 destinations=screen,A,B");
    }

    if (run_alloc) {
    alloc_ok = failed_create_recovery(r, 1) && failed_create_recovery(r, 2);
    if (!alloc_ok) goto done;
    SDL_Log("pvr_hardening allocation_fail_recovery=PASS steps=1,2");
    if (!color(r, 0, 0, 0) || !SDL_RenderClear(r) || !SDL_RenderPresent(r)) goto done;
    SDL_Log("pvr_hardening screen_after_failure=PASS");
    }
    ok = true;
done:
    SDL_Log("pvr_hardening result=%s", ok ? "PASS" : "FAIL");
    if (b) SDL_DestroyTexture(b);
    if (a) SDL_DestroyTexture(a);
    if (r) SDL_DestroyRenderer(r);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
