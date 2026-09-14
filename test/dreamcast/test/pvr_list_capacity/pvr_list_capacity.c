/*
 * Focused TA/OPB capacity and performance measurement for the guarded
 * opaque-prefix list-routing increment.
 *
 * Workloads
 *   A — Capacity baseline: feature-off (TR-only) escalating rectangles
 *   B — Capacity feature-on: mixed OP-prefix + TR escalating rectangles
 *   C — Capacity feature-on: opaque-only workload
 *   D — Performance: screen clear only
 *   E — Performance: many opaque untextured rectangles
 *   F — Performance: many translucent rectangles
 *   G — Performance: mixed OP-prefix + translucent workload
 *   H — Performance: textured geometry
 *   I — Feature-off comparison (same workload as B)
 *
 * Timing uses KOS timer_us_gettime64() for hardware-accurate elapsed us.
 * Captures raw 640x480 PPMs via /pc path.
 *
 * Build:
 *   make -B -f Makefile.dc SDL2_LIBDIR=/path/to/local/build
 */

#include <SDL.h>
#include <kos/timer.h>
#include <dc/pvr.h>
#include <dc/video.h>

#include <stdio.h>
#include <string.h>

/* ---------- constants ---------- */

#define HOLD_MS       2000
#define SCREEN_W      640
#define SCREEN_H      480
#define CAPTURE_BASE  "/pc/pvr_list_capacity"

/* ---------- helpers ---------- */

static void pvr_init_and_create(SDL_Renderer **renderer, SDL_Window **win)
{
    int drv = -1;
    SDL_RendererInfo info;
    int i;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");

    for (i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 &&
            SDL_strcmp(info.name, "Dreamcast PVR") == 0) {
            drv = i;
            break;
        }
    }
    if (drv < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "No Dreamcast PVR driver found");
        return;
    }

    *win = SDL_CreateWindow("pvr_list_capacity",
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            SCREEN_W, SCREEN_H, 0);
    if (!*win) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateWindow failed: %s", SDL_GetError());
        return;
    }

    *renderer = SDL_CreateRenderer(*win, drv,
                                   SDL_RENDERER_ACCELERATED |
                                   SDL_RENDERER_PRESENTVSYNC);
    if (!*renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(*win);
        *win = NULL;
        return;
    }
}

static void present_and_hold(SDL_Renderer *r)
{
    SDL_RenderPresent(r);
    SDL_Delay(HOLD_MS);
}

static void screenshot(const char *path)
{
    SDL_Delay(120);
    vid_waitvbl(); vid_waitvbl(); vid_waitvbl();
    if (path) vid_screen_shot(path);
}

/* ---------- workload A: feature-off capacity ---------- */

static void run_workload_a(SDL_Renderer *renderer, SDL_Window *win)
{
    const char *workload = "A (feature-off capacity)";
    int count;
    uint64_t start_us, end_us;
    unsigned long elapsed_ms;
    SDL_Rect r;
    int best = 0;
    int last_r_c = 0, last_g_c = 0, last_b_c = 0;

    (void)win;
    SDL_Log("=== %s ===", workload);
    SDL_Log("Starting capacity scan (feature-off, TR-only)...");

    /* Bounded search: 3000 -> 6000 in steps of 500 */
    for (count = 3000; count <= 6000; count += 500) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();

        for (int i = 0; i < count; ++i) {
            Uint8 r_c = (Uint8)(i & 0xff);
            Uint8 g_c = (Uint8)((i >> 8) & 0xff);
            Uint8 b_c = (Uint8)((i >> 16) & 0xff);
            SDL_SetRenderDrawColor(renderer, r_c, g_c, b_c, 255);
            r.x = (i * 3) % SCREEN_W;
            r.y = (i * 7) % SCREEN_H;
            r.w = 4;
            r.h = 4;
            SDL_RenderFillRect(renderer, &r);
        }

        end_us = timer_us_gettime64();
        elapsed_ms = (unsigned long)((end_us - start_us) / 1000);

        SDL_ClearError();
        SDL_RenderPresent(renderer);
        if (SDL_GetError()[0] != '\0') {
            SDL_Log("  %d rects: FAIL (%s) in %lu ms",
                    count, SDL_GetError(), elapsed_ms);
            break;
        }

        SDL_Log("  %d rects: OK (%lu ms/frame)", count, elapsed_ms);
        best = count;
        last_r_c = (int)(count - 1) & 0xff;
        last_g_c = (int)((count - 1) >> 8) & 0xff;
        last_b_c = (int)((count - 1) >> 16) & 0xff;
    }

    SDL_Log("  Best successful count: %d", best);
    SDL_Log("  Last color: 0x%06x",
            (last_r_c << 16) | (last_g_c << 8) | last_b_c);
    present_and_hold(renderer);
    screenshot(CAPTURE_BASE "-workloadA.ppm");
}

/* ---------- workload B: feature-on capacity (mixed OP+TR) ---------- */

static void run_workload_b(SDL_Renderer *renderer, SDL_Window *win)
{
    const char *workload = "B (feature-on capacity, mixed OP+TR)";
    int count;
    uint64_t start_us, end_us;
    unsigned long elapsed_ms;
    SDL_Rect r;
    int best = 0;
    int last_r_c = 0, last_g_c = 0, last_b_c = 0;

    (void)win;
    SDL_Log("=== %s ===", workload);
    SDL_Log("Starting capacity scan (feature-on, mixed OP-prefix + TR)...");

    for (count = 3000; count <= 6000; count += 500) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();

        for (int i = 0; i < count; ++i) {
            if (i % 2 == 0) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            } else {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128);
            }
            if (i % 2 == 0) {
                Uint8 r_c = (Uint8)(i & 0xff);
                Uint8 g_c = (Uint8)((i >> 8) & 0xff);
                Uint8 b_c = (Uint8)((i >> 16) & 0xff);
                SDL_SetRenderDrawColor(renderer, r_c, g_c, b_c, 255);
            }
            r.x = (i * 3) % SCREEN_W;
            r.y = (i * 7) % SCREEN_H;
            r.w = 4;
            r.h = 4;
            SDL_RenderFillRect(renderer, &r);
        }

        end_us = timer_us_gettime64();
        elapsed_ms = (unsigned long)((end_us - start_us) / 1000);

        SDL_ClearError();
        SDL_RenderPresent(renderer);
        if (SDL_GetError()[0] != '\0') {
            SDL_Log("  %d rects: FAIL (%s) in %lu ms",
                    count, SDL_GetError(), elapsed_ms);
            break;
        }

        SDL_Log("  %d rects: OK (%lu ms/frame)", count, elapsed_ms);
        best = count;
        last_r_c = (int)(count - 1) & 0xff;
        last_g_c = (int)((count - 1) >> 8) & 0xff;
        last_b_c = (int)((count - 1) >> 16) & 0xff;
    }

    SDL_Log("  Best successful count: %d", best);
    SDL_Log("  Last color: 0x%06x",
            (last_r_c << 16) | (last_g_c << 8) | last_b_c);
    present_and_hold(renderer);
    screenshot(CAPTURE_BASE "-workloadB.ppm");
}

/* ---------- workload C: feature-on opaque-only ---------- */

static void run_workload_c(SDL_Renderer *renderer, SDL_Window *win)
{
    const char *workload = "C (feature-on opaque-only)";
    int count;
    uint64_t start_us, end_us;
    unsigned long elapsed_ms;
    SDL_Rect r;
    int best = 0;

    (void)win;
    SDL_Log("=== %s ===", workload);
    SDL_Log("Starting capacity scan (feature-on, opaque-only)...");

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    for (count = 3000; count <= 6000; count += 500) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();

        for (int i = 0; i < count; ++i) {
            Uint8 r_c = (Uint8)(i & 0xff);
            Uint8 g_c = (Uint8)((i >> 8) & 0xff);
            Uint8 b_c = (Uint8)((i >> 16) & 0xff);
            SDL_SetRenderDrawColor(renderer, r_c, g_c, b_c, 255);
            r.x = (i * 3) % SCREEN_W;
            r.y = (i * 7) % SCREEN_H;
            r.w = 4;
            r.h = 4;
            SDL_RenderFillRect(renderer, &r);
        }

        end_us = timer_us_gettime64();
        elapsed_ms = (unsigned long)((end_us - start_us) / 1000);

        SDL_ClearError();
        SDL_RenderPresent(renderer);
        if (SDL_GetError()[0] != '\0') {
            SDL_Log("  %d rects: FAIL (%s) in %lu ms",
                    count, SDL_GetError(), elapsed_ms);
            break;
        }

        SDL_Log("  %d rects: OK (%lu ms/frame)", count, elapsed_ms);
        best = count;
    }

    SDL_Log("  Best successful count: %d", best);
    present_and_hold(renderer);
    screenshot(CAPTURE_BASE "-workloadC.ppm");
}

/* ---------- workload D: clear only ---------- */

static void run_workload_d(SDL_Renderer *renderer)
{
    const char *workload = "D (clear only)";
    uint64_t start_us, end_us;
    uint64_t elapsed_us;
    uint64_t total_us = 0;
    int frames = 50;

    SDL_Log("=== %s ===", workload);

    for (int f = 0; f < frames; ++f) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();
        SDL_RenderPresent(renderer);
        end_us = timer_us_gettime64();
        elapsed_us = end_us - start_us;
        total_us += elapsed_us;

        SDL_ClearError();
        (void)SDL_GetError();
    }

    SDL_Log("  Mean: %.3f ms/frame (%.1f FPS)",
            (double)total_us / frames / 1000.0,
            1000000.0 / ((double)total_us / frames));
    present_and_hold(renderer);
}

/* ---------- workload E: many opaque rectangles ---------- */

static void run_workload_e(SDL_Renderer *renderer)
{
    const char *workload = "E (many opaque rects)";
    const int count = 1000;
    uint64_t start_us, end_us;
    uint64_t elapsed_us;
    uint64_t total_us = 0;
    int frames = 30;
    SDL_Rect r;

    SDL_Log("=== %s (%d rects/frame) ===", workload, count);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    for (int f = 0; f < frames; ++f) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();

        for (int i = 0; i < count; ++i) {
            Uint8 r_c = (Uint8)(i & 0xff);
            Uint8 g_c = (Uint8)((i >> 8) & 0xff);
            Uint8 b_c = (Uint8)((i >> 16) & 0xff);
            SDL_SetRenderDrawColor(renderer, r_c, g_c, b_c, 255);
            r.x = (i * 3) % SCREEN_W;
            r.y = (i * 7) % SCREEN_H;
            r.w = 4;
            r.h = 4;
            SDL_RenderFillRect(renderer, &r);
        }

        SDL_RenderPresent(renderer);
        end_us = timer_us_gettime64();
        elapsed_us = end_us - start_us;
        total_us += elapsed_us;

        SDL_ClearError();
        (void)SDL_GetError();
    }

    SDL_Log("  Mean: %.3f ms/frame (%.1f FPS)",
            (double)total_us / frames / 1000.0,
            1000000.0 / ((double)total_us / frames));
    present_and_hold(renderer);
}

/* ---------- workload F: many translucent rectangles ---------- */

static void run_workload_f(SDL_Renderer *renderer)
{
    const char *workload = "F (many translucent rects)";
    const int count = 1000;
    uint64_t start_us, end_us;
    uint64_t elapsed_us;
    uint64_t total_us = 0;
    int frames = 30;
    SDL_Rect r;

    SDL_Log("=== %s (%d rects/frame) ===", workload, count);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int f = 0; f < frames; ++f) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();

        for (int i = 0; i < count; ++i) {
            Uint8 r_c = (Uint8)(i & 0xff);
            Uint8 g_c = (Uint8)((i >> 8) & 0xff);
            Uint8 b_c = (Uint8)((i >> 16) & 0xff);
            SDL_SetRenderDrawColor(renderer, r_c, g_c, b_c, 128);
            r.x = (i * 3) % SCREEN_W;
            r.y = (i * 7) % SCREEN_H;
            r.w = 4;
            r.h = 4;
            SDL_RenderFillRect(renderer, &r);
        }

        SDL_RenderPresent(renderer);
        end_us = timer_us_gettime64();
        elapsed_us = end_us - start_us;
        total_us += elapsed_us;

        SDL_ClearError();
        (void)SDL_GetError();
    }

    SDL_Log("  Mean: %.3f ms/frame (%.1f FPS)",
            (double)total_us / frames / 1000.0,
            1000000.0 / ((double)total_us / frames));
    present_and_hold(renderer);
}

/* ---------- workload G: mixed OP-prefix + translucent ---------- */

static void run_workload_g(SDL_Renderer *renderer)
{
    const char *workload = "G (mixed OP-prefix + translucent)";
    const int count = 1000;
    uint64_t start_us, end_us;
    uint64_t elapsed_us;
    uint64_t total_us = 0;
    int frames = 30;
    SDL_Rect r;

    SDL_Log("=== %s (%d rects/frame) ===", workload, count);

    for (int f = 0; f < frames; ++f) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();

        for (int i = 0; i < count; ++i) {
            if (i < count / 2) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer,
                                       (i & 0xff), ((i >> 8) & 0xff),
                                       ((i >> 16) & 0xff), 255);
            } else {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer,
                                       (i & 0xff), ((i >> 8) & 0xff),
                                       ((i >> 16) & 0xff), 128);
            }
            r.x = (i * 3) % SCREEN_W;
            r.y = (i * 7) % SCREEN_H;
            r.w = 4;
            r.h = 4;
            SDL_RenderFillRect(renderer, &r);
        }

        SDL_RenderPresent(renderer);
        end_us = timer_us_gettime64();
        elapsed_us = end_us - start_us;
        total_us += elapsed_us;

        SDL_ClearError();
        (void)SDL_GetError();
    }

    SDL_Log("  Mean: %.3f ms/frame (%.1f FPS)",
            (double)total_us / frames / 1000.0,
            1000000.0 / ((double)total_us / frames));
    present_and_hold(renderer);
}

/* ---------- workload H: textured geometry ---------- */

static void run_workload_h(SDL_Renderer *renderer)
{
    const char *workload = "H (textured geometry)";
    const int count = 500;
    uint64_t start_us, end_us;
    uint64_t elapsed_us;
    uint64_t total_us = 0;
    int frames = 50;
    SDL_Rect dr;
    SDL_Texture *tex;
    Uint16 pixels[64];
    int i;

    SDL_Log("=== %s (%d quads/frame) ===", workload, count);

    /* Create a simple 8x8 checkerboard texture */
    for (i = 0; i < 64; ++i) {
        int x = i % 8;
        int y = i / 8;
        pixels[i] = ((x ^ y) & 1) ? 0x7C00 : 0x03E0;
    }
    tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                            SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!tex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateTexture failed: %s", SDL_GetError());
        present_and_hold(renderer);
        return;
    }
    SDL_UpdateTexture(tex, NULL, pixels, 16);

    for (int f = 0; f < frames; ++f) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();

        for (int i = 0; i < count; ++i) {
            dr.x = (i * 3) % (SCREEN_W - 8);
            dr.y = (i * 7) % (SCREEN_H - 8);
            dr.w = 8;
            dr.h = 8;
            SDL_RenderCopy(renderer, tex, NULL, &dr);
        }

        SDL_RenderPresent(renderer);
        end_us = timer_us_gettime64();
        elapsed_us = end_us - start_us;
        total_us += elapsed_us;

        SDL_ClearError();
        (void)SDL_GetError();
    }

    SDL_DestroyTexture(tex);
    SDL_Log("  Mean: %.3f ms/frame (%.1f FPS)",
            (double)total_us / frames / 1000.0,
            1000000.0 / ((double)total_us / frames));
    present_and_hold(renderer);
    screenshot(CAPTURE_BASE "-workloadH.ppm");
}

/* ---------- workload I: feature-off comparison (same as B) ---------- */

static void run_workload_i(SDL_Renderer *renderer, SDL_Window *win)
{
    const char *workload = "I (feature-off comparison, same as B)";
    int count = 3000;
    uint64_t start_us, end_us;
    uint64_t elapsed_us;
    SDL_Rect r;
    uint64_t total_us = 0;
    int frames = 15;

    (void)win;
    SDL_Log("=== %s ===", workload);
    SDL_Log("Running %d frames at %d rects/frame (feature-off)...",
            frames, count);

    for (int f = 0; f < frames; ++f) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        start_us = timer_us_gettime64();

        for (int i = 0; i < count; ++i) {
            if (i % 2 == 0) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                Uint8 r_c = (Uint8)(i & 0xff);
                Uint8 g_c = (Uint8)((i >> 8) & 0xff);
                Uint8 b_c = (Uint8)((i >> 16) & 0xff);
                SDL_SetRenderDrawColor(renderer, r_c, g_c, b_c, 255);
            } else {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128);
            }
            r.x = (i * 3) % SCREEN_W;
            r.y = (i * 7) % SCREEN_H;
            r.w = 4;
            r.h = 4;
            SDL_RenderFillRect(renderer, &r);
        }

        SDL_RenderPresent(renderer);
        end_us = timer_us_gettime64();
        elapsed_us = end_us - start_us;
        total_us += elapsed_us;

        SDL_ClearError();
        (void)SDL_GetError();
    }

    SDL_Log("  Mean: %.3f ms/frame (%.1f FPS)",
            (double)total_us / frames / 1000.0,
            1000000.0 / ((double)total_us / frames));
    present_and_hold(renderer);
    screenshot(CAPTURE_BASE "-workloadI.ppm");
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    pvr_init_and_create(&renderer, &win);
    if (!renderer || !win) {
        SDL_Quit();
        return 1;
    }

    /* Report architecture — use renderer name to prove linked archive state */
    {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(renderer, &info);
        SDL_Log("Architecture: %s", info.name);
    }
    SDL_Log("Output: %dx%d", SCREEN_W, SCREEN_H);
    SDL_Log("PVR vertex buffer: 512 KiB (shared)");
    SDL_Log("OPB bin sizes: [0]=%d [1]=%d [2]=%d [3]=%d [4]=%d",
            PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16,
            PVR_BINSIZE_0, PVR_BINSIZE_0);
    SDL_Log("OPB overflow count: 3");
    SDL_Log("%s", "");

    /* Capacity workloads */
    run_workload_a(renderer, win);
    SDL_Delay(500);
    run_workload_b(renderer, win);
    SDL_Delay(500);
    run_workload_c(renderer, win);

    SDL_Log("%s", "");
    SDL_Log("=== Performance workloads ===");

    /* Performance workloads */
    run_workload_d(renderer);
    SDL_Delay(500);
    run_workload_e(renderer);
    SDL_Delay(500);
    run_workload_f(renderer);
    SDL_Delay(500);
    run_workload_g(renderer);
    SDL_Delay(500);
    run_workload_h(renderer);

    SDL_Log("%s", "");
    SDL_Log("=== Feature-off comparison ===");

    /* Feature-off comparison */
    SDL_Delay(500);
    run_workload_i(renderer, win);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();

    SDL_Log("PASS");
    return 0;
}
