/*
 * Performance measurement for the native Dreamcast PVR renderer with ordered
 * render targets.
 *
 * Ordered render targets are always enabled. This test measures the frame
 * cost of both screen-only workloads (A-D) and target-texture workloads (E-F).
 *
 * Measures end-to-end frame cost (SDL command execution + scene finish +
 * SDL_RenderPresent + pvr_wait_render_done + vsync) using the KOS high-
 * resolution monotonic timer.
 */

#include <SDL.h>
#include <kos.h>
#include <kos/timer.h>
#include <dc/pvr/pvr_mem.h>

#include <stdio.h>
#include <string.h>

#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
#define DC_PVR_PERFORMANCE_HAS_TARGETS 1
#endif

/* ------------------------------------------------------------------ */
/* Configuration                                                      */
/* ------------------------------------------------------------------ */

#define OUTPUT_W        640
#define OUTPUT_H        480
#define WARMUP_FRAMES   60
#define MEASURED_FRAMES 300

/* Display hold time in milliseconds */
#ifndef PAGE_HOLD_MS
#define PAGE_HOLD_MS 12000
#endif

/* TA-capacity guard — stay well below the validated boundary */
#define MAX_DRAW_PER_FRAME  400

/* ------------------------------------------------------------------ */
/* Hardware-screenshot guard                                          */
/* ------------------------------------------------------------------ */

#ifdef DC_PVR_RENDER_TARGET_PERFORMANCE_HARDWARE_SCREENSHOT
static const char *const ordinary_capture =
    "/pc/pvr_render_target_performance-ordinary.ppm";
static const char *const prototype_capture =
    "/pc/pvr_render_target_performance-prototype.ppm";
static const char *const ordered_capture =
    "/pc/pvr_render_target_performance-ordered.ppm";
#endif

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */

static int g_checks;
static int g_failures;

/* Timer resolution query result */
static const char *g_timer_desc;

/* Timer resolution in microseconds (1 = microsecond granularity) */
static uint64_t g_timer_resolution_us;

/* ------------------------------------------------------------------ */
/* Workload function pointer type                                     */
/* ------------------------------------------------------------------ */

/* Workload signature: renderer, texture (may be NULL), durations array. */
typedef int (*workload_fn)(SDL_Renderer *renderer,
                           SDL_Texture *tex,
                           uint64_t *durations);

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static int fail_message(const char *operation)
{
    ++g_failures;
    printf("FAIL: %s: %s\n", operation, SDL_GetError());
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return fail_message(#call); } while (0)

static void assertion_check(SDL_bool condition, const char *description)
{
    ++g_checks;
    if (condition) {
        printf("  CHECK PASS: %s\n", description);
    } else {
        ++g_failures;
        printf("  CHECK FAIL: %s\n", description);
    }
}

/* ------------------------------------------------------------------ */
/* Workload helpers                                                   */
/* ------------------------------------------------------------------ */

static int clear_to(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderClear(renderer));
    return 0;
}

static int fill_rect(SDL_Renderer *renderer,
                     int x, int y, int w, int h,
                     Uint8 r, Uint8 g, Uint8 b, Uint8 a,
                     SDL_BlendMode blend)
{
    SDL_Rect rect = { x, y, w, h };
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, blend));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, a));
    REQUIRE(SDL_RenderFillRect(renderer, &rect));
    return 0;
}

static int present_and_vblank(SDL_Renderer *renderer)
{
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (*SDL_GetError())
        return fail_message("SDL_RenderPresent");
    vid_waitvbl();
    return 0;
}

/* Create one RGB565 static texture (128x128, POT backing 128x128). */
static SDL_Texture *create_test_texture(SDL_Renderer *renderer)
{
    SDL_Texture *tex;
    int x, y;
    Uint16 *pixels;

    tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                            SDL_TEXTUREACCESS_STATIC, 128, 128);
    if (!tex)
        return NULL;

    pixels = (Uint16 *)SDL_malloc(128 * 128 * sizeof(*pixels));
    if (!pixels) {
        SDL_DestroyTexture(tex);
        return NULL;
    }
    for (y = 0; y < 128; y++) {
        for (x = 0; x < 128; x++) {
            Uint16 val;
            if ((x + y) & 1) {
                val = 0x001F;          /* blue */
            } else {
                val = 0x0400;          /* green */
            }
            pixels[y * 128 + x] = val;
        }
    }
    if (SDL_UpdateTexture(tex, NULL, pixels, 128 * (int)sizeof(*pixels)) < 0) {
        SDL_free(pixels);
        SDL_DestroyTexture(tex);
        return NULL;
    }
    SDL_free(pixels);
    return tex;
}

/* ------------------------------------------------------------------ */
/* Workloads A-D (identical for both builds)                          */
/* ------------------------------------------------------------------ */

/* Workload A: clear-only */
static int workload_clear(SDL_Renderer *renderer,
                          SDL_Texture *tex,
                          uint64_t *durations)
{
    int i;
    uint64_t t0, t1;

    (void)tex;
    for (i = 0; i < WARMUP_FRAMES; i++) {
        REQUIRE(clear_to(renderer, 8, 8, 16));
        REQUIRE(present_and_vblank(renderer));
    }

    for (i = 0; i < MEASURED_FRAMES; i++) {
        SDL_ClearError();
        t0 = timer_us_gettime64();
        REQUIRE(clear_to(renderer, 8, 8, 16));
        SDL_RenderPresent(renderer);
        vid_waitvbl();
        t1 = timer_us_gettime64();
        if (*SDL_GetError()) return fail_message("SDL_RenderPresent");
        durations[i] = t1 > t0 ? t1 - t0 : 0;
    }
    return 0;
}

/* Workload B: moderate untextured geometry */
static int workload_untextured(SDL_Renderer *renderer,
                               SDL_Texture *tex,
                               uint64_t *durations)
{
    int i, j;
    uint64_t t0, t1;

    (void)tex;
    for (i = 0; i < WARMUP_FRAMES; i++) {
        REQUIRE(clear_to(renderer, 8, 8, 16));
        for (j = 0; j < 100; j++) {
            int x = (j * 7) % (OUTPUT_W - 20);
            int y = (j * 13) % (OUTPUT_H - 20);
            int w = 10 + (j % 30);
            int h = 10 + (j % 20);
            Uint8 r = (j * 3) & 0xFF;
            Uint8 g = (j * 7) & 0xFF;
            Uint8 b = (j * 11) & 0xFF;
            REQUIRE(fill_rect(renderer, x, y, w, h, r, g, b, 255,
                              (j & 1) ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND));
        }
        REQUIRE(present_and_vblank(renderer));
    }

    for (i = 0; i < MEASURED_FRAMES; i++) {
        SDL_ClearError();
        t0 = timer_us_gettime64();
        REQUIRE(clear_to(renderer, 8, 8, 16));
        for (j = 0; j < 100; j++) {
            int x = (j * 7) % (OUTPUT_W - 20);
            int y = (j * 13) % (OUTPUT_H - 20);
            int w = 10 + (j % 30);
            int h = 10 + (j % 20);
            Uint8 r = (j * 3) & 0xFF;
            Uint8 g = (j * 7) & 0xFF;
            Uint8 b = (j * 11) & 0xFF;
            REQUIRE(fill_rect(renderer, x, y, w, h, r, g, b, 255,
                              (j & 1) ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND));
        }
        SDL_RenderPresent(renderer);
        vid_waitvbl();
        t1 = timer_us_gettime64();
        if (*SDL_GetError()) return fail_message("SDL_RenderPresent");
        durations[i] = t1 > t0 ? t1 - t0 : 0;
    }
    return 0;
}

/* Workload C: moderate textured geometry */
static int workload_textured(SDL_Renderer *renderer,
                             SDL_Texture *tex,
                             uint64_t *durations)
{
    int i, j;
    uint64_t t0, t1;
    SDL_Rect dst;
    SDL_Rect src = { 0, 0, 64, 64 };

    if (!tex)
        return fail_message("workload_textured: NULL texture");

    for (i = 0; i < WARMUP_FRAMES; i++) {
        REQUIRE(clear_to(renderer, 8, 8, 16));
        for (j = 0; j < 200; j++) {
            dst.x = (j * 11) % (OUTPUT_W - 64);
            dst.y = (j * 17) % (OUTPUT_H - 64);
            dst.w = 32 + (j % 32);
            dst.h = 32 + (j % 32);
            REQUIRE(SDL_SetTextureColorMod(tex, (j * 5) & 0xFF,
                                           (j * 13) & 0xFF,
                                           (j * 19) & 0xFF));
            REQUIRE(SDL_SetTextureAlphaMod(tex, 128 + (j & 0x7F)));
            REQUIRE(SDL_RenderCopy(renderer, tex, &src, &dst));
        }
        REQUIRE(present_and_vblank(renderer));
    }

    for (i = 0; i < MEASURED_FRAMES; i++) {
        SDL_ClearError();
        t0 = timer_us_gettime64();
        REQUIRE(clear_to(renderer, 8, 8, 16));
        for (j = 0; j < 200; j++) {
            dst.x = (j * 11) % (OUTPUT_W - 64);
            dst.y = (j * 17) % (OUTPUT_H - 64);
            dst.w = 32 + (j % 32);
            dst.h = 32 + (j % 32);
            REQUIRE(SDL_SetTextureColorMod(tex, (j * 5) & 0xFF,
                                           (j * 13) & 0xFF,
                                           (j * 19) & 0xFF));
            REQUIRE(SDL_SetTextureAlphaMod(tex, 128 + (j & 0x7F)));
            REQUIRE(SDL_RenderCopy(renderer, tex, &src, &dst));
        }
        SDL_RenderPresent(renderer);
        vid_waitvbl();
        t1 = timer_us_gettime64();
        if (*SDL_GetError()) return fail_message("SDL_RenderPresent");
        durations[i] = t1 > t0 ? t1 - t0 : 0;
    }
    return 0;
}

/* Workload D: higher command load (mix of rects and texture copies) */
static int workload_higher_cmd(SDL_Renderer *renderer,
                               SDL_Texture *tex,
                               uint64_t *durations)
{
    int i, j;
    uint64_t t0, t1;
    SDL_Rect dst;
    SDL_Rect src = { 0, 0, 64, 64 };

    if (!tex)
        return fail_message("workload_higher_cmd: NULL texture");

    for (i = 0; i < WARMUP_FRAMES; i++) {
        REQUIRE(clear_to(renderer, 8, 8, 16));
        for (j = 0; j < 400; j++) {
            if (j & 1) {
                int x = (j * 7) % (OUTPUT_W - 20);
                int y = (j * 13) % (OUTPUT_H - 20);
                int w = 8 + (j % 24);
                int h = 8 + (j % 16);
                REQUIRE(fill_rect(renderer, x, y, w, h,
                                  (j * 3) & 0xFF,
                                  (j * 7) & 0xFF,
                                  (j * 11) & 0xFF, 255,
                                  (j & 2) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE));
            } else {
                dst.x = (j * 11) % (OUTPUT_W - 64);
                dst.y = (j * 17) % (OUTPUT_H - 64);
                dst.w = 24 + (j % 40);
                dst.h = 24 + (j % 40);
                REQUIRE(SDL_SetTextureColorMod(tex, (j * 5) & 0xFF,
                                               (j * 13) & 0xFF,
                                               (j * 19) & 0xFF));
                REQUIRE(SDL_SetTextureAlphaMod(tex, 200));
                REQUIRE(SDL_RenderCopy(renderer, tex, &src, &dst));
            }
        }
        REQUIRE(present_and_vblank(renderer));
    }

    for (i = 0; i < MEASURED_FRAMES; i++) {
        SDL_ClearError();
        t0 = timer_us_gettime64();
        REQUIRE(clear_to(renderer, 8, 8, 16));
        for (j = 0; j < 400; j++) {
            if (j & 1) {
                int x = (j * 7) % (OUTPUT_W - 20);
                int y = (j * 13) % (OUTPUT_H - 20);
                int w = 8 + (j % 24);
                int h = 8 + (j % 16);
                REQUIRE(fill_rect(renderer, x, y, w, h,
                                  (j * 3) & 0xFF,
                                  (j * 7) & 0xFF,
                                  (j * 11) & 0xFF, 255,
                                  (j & 2) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE));
            } else {
                dst.x = (j * 11) % (OUTPUT_W - 64);
                dst.y = (j * 17) % (OUTPUT_H - 64);
                dst.w = 24 + (j % 40);
                dst.h = 24 + (j % 40);
                REQUIRE(SDL_SetTextureColorMod(tex, (j * 5) & 0xFF,
                                               (j * 13) & 0xFF,
                                               (j * 19) & 0xFF));
                REQUIRE(SDL_SetTextureAlphaMod(tex, 200));
                REQUIRE(SDL_RenderCopy(renderer, tex, &src, &dst));
            }
        }
        SDL_RenderPresent(renderer);
        vid_waitvbl();
        t1 = timer_us_gettime64();
        if (*SDL_GetError()) return fail_message("SDL_RenderPresent");
        durations[i] = t1 > t0 ? t1 - t0 : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Workloads E-F (prototype only)                                     */
/* ------------------------------------------------------------------ */

/* Workload E: 256x256 target update */
static int workload_target_update(SDL_Renderer *renderer,
                                  SDL_Texture *tex,
                                  uint64_t *durations)
{
    int i;
    uint64_t t0, t1;
    SDL_Texture *target;
    SDL_Rect dst = { 0, 0, 640, 480 };

    (void)tex;

    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_TARGET, 256, 256);
    if (!target)
        return fail_message("SDL_CreateTexture(TARGET 256x256)");

    /* Warmup */
    for (i = 0; i < WARMUP_FRAMES; i++) {
        REQUIRE(SDL_SetRenderTarget(renderer, target));
        REQUIRE(clear_to(renderer, 16, 16, 48));
        REQUIRE(fill_rect(renderer, 0, 0, 64, 64,
                          255, 0, 0, 255, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderTarget(renderer, NULL));
        REQUIRE(clear_to(renderer, 8, 8, 16));
        REQUIRE(SDL_RenderCopy(renderer, target, NULL, &dst));
        REQUIRE(present_and_vblank(renderer));
    }

    /* Measure */
    for (i = 0; i < MEASURED_FRAMES; i++) {
        SDL_ClearError();
        t0 = timer_us_gettime64();
        REQUIRE(SDL_SetRenderTarget(renderer, target));
        REQUIRE(clear_to(renderer, 16, 16, 48));
        REQUIRE(fill_rect(renderer, 0, 0, 64, 64,
                          255, 0, 0, 255, SDL_BLENDMODE_NONE));
        REQUIRE(fill_rect(renderer, 192, 192, 64, 64,
                          0, 0, 255, 255, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderTarget(renderer, NULL));
        REQUIRE(clear_to(renderer, 8, 8, 16));
        REQUIRE(SDL_RenderCopy(renderer, target, NULL, &dst));
        SDL_RenderPresent(renderer);
        vid_waitvbl();
        t1 = timer_us_gettime64();
        if (*SDL_GetError()) return fail_message("SDL_RenderPresent");
        durations[i] = t1 > t0 ? t1 - t0 : 0;
    }

    SDL_DestroyTexture(target);
    return 0;
}

/* Workload F: target preservation */
static int workload_target_preservation(SDL_Renderer *renderer,
                                        SDL_Texture *tex,
                                        uint64_t *durations)
{
    int i;
    uint64_t t0, t1;
    SDL_Texture *target;
    SDL_Rect dst = { 0, 0, 640, 480 };

    (void)tex;

    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_TARGET, 256, 256);
    if (!target)
        return fail_message("SDL_CreateTexture(TARGET 256x256)");

    /* Seed initial marker */
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    REQUIRE(clear_to(renderer, 32, 32, 32));
    REQUIRE(fill_rect(renderer, 96, 96, 64, 64,
                      255, 255, 0, 255, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));

    /* Warmup */
    for (i = 0; i < WARMUP_FRAMES; i++) {
        REQUIRE(SDL_SetRenderTarget(renderer, target));
        REQUIRE(fill_rect(renderer, 32 + (i % 160), 32 + (i % 128), 16, 16,
                          0, 255, 0, 255, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderTarget(renderer, NULL));
        REQUIRE(clear_to(renderer, 8, 8, 16));
        REQUIRE(SDL_RenderCopy(renderer, target, NULL, &dst));
        REQUIRE(present_and_vblank(renderer));
    }

    /* Measure */
    for (i = 0; i < MEASURED_FRAMES; i++) {
        SDL_ClearError();
        t0 = timer_us_gettime64();
        REQUIRE(SDL_SetRenderTarget(renderer, target));
        REQUIRE(fill_rect(renderer, 32 + (i % 160), 32 + (i % 128), 16, 16,
                          0, 255, 0, 255, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderTarget(renderer, NULL));
        REQUIRE(clear_to(renderer, 8, 8, 16));
        REQUIRE(SDL_RenderCopy(renderer, target, NULL, &dst));
        SDL_RenderPresent(renderer);
        vid_waitvbl();
        t1 = timer_us_gettime64();
        if (*SDL_GetError()) return fail_message("SDL_RenderPresent");
        durations[i] = t1 > t0 ? t1 - t0 : 0;
    }

    /* Verify original marker still visible */
    REQUIRE(clear_to(renderer, 8, 8, 16));
    REQUIRE(SDL_RenderCopy(renderer, target, NULL, &dst));
    REQUIRE(fill_rect(renderer, 0, 0, 256, 16,
                      255, 255, 255, 255, SDL_BLENDMODE_NONE));
    REQUIRE(present_and_vblank(renderer));

    SDL_DestroyTexture(target);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reference screen (shared for workloads A-D)                        */
/* ------------------------------------------------------------------ */

static int draw_reference_screen(SDL_Renderer *renderer,
                                 const char *label)
{
    SDL_Texture *tex;
    int frame;
#ifdef DC_PVR_PERFORMANCE_HAS_TARGETS
    SDL_Texture *target;
#endif

    (void)label;

#ifdef DC_PVR_PERFORMANCE_HAS_TARGETS
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_TARGET, 64, 64);
    if (!target)
        return fail_message("reference target creation");
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    REQUIRE(clear_to(renderer, 8, 24, 16));
    REQUIRE(fill_rect(renderer, 0, 0, 16, 16,
                      0, 220, 40, 255, SDL_BLENDMODE_NONE));
    REQUIRE(fill_rect(renderer, 48, 48, 16, 16,
                      255, 220, 0, 255, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
#endif

    tex = create_test_texture(renderer);
    if (!tex)
        return fail_message("reference texture creation");

    /* Render the same reference three times before capture so the physical
     * display and screenshot source have both advanced beyond the final
     * benchmark frame. */
    for (frame = 0; frame < 3; ++frame) {
        SDL_Rect panel = { 200, 60, 240, 180 };
        SDL_Rect src = { 0, 0, 128, 128 };

        /* Dark background and textured panel. */
        REQUIRE(clear_to(renderer, 10, 10, 20));
        REQUIRE(SDL_SetTextureColorMod(tex, 255, 200, 100));
        REQUIRE(SDL_SetTextureAlphaMod(tex, 255));
        REQUIRE(SDL_RenderCopy(renderer, tex, &src, &panel));

        /* Translucent overlap. */
        REQUIRE(fill_rect(renderer, 300, 140, 120, 100,
                          255, 0, 255, 128, SDL_BLENDMODE_BLEND));

        /* Green status bar. */
        REQUIRE(fill_rect(renderer, 0, OUTPUT_H - 24, OUTPUT_W, 24,
                          0, 200, 0, 255, SDL_BLENDMODE_NONE));

        /* Four white screen-corner markers, submitted after the status bar
         * so the bottom pair remains visible. */
        REQUIRE(fill_rect(renderer, 0, 0, 8, 8,
                          255, 255, 255, 255, SDL_BLENDMODE_NONE));
        REQUIRE(fill_rect(renderer, OUTPUT_W - 8, 0, 8, 8,
                          255, 255, 255, 255, SDL_BLENDMODE_NONE));
        REQUIRE(fill_rect(renderer, 0, OUTPUT_H - 8, 8, 8,
                          255, 255, 255, 255, SDL_BLENDMODE_NONE));
        REQUIRE(fill_rect(renderer, OUTPUT_W - 8, OUTPUT_H - 8, 8, 8,
                          255, 255, 255, 255, SDL_BLENDMODE_NONE));

#ifdef DC_PVR_PERFORMANCE_HAS_TARGETS
        {
            SDL_Rect target_panel = { 460, 280, 128, 128 };
            REQUIRE(SDL_RenderCopy(renderer, target, NULL, &target_panel));
        }
#endif

        REQUIRE(present_and_vblank(renderer));
    }
    SDL_DestroyTexture(tex);
#ifdef DC_PVR_PERFORMANCE_HAS_TARGETS
    SDL_DestroyTexture(target);
#endif
    return 0;
}

/* ------------------------------------------------------------------ */
/* Statistics helpers (integer-safe)                                  */
/* ------------------------------------------------------------------ */

static void sort_durations(uint64_t *d, int n)
{
    int i, j;
    uint64_t key;
    for (i = 1; i < n; i++) {
        key = d[i];
        j = i - 1;
        while (j >= 0 && d[j] > key) {
            d[j + 1] = d[j];
            --j;
        }
        d[j + 1] = key;
    }
}

static void print_perf_result(const char *build, char workload,
                              int frames,
                              uint64_t min_us, uint64_t max_us,
                              uint64_t total_us,
                              uint64_t mean_us, uint64_t median_us,
                              uint64_t p95_us, uint64_t p99_us,
                              int over_16667, int over_20000,
                              uint64_t fps_x1000)
{
    printf("PERF build=%s workload=%c frames=%d "
           "min_us=%lu max_us=%lu mean_us=%lu median_us=%lu "
           "p95_us=%lu p99_us=%lu over_16667=%d over_20000=%d "
           "fps_x1000=%lu total_us=%lu\n",
           build, workload, frames,
           (unsigned long)min_us, (unsigned long)max_us,
           (unsigned long)mean_us, (unsigned long)median_us,
           (unsigned long)p95_us, (unsigned long)p99_us,
           over_16667, over_20000,
           (unsigned long)fps_x1000, (unsigned long)total_us);
}

/* ------------------------------------------------------------------ */
/* Run one workload and report stats                                  */
/* ------------------------------------------------------------------ */

static int run_workload(const char *build, char workload,
                        workload_fn fn,
                        SDL_Texture *tex, SDL_Renderer *renderer)
{
    uint64_t durations[MEASURED_FRAMES];
    uint64_t sorted[MEASURED_FRAMES];
    uint64_t min_us, max_us, total_us;
    uint64_t mean_us, median_us, p95_us, p99_us;
    int over_16667, over_20000;
    uint64_t fps_x1000;
    int i;

    printf("\n=== WORKLOAD %c: %s ===\n", workload,
           workload == 'A' ? "clear-only" :
           workload == 'B' ? "moderate untextured geometry" :
           workload == 'C' ? "moderate textured geometry" :
           workload == 'D' ? "higher command load" :
           workload == 'E' ? "256x256 target update" :
           workload == 'F' ? "target preservation" : "unknown");

    if (fn(renderer, tex, durations) < 0)
        return -1;

    /* Compute statistics */
    min_us = (uint64_t)0xFFFFFFFFFFFFFFFF;
    max_us = 0;
    total_us = 0;
    over_16667 = 0;
    over_20000 = 0;

    for (i = 0; i < MEASURED_FRAMES; i++) {
        if (durations[i] < min_us) min_us = durations[i];
        if (durations[i] > max_us) max_us = durations[i];
        total_us += durations[i];
        if (durations[i] > 16667) over_16667++;
        if (durations[i] > 20000) over_20000++;
    }

    /* Copy and sort for median / percentiles */
    for (i = 0; i < MEASURED_FRAMES; i++)
        sorted[i] = durations[i];
    sort_durations(sorted, MEASURED_FRAMES);

    mean_us = total_us / (uint64_t)MEASURED_FRAMES;
    median_us = (sorted[(MEASURED_FRAMES / 2) - 1] +
                 sorted[MEASURED_FRAMES / 2]) / 2;

    /* Nearest-rank percentiles: ceil(p * n) - 1. */
    p95_us = sorted[(int)(((uint64_t)MEASURED_FRAMES * 95 + 99) / 100) - 1];
    p99_us = sorted[(int)(((uint64_t)MEASURED_FRAMES * 99 + 99) / 100) - 1];

    /* FPS * 1000 — integer division */
    fps_x1000 = total_us > 0
                    ? (uint64_t)MEASURED_FRAMES * 1000000000ULL / total_us
                    : 0;

    printf("  warmup frames:       %d\n", WARMUP_FRAMES);
    printf("  measured frames:     %d\n", MEASURED_FRAMES);
    printf("  timer API:           timer_us_gettime64() (kos/timer.h)\n");
    printf("  min_us:              %lu\n", (unsigned long)min_us);
    printf("  max_us:              %lu\n", (unsigned long)max_us);
    printf("  mean_us:             %lu\n", (unsigned long)mean_us);
    printf("  median_us:           %lu\n", (unsigned long)median_us);
    printf("  p95_us:              %lu\n", (unsigned long)p95_us);
    printf("  p99_us:              %lu\n", (unsigned long)p99_us);
    printf("  total_us:            %lu\n", (unsigned long)total_us);
    printf("  over_16667_us:      %d\n", over_16667);
    printf("  over_20000_us:      %d\n", over_20000);
    printf("  fps_x1000:           %lu\n", (unsigned long)fps_x1000);

    print_perf_result(build, workload, MEASURED_FRAMES,
                      min_us, max_us, total_us,
                      mean_us, median_us, p95_us, p99_us,
                      over_16667, over_20000, fps_x1000);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Screenshot helper                                                  */
/* ------------------------------------------------------------------ */

static int take_screenshot(const char *path)
{
#ifdef DC_PVR_RENDER_TARGET_PERFORMANCE_HARDWARE_SCREENSHOT
    int ret;
    SDL_ClearError();
    ret = vid_screen_shot(path);
    if (ret < 0) {
        printf("  CAPTURE FAIL: %s (vid_screen_shot returned %d): %s\n",
               path, ret, SDL_GetError());
        return -1;
    }
    printf("  CAPTURE OK: %s\n", path);
#else
    (void)path;
#endif
    return 0;
}

/* ------------------------------------------------------------------ */
/* Helper: find Dreamcast PVR driver index                          */
/* ------------------------------------------------------------------ */

static int find_pvr_driver(void)
{
    SDL_RendererInfo info;
    int i;

    for (i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 && info.name &&
            SDL_strcmp(info.name, "Dreamcast PVR") == 0) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int driver_idx;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_RendererInfo info;
    size_t vram_after_create;
    SDL_Texture *test_tex = NULL;
    int workload_result;

    /* Ordered render targets are always enabled in the native PVR renderer. */
    const char *build_kind = "ordered";

    (void)argc;
    (void)argv;

    /* KOS documents timer_us_gettime64() in microseconds. */
    g_timer_resolution_us = 1;
    g_timer_desc = "microseconds via timer_us_gettime64()";

    printf("PVR RTT PERFORMANCE COMPARISON\n");
    printf("  build:       %s\n", build_kind);
    printf("  timer API:   timer_us_gettime64() from kos/timer.h\n");
    printf("  timer unit:  microseconds (uint64_t since KOS boot)\n");
    printf("  timer description: %s\n", g_timer_desc);
    printf("  timer scope: measured interval covers SDL command ingestion,\n");
    printf("               command execution, RTT scene finish (if any),\n");
    printf("               physical presentation scene, pvr_wait_render_done(),\n");
    printf("               and one explicit vid_waitvbl().\n");
    printf("  warmup frames:    %d\n", WARMUP_FRAMES);
    printf("  measured frames:  %d\n", MEASURED_FRAMES);
    printf("  timer resolution: %lu us\n",
           (unsigned long)g_timer_resolution_us);

    /* The Dreamcast video path is selected during SDL video
     * initialization, so set the hint before SDL_Init().
     * Enable ordered render targets for this test. */
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");

    /* SDL init */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    /* Select Dreamcast PVR renderer */
    driver_idx = find_pvr_driver();
    if (driver_idx < 0) {
        printf("FAIL: no Dreamcast PVR renderer found\n");
        SDL_Quit();
        return 1;
    }

    /* Create window and renderer */
    window = SDL_CreateWindow("PVR RTT Performance", 0, 0,
                              OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        printf("FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, driver_idx,
                                  SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("FAIL: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Report renderer info */
    if (SDL_GetRendererInfo(renderer, &info) < 0) {
        fail_message("SDL_GetRendererInfo");
        goto done;
    }
    printf("\n  renderer name:   %s\n", info.name);
    printf("  renderer flags:  0x%08lx\n", (unsigned long)info.flags);
    printf("  output size:     %d x %d\n", OUTPUT_W, OUTPUT_H);
    printf("  vsync:           %s\n",
           (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "enabled" : "disabled");

    assertion_check(info.name != NULL &&
                    SDL_strcmp(info.name, "Dreamcast PVR") == 0,
                    "renderer is Dreamcast PVR");

    /* Check target-texture capability */
    {
        SDL_bool has_target = (info.flags & SDL_RENDERER_TARGETTEXTURE) != 0;
        if (SDL_strcmp(build_kind, "ordinary") == 0) {
            assertion_check(!has_target,
                            "ordinary: SDL_RENDERER_TARGETTEXTURE absent");
        } else {
            assertion_check(has_target,
                            "target build: SDL_RENDERER_TARGETTEXTURE present");
        }
    }

    /* VRAM after create */
    vram_after_create = (size_t)pvr_mem_available();
    printf("  VRAM build=%s available=%zu\n", build_kind, vram_after_create);

    /* ------------------------------------------------------------------ */
    /* Create shared test texture for workloads C and D                   */
    /* ------------------------------------------------------------------ */

    test_tex = create_test_texture(renderer);
    if (!test_tex) {
        printf("FAIL: create_test_texture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* ------------------------------------------------------------------ */
    /* Run workloads A-D (identical for both builds)                      */
    /* ------------------------------------------------------------------ */

    printf("\n========== SCREEN-ONLY WORKLOADS (A-D) ==========\n");

    workload_result = run_workload(build_kind, 'A', workload_clear, test_tex, renderer);
    if (workload_result < 0) goto done;

    workload_result = run_workload(build_kind, 'B', workload_untextured, test_tex, renderer);
    if (workload_result < 0) goto done;

    workload_result = run_workload(build_kind, 'C', workload_textured, test_tex, renderer);
    if (workload_result < 0) goto done;

    workload_result = run_workload(build_kind, 'D', workload_higher_cmd, test_tex, renderer);
    if (workload_result < 0) goto done;

    /* ------------------------------------------------------------------ */
    /* Workloads E-F (target builds only)                                 */
    /* ------------------------------------------------------------------ */

    if (SDL_strcmp(build_kind, "ordinary") != 0) {
        printf("\n========== TARGET WORKLOADS (E-F) ==========\n");

        workload_result = run_workload(build_kind, 'E', workload_target_update, test_tex, renderer);
        if (workload_result < 0) goto done;

        workload_result = run_workload(build_kind, 'F', workload_target_preservation, test_tex, renderer);
        if (workload_result < 0) goto done;
    }

    /* ------------------------------------------------------------------ */
    /* Reference screen (shared visual validation)                        */
    /* ------------------------------------------------------------------ */

    printf("\n========== DRAWING REFERENCE SCREEN ==========\n");
    if (draw_reference_screen(renderer, build_kind) < 0)
        goto done;

    /* ------------------------------------------------------------------ */
    /* Hardware screenshot                                                */
    /* ------------------------------------------------------------------ */

    {
        const char *cap_path = NULL;
#ifdef DC_PVR_RENDER_TARGET_PERFORMANCE_HARDWARE_SCREENSHOT
        if (SDL_strcmp(build_kind, "ordinary") == 0) {
            cap_path = ordinary_capture;
        } else if (SDL_strcmp(build_kind, "prototype") == 0) {
            cap_path = prototype_capture;
        } else {
            cap_path = ordered_capture;
        }
#endif
        if (cap_path) {
            if (take_screenshot(cap_path) < 0) {
                ++g_failures;
                printf("  Screenshot failed.\n");
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /* Summary                                                            */
    /* ------------------------------------------------------------------ */

    printf("\n========== SUMMARY ==========\n");
    printf("  checks:      %d\n", g_checks);
    printf("  failures:    %d\n", g_failures);
    printf("  PASS: %s\n", g_failures == 0 ? "YES" : "NO");

    /* Hold the final frame for 12 seconds, then return to dc-load-ip */
    printf("  Holding frame for %d ms, then returning to dc-load-ip...\n",
           PAGE_HOLD_MS);
    SDL_Delay(PAGE_HOLD_MS);

done:
    if (test_tex)
        SDL_DestroyTexture(test_tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return g_failures > 0 ? 1 : 0;
}
