/*
  Bounded single-frame line-capacity probe for the native Dreamcast PVR renderer.

  Test-only diagnostic. It changes no backend behavior.

  Each sweep step renders exactly one frame containing N SDL_RenderDrawLine calls
  on the same diagonal pair, then reports:

    - SDL API errors during submission,
    - CPU submit time (so a submission stall is visible as a huge elapsed time),
    - PVR TA register state (OPB and vertex-buffer span and high-water mark),
    - SDL_RenderPresent result and elapsed time,
    - true render completion time, measured by calling pvr_wait_render_done()
      repeatedly until it succeeds (KOS's single call gives up after 100 ms, so a
      first-call failure means "render took longer than 100 ms", not "overflow"),
    - a physical framebuffer capture,
    - a known-good recovery frame afterwards.

  The sweep stops at the first anomaly. Counts are bounded so the console is never
  driven far past an unknown limit.

  Expected command counts are computed from SDL's default POINTS line method:
  a diagonal segment rasterizes to max(|dx|,|dy|)+1 pixels, each pixel becomes one
  1x1 quad, and each quad is two independent triangles of three 32-byte vertex
  commands. SDL owns endpoint inclusion; for a standalone line call draw_last is
  true, so the full pixel count is queued.
*/

#include <SDL.h>

#include <stdio.h>
#include <time.h>

#include <dc/pvr.h>
#include <dc/video.h>

#include "../../pvr_hardware_screenshot.h"

#define OUTPUT_W 640
#define OUTPUT_H 480

/* Standalone line calls, one frame per count. */
static const int SWEEP[] = { 1, 2, 4, 6, 7, 8 };

/* The probe segment endpoints. Chosen so max(|dx|,|dy|)+1 = 575 pixels per call
 * and so parallel copies offset by 24 pixels never overlap, which keeps each
 * call's rendered pixel count independently measurable. */
#define X1 32
#define Y1 100
#define X2 606
#define Y2 200
#define LINE_OFFSET 24

#define MAX_RENDER_WAIT_ATTEMPTS 10

/* Measured hardware vertex-buffer storage: about 20 bytes per 32-byte vertex
 * command, plus a small fixed base. Predicted usage is only a model; the
 * register high-water mark is the fact. */
#define VERTEX_BYTES_PER_COMMAND 20
#define VERTEX_BASE_BYTES 76

/* One distinct color per line call, so overlapping lines cannot hide dropped
 * geometry. Values are RGB565-exact so the capture matches them directly. */
static const int PALETTE[8][3] = {
    {248, 0, 0}, {0, 248, 0}, {0, 0, 248}, {248, 248, 0},
    {248, 0, 248}, {0, 248, 248}, {128, 0, 248}, {248, 128, 0}
};

static unsigned long long now_ns(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long) t.tv_sec * 1000000000ull + (unsigned long long) t.tv_nsec;
}

static int bresenham_pixels(int x1, int y1, int x2, int y2)
{
    int dx = SDL_abs(x2 - x1);
    int dy = SDL_abs(y2 - y1);

    return (dx >= dy ? dx : dy) + 1;
}

static void snapshot_regs(const char *label, uint32_t regs[6])
{
    regs[0] = PVR_GET(PVR_TA_OPB_START);
    regs[1] = PVR_GET(PVR_TA_OPB_END);
    regs[2] = PVR_GET(PVR_TA_OPB_POS);
    regs[3] = PVR_GET(PVR_TA_VERTBUF_START);
    regs[4] = PVR_GET(PVR_TA_VERTBUF_END);
    regs[5] = PVR_GET(PVR_TA_VERTBUF_POS);

    printf("probe %-14s opb %08lx..%08lx pos %08lx  vertbuf %08lx..%08lx pos %08lx\n",
           label, (unsigned long) regs[0], (unsigned long) regs[1], (unsigned long) regs[2],
           (unsigned long) regs[3], (unsigned long) regs[4], (unsigned long) regs[5]);
    fflush(stdout);
}

static void capture(const char *path)
{
#ifdef PVR_HARDWARE_SCREENSHOT
    if (!pvr_hardware_capture(path)) {
        printf("probe capture failed for %s\n", path);
    } else {
        printf("probe captured %s\n", path);
    }
    fflush(stdout);
#else
    (void) path;
#endif
}

static int run_one(SDL_Renderer *renderer, int calls)
{
    uint32_t regs[6];
    unsigned long long t0, t1;
    int pixels, quads, tris;
    int api_error = 0, present_error = 0, wait_error = 0;
    int first_wait_result = 0, attempts = 0;
    char path[128];

    pixels = bresenham_pixels(X1, Y1, X2, Y2);
    quads = pixels * calls;
    tris = quads * 2;

    printf("\nprobe calls=%d pixels_per_call=%d quads=%d triangles=%d vertex_commands=%d predicted_usage=%d\n",
           calls, pixels, quads, tris, quads * 6, quads * 6 * VERTEX_BYTES_PER_COMMAND + VERTEX_BASE_BYTES);
    for (int i = 0; i < calls; ++i) {
        printf("probe line %d color %d,%d,%d\n", i, PALETTE[i % 8][0], PALETTE[i % 8][1], PALETTE[i % 8][2]);
    }
    fflush(stdout);

    SDL_ClearError();
    SDL_SetRenderDrawColor(renderer, 0x20, 0x20, 0x40, 0xFF);
    if (SDL_RenderClear(renderer) < 0) {
        printf("probe clear error: %s\n", SDL_GetError());
        return 1;
    }

    /* Distinct from the clear color, and distinct per call, so overlapping
     * lines cannot hide dropped geometry. */
    t0 = now_ns();
    for (int i = 0; i < calls; ++i) {
        SDL_SetRenderDrawColor(renderer, (Uint8) PALETTE[i % 8][0],
                               (Uint8) PALETTE[i % 8][1], (Uint8) PALETTE[i % 8][2], 255);
        if (SDL_RenderDrawLine(renderer, X1, Y1 + LINE_OFFSET * i, X2, Y2 + LINE_OFFSET * i) < 0) {
            printf("probe draw error at call %d: %s\n", i, SDL_GetError());
            api_error = 1;
            break;
        }
    }
    t1 = now_ns();
    printf("probe submit %7.2f ms (%d calls)\n", (double) (t1 - t0) / 1000000.0, calls);
    fflush(stdout);

    snapshot_regs("after submit", regs);
    /* OPB_POS is reported in 4-byte units by the KOS debug convention; the raw
     * value is printed above so the scaling assumption is auditable. */
    printf("probe usage opb_bytes_reported=%ld vertbuf_bytes_reported=%ld vertbuf_full=%d\n",
           (long) ((regs[2] << 2) - regs[0]),
           (long) (regs[5] - regs[3]),
           regs[5] == regs[4] ? 1 : 0);
    printf("probe check_ready before present=%d\n", pvr_check_ready());

    SDL_ClearError();
    t0 = now_ns();
    SDL_RenderPresent(renderer);   /* void in SDL2: errors surface through SDL_GetError() */
    t1 = now_ns();
    present_error = SDL_GetError()[0] ? 1 : 0;
    printf("probe present %7.2f ms%s%s\n", (double) (t1 - t0) / 1000000.0,
           present_error ? " ERROR" : "", present_error ? SDL_GetError() : "");
    fflush(stdout);

    snapshot_regs("after present", regs);

    /* Measure the true render time. KOS's pvr_wait_render_done() gives up after
     * 100 ms without distinguishing "still rendering" from "never completes".
     * Calling it repeatedly until it succeeds separates the two. */
    t0 = now_ns();
    first_wait_result = pvr_wait_render_done();
    attempts = 1;
    while (first_wait_result < 0 && attempts < MAX_RENDER_WAIT_ATTEMPTS) {
        if (pvr_wait_render_done() < 0) {
            attempts++;
        } else {
            break;
        }
    }
    t1 = now_ns();
    wait_error = first_wait_result < 0 ? 1 : 0;
    printf("probe render wait first result=%d attempts=%d elapsed %7.2f ms\n",
           first_wait_result, attempts, (double) (t1 - t0) / 1000000.0);
    printf("probe check_ready after wait=%d\n", pvr_check_ready());
    fflush(stdout);

    snprintf(path, sizeof(path), "/pc/probe-line-%d.ppm", calls);
    capture(path);

    /* Known-good recovery frame: a cleared frame with a marker that must appear. */
    SDL_ClearError();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(renderer) < 0) {
        printf("probe recovery clear error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
    {
        SDL_Rect marker = { 300, 240, 40, 40 };

        if (SDL_RenderFillRect(renderer, &marker) < 0) {
            printf("probe recovery draw error: %s\n", SDL_GetError());
            return 1;
        }
    }
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (SDL_GetError()[0]) {
        printf("probe recovery present error: %s\n", SDL_GetError());
        return 1;
    }
    if (pvr_wait_render_done() < 0) {
        printf("probe recovery render did not complete\n");
        return 1;
    }
    snprintf(path, sizeof(path), "/pc/probe-line-%d-recovery.ppm", calls);
    capture(path);

    return api_error || present_error || wait_error;
}

int main(int argc, char *argv[])
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    int i, anomaly = 0;

    (void) argc;
    (void) argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("PVR line capacity probe", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    {
        SDL_RendererInfo info;

        if (SDL_GetRendererInfo(renderer, &info) == 0) {
            printf("probe renderer=%s\n", info.name);
        } else {
            printf("probe renderer=<unknown>\n");
        }
        fflush(stdout);
    }

    for (i = 0; i < (int) (sizeof(SWEEP) / sizeof(SWEEP[0])); ++i) {
        if (run_one(renderer, SWEEP[i]) != 0) {
            anomaly = 1;
            printf("probe stopping at calls=%d (first anomaly)\n", SWEEP[i]);
            break;
        }
    }

    SDL_ClearError();
    SDL_DestroyRenderer(renderer);
    if (SDL_GetError()[0]) {
        printf("probe renderer shutdown error: %s\n", SDL_GetError());
        anomaly = 1;
    } else {
        printf("probe renderer shutdown clean\n");
    }

    SDL_Quit();
    printf("probe done anomaly=%d\n", anomaly);
    fflush(stdout);
    return anomaly ? 1 : 0;
}
