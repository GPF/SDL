/*
  CPU submission cost of geometry workloads for the native Dreamcast PVR renderer.

  Prints milliseconds per frame spent submitting each workload and the cost per
  primitive. There is no pass/fail: the point is to compare library builds.

  SDL2 has no public renderer flush. With the default (unbatched) behavior every
  public draw call flushes its own command queue, so timing the workload calls
  measures SDL core ingestion plus backend emission plus submission. Each frame
  starts with a clear, which begins the scene and absorbs KallistiOS's wait for
  the previous render; the timed window therefore contains no vsync or GPU time.

  SDL2 grows the render vertex buffer on demand instead of using a fixed 512 KB
  buffer, so the counts here are chosen to match the SDL3 reference workload and
  stay well inside available memory rather than inside a fixed buffer.

  Clock: clock_gettime(CLOCK_MONOTONIC). SDL_GetTicksNS is not reliable on the
  Dreamcast timer path, so the raw POSIX clock is used.
*/

#include <SDL.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define FRAMES 60

#define NSPRITES 400
#define NRECTS 2400
#define NPOINTS 2400
/* A single diagonal line becomes hundreds of 1x1 PVR quads. After viewport
 * clipping, the previous 1500-pair workload generated about 1.53 million
 * in-viewport pixel quads, or 9.17 million vertex commands per frame. That is
 * roughly 350x the estimated 26,210-command limit, far beyond a useful CPU
 * benchmark. Keep only one pair here; line capacity stress belongs in its own
 * bounded test. */
#define NLINES 1 /* pairs: 2 * NLINES segments per frame */
#define NTRIS 1500
#define NBIG 20

static unsigned long long now_ns(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long) t.tv_sec * 1000000000ull + (unsigned long long) t.tv_nsec;
}

static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_Rect sprites[NSPRITES];
static SDL_Rect rects[NRECTS];
static SDL_Point points[NPOINTS];
static SDL_Rect big[NBIG];
static SDL_Vertex verts[NTRIS * 3];

static int check_result(const char *operation, int result)
{
    if (result < 0) {
        printf("gbench FAIL %-28s %s\n", operation, SDL_GetError());
        fflush(stdout);
        return -1;
    }
    return 0;
}

static int find_pvr_driver(void)
{
    SDL_RendererInfo info;
    int i;
    const int count = SDL_GetNumRenderDrivers();

    for (i = 0; i < count; ++i) {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 &&
            SDL_strcmp(info.name, "Dreamcast PVR") == 0) {
            return i;
        }
    }
    return -1;
}

static double run(const char *name, int nsprites, int nrects, int npoints,
                  int nlines, int ntris, int units)
{
    unsigned long long total_ns = 0;
    unsigned long long frame0 = now_ns();

    for (int f = 0; f < FRAMES; ++f) {
        unsigned long long t0;

        SDL_ClearError();
        if (check_result("SDL_SetRenderDrawColor", SDL_SetRenderDrawColor(renderer, 0x20, 0x20, 0x40, 0xFF)) < 0 ||
            check_result("SDL_RenderClear", SDL_RenderClear(renderer)) < 0) { /* absorbs the previous render */
            return -1.0;
        }
        t0 = now_ns();

        if (nrects) {
            if (check_result("SDL_RenderFillRects", SDL_SetRenderDrawColor(renderer, 255, 128, 0, 255)) < 0 ||
                check_result("SDL_RenderFillRects", SDL_RenderFillRects(renderer, rects, nrects)) < 0) return -1.0;
        }
        if (npoints) {
            if (check_result("SDL_RenderDrawPoints", SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255)) < 0 ||
                check_result("SDL_RenderDrawPoints", SDL_RenderDrawPoints(renderer, points, npoints)) < 0) return -1.0;
        }
        if (nlines) {
            if (check_result("SDL_RenderDrawLine", SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255)) < 0) return -1.0;
            for (int k = 0; k < nlines; ++k) {
                if (check_result("SDL_RenderDrawLine", SDL_RenderDrawLine(renderer, 32, 32 + k, 606, 446 - k)) < 0 ||
                    check_result("SDL_RenderDrawLine", SDL_RenderDrawLine(renderer, 606, 32 + k, 32, 446 - k)) < 0) return -1.0;
            }
        }
        if (ntris) {
            if (check_result("SDL_RenderGeometry", SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255)) < 0 ||
                check_result("SDL_RenderGeometry", SDL_RenderGeometry(renderer, NULL, verts, ntris * 3, NULL, 0)) < 0) return -1.0;
        }
        if (nsprites) {
            for (int i = 0; i < nsprites; ++i) {
                if (check_result("SDL_RenderCopy", SDL_RenderCopy(renderer, texture, NULL, &sprites[i])) < 0) return -1.0;
            }
        }
        total_ns += now_ns() - t0; /* every call above flushed its own queue */

        SDL_RenderPresent(renderer);
        if (SDL_GetError()[0]) {
            printf("gbench FAIL %-28s %s\n", name, SDL_GetError());
            fflush(stdout);
            return -1.0;
        }
    }

    {
        const double ms = (double) total_ns / 1000000.0 / FRAMES;
        const double frame_ms = (double) (now_ns() - frame0) / 1000000.0 / FRAMES;

        if (units > 0) {
            printf("gbench %-32s submit %7.2f ms/frame  %6.2f us per unit (%d units)  [frame %.2f ms]\n",
                   name, ms, ms * 1000.0 / units, units, frame_ms);
            fflush(stdout);
        } else {
            printf("gbench %-32s submit %7.2f ms/frame  [frame %.2f ms]\n", name, ms, frame_ms);
            fflush(stdout);
        }
        return ms;
    }
}

/* Full frame time including SDL_RenderPresent. This is the workload that shows
 * whether presentation still has to wait for the PVR to finish rendering. */
static double run_present(const char *name, int nsprites, int nrects, int npoints,
                          int ntris, int nbig)
{
    unsigned long long total_ns = 0;

    for (int f = 0; f < FRAMES; ++f) {
        unsigned long long t0, t1;

        SDL_ClearError();
        SDL_SetRenderDrawColor(renderer, 0x20, 0x20, 0x40, 0xFF);
        SDL_RenderClear(renderer);
        if (nrects) {
            SDL_SetRenderDrawColor(renderer, 255, 128, 0, 255);
            SDL_RenderFillRects(renderer, rects, nrects);
        }
        if (npoints) {
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            SDL_RenderDrawPoints(renderer, points, npoints);
        }
        if (ntris) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
            SDL_RenderGeometry(renderer, NULL, verts, ntris * 3, NULL, 0);
        }
        if (nsprites) {
            for (int i = 0; i < nsprites; ++i) {
                SDL_RenderCopy(renderer, texture, NULL, &sprites[i]);
            }
        }
        if (nbig) {
            SDL_SetRenderDrawColor(renderer, 0, 200, 200, 128);
            for (int i = 0; i < nbig; ++i) {
                SDL_RenderFillRect(renderer, &big[i]);
            }
        }
        t0 = now_ns();
        SDL_ClearError();
        SDL_RenderPresent(renderer);
        t1 = now_ns();
        total_ns += t1 - t0;
        if (SDL_GetError()[0]) {
            printf("gbench %-32s present error: %s\n", name, SDL_GetError());
            fflush(stdout);
            return -1.0;
        }
    }

    {
        const double ms = (double) total_ns / 1000000.0 / FRAMES;

        printf("gbench %-32s present %7.2f ms/frame\n", name, ms);
        fflush(stdout);
        return ms;
    }
}

/* Render-target transition cost: target draw, back to screen, present. The
 * ordered backend must finish the target scene here, so this is the workload
 * most sensitive to a presentation that waits for the PVR. */
static double run_rtt_switch(void)
{
    SDL_Texture *target;
    SDL_Rect small[8];
    unsigned long long total_ns = 0;

    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_TARGET, 320, 240);
    if (!target) {
        printf("gbench render-target switch        skipped: %s\n", SDL_GetError());
        return 0.0;
    }
    for (int i = 0; i < 8; ++i) {
        small[i] = (SDL_Rect) { 20 + i * 30, 20 + i * 20, 24, 24 };
    }

    /* Warm up one complete screen frame so the ordered backend is at the
     * "targets allowed" boundary before measuring the transition. */
    SDL_ClearError();
    SDL_SetRenderDrawColor(renderer, 0x20, 0x20, 0x40, 0xFF);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    if (SDL_GetError()[0]) {
        printf("gbench render-target switch        warmup failed: %s\n", SDL_GetError());
        SDL_DestroyTexture(target);
        return 0.0;
    }

    for (int f = 0; f < FRAMES; ++f) {
        unsigned long long t0, t1;

        t0 = now_ns();
        SDL_ClearError();
        if (SDL_SetRenderTarget(renderer, target) < 0) {
            printf("gbench render-target switch        failed: %s\n", SDL_GetError());
            SDL_DestroyTexture(target);
            return 0.0;
        }
        SDL_SetRenderDrawColor(renderer, 200, 0, 200, 255);
        SDL_RenderFillRects(renderer, small, 8);
        if (SDL_SetRenderTarget(renderer, NULL) < 0) {
            printf("gbench render-target switch        failed: %s\n", SDL_GetError());
            SDL_DestroyTexture(target);
            return 0.0;
        }
        SDL_SetRenderDrawColor(renderer, 0x20, 0x20, 0x40, 0xFF);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 0, 200, 200, 255);
        SDL_RenderFillRects(renderer, small, 8);
        SDL_RenderPresent(renderer);
        t1 = now_ns();
        total_ns += t1 - t0;
    }

    SDL_DestroyTexture(target);
    printf("gbench %-32s present %7.2f ms/frame\n", "render-target switch", total_ns / 1000000.0 / FRAMES);
    fflush(stdout);
    return total_ns / 1000000.0 / FRAMES;
}

int main(int argc, char *argv[])
{
    SDL_Window *window;
    int driver_index;
    int exit_status = 0;

    (void) argc;
    (void) argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    driver_index = find_pvr_driver();
    if (driver_index < 0) {
        printf("FAIL: no Dreamcast PVR renderer driver\n");
        SDL_Quit();
        return 1;
    }

    window = SDL_CreateWindow("PVR geometry bench", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, driver_index, 0);
    if (!renderer) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0) {
        printf("FAIL: blend mode\n");
        SDL_Quit();
        return 1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                SDL_TEXTUREACCESS_STATIC, 32, 32);
    if (!texture) {
        printf("FAIL: texture: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    {
        Uint16 pixels[32 * 32];

        for (int i = 0; i < 32 * 32; ++i) {
            pixels[i] = (Uint16) ((i & 1) ? 0x801f : 0x001f);
        }
        if (SDL_UpdateTexture(texture, NULL, pixels, 32 * sizeof(Uint16)) < 0) {
            printf("FAIL: update: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
    }

    for (int i = 0; i < NSPRITES; ++i) {
        sprites[i] = (SDL_Rect) { (i * 3) % 600, (i * 5) % 440, 16, 16 };
    }
    for (int i = 0; i < NRECTS; ++i) {
        rects[i] = (SDL_Rect) { (i * 7) % 636, (i * 11) % 476, 4, 4 };
    }
    for (int i = 0; i < NPOINTS; ++i) {
        points[i] = (SDL_Point) { (i * 13) % 640, (i * 17) % 480 };
    }
    for (int i = 0; i < NBIG; ++i) {
        big[i] = (SDL_Rect) { 0, 0, OUTPUT_W, OUTPUT_H };
    }
    for (int i = 0; i < NTRIS * 3; ++i) {
        verts[i].position.x = (float) ((i * 23) % 640);
        verts[i].position.y = (float) ((i * 29) % 480);
        verts[i].color.r = (Uint8) (i & 0xff);
        verts[i].color.g = (Uint8) ((i * 3) & 0xff);
        verts[i].color.b = (Uint8) ((i * 5) & 0xff);
        verts[i].color.a = 255;
        verts[i].tex_coord.x = (float) (i % 32) / 32.0f;
        verts[i].tex_coord.y = (float) ((i * 7) % 32) / 32.0f;
    }

    {
        SDL_RendererInfo info;

        if (SDL_GetRendererInfo(renderer, &info) == 0) {
            printf("gbench renderer=%s\n", info.name);
        } else {
            printf("gbench renderer=<unknown>\n");
        }
        fflush(stdout);
    }

#define RUN_BENCH(...) do { if (run(__VA_ARGS__) < 0.0) goto bench_done; } while (0)
    RUN_BENCH("empty frame", 0, 0, 0, 0, 0, 0);
    /* Small counts identify the CPU-bound regime: with large counts the
     * store-queue path stalls on the TA, so the figure becomes GPU-bound and
     * stops reflecting backend CPU work. */
    RUN_BENCH("200 sprites", 200, 0, 0, 0, 0, 200);
    RUN_BENCH("200 fill rects", 0, 200, 0, 0, 0, 200);
    RUN_BENCH("200 points", 0, 0, 200, 0, 0, 200);
    RUN_BENCH("200 triangles", 0, 0, 0, 0, 200, 200);
    RUN_BENCH("400 sprites", NSPRITES, 0, 0, 0, 0, NSPRITES);
    RUN_BENCH("2400 fill rects", 0, NRECTS, 0, 0, 0, NRECTS);
    RUN_BENCH("2400 points", 0, 0, NPOINTS, 0, 0, NPOINTS);
    RUN_BENCH("2 line calls", 0, 0, 0, NLINES, 0, NLINES * 2);
    RUN_BENCH("1500 triangles", 0, 0, 0, 0, NTRIS, NTRIS);
    RUN_BENCH("testsprite-like mix", 100, 0, 600, 0, 12, 712);

    printf("\n--- full frame including SDL_RenderPresent ---\n");
    if (run_present("8 quads", 0, 8, 0, 0, 0) < 0.0 ||
        run_present("200 sprites", 200, 0, 0, 0, 0) < 0.0 ||
        run_present("1000 fill rects", 0, 1000, 0, 0, 0) < 0.0) goto bench_done;
    /* GPU-bound: full-screen blended quads, so presentation cannot finish before
     * the PVR has actually rendered the frame. */
    if (run_present("20 fullscreen quads (GPU-bound)", 0, 0, 0, 0, NBIG) < 0.0) goto bench_done;
    run_rtt_switch();

bench_done:
#undef RUN_BENCH
    if (renderer) {
        SDL_ClearError();
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
        if (SDL_GetError()[0]) {
            printf("gbench renderer shutdown error: %s\n", SDL_GetError());
            exit_status = 1;
        }
    }
    SDL_Quit();
    return exit_status;
}
