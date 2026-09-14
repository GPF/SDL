/*
 * Bounded VRAM-pressure and allocation-rollback test for the guarded
 * Dreamcast PVR render-target prototype.
 *
 * Builds only against the prototype archive, validates clean exhaustion,
 * rollback, and recovery of target-texture allocations on real hardware.
 */

#include <SDL.h>
#include <kos.h>

#include <stdio.h>
#include <string.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define PAGE_HOLD_MS 12000

/* Maximum target textures in any single allocation phase. */
#define MAX_TARGETS 32

/* Maximum mixed-size allocations before rollback. */
#define MAX_MIXED_TARGETS 24

/* Maximum repeated exhaust → free → recovery cycles. */
#define MAX_ROLLBACK_CYCLES 10

/* Per-target cost for 256x256 RGB565: two POT allocations. */
#define TARGET_W 256
#define TARGET_H 256
#define TARGET_BACKING_W 256
#define TARGET_BACKING_H 256
#define TARGET_BYTES_PER_ALLOCATION ((size_t)TARGET_BACKING_W * TARGET_BACKING_H * 2)
#define TARGET_DUAL_COST (TARGET_BYTES_PER_ALLOCATION * 2)

#ifdef DC_PVR_RENDER_TARGET_PROTOTYPE_HARDWARE_SCREENSHOT
static const char *capture_path = "/pc/pvr_render_target_stress-hardware.ppm";
#endif

static int checks;
static int failures;

static int fail_message(const char *operation)
{
    printf("FAIL: %s: %s\n", operation, SDL_GetError());
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return fail_message(#call); } while (0)

static void assertion_check(SDL_bool condition, const char *description)
{
    ++checks;
    if (condition) {
        printf("  CHECK PASS: %s\n", description);
    } else {
        ++failures;
        printf("  CHECK FAIL: %s: %s\n", description, SDL_GetError());
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
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

static int clear_to(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderClear(renderer));
    return 0;
}

static int fill_rect(SDL_Renderer *renderer,
                     int x, int y, int w, int h,
                     Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Rect rect = { x, y, w, h };
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderFillRect(renderer, &rect));
    return 0;
}

static SDL_Texture *create_target(SDL_Renderer *renderer, int w, int h)
{
    return SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                             SDL_TEXTUREACCESS_TARGET, w, h);
}

static int present_and_wait(SDL_Renderer *renderer)
{
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (*SDL_GetError()) {
        return fail_message("SDL_RenderPresent");
    }
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
    return 0;
}

static int screenshot(void)
{
#ifdef DC_PVR_RENDER_TARGET_PROTOTYPE_HARDWARE_SCREENSHOT
    if (vid_screen_shot(capture_path) < 0) {
        printf("  CAPTURE FAIL: %s\n", capture_path);
        return -1;
    }
    printf("  CAPTURE OK: %s\n", capture_path);
#endif
    return 0;
}

/* ------------------------------------------------------------------ */
/* Phase 1: baseline VRAM and renderer info                           */
/* ------------------------------------------------------------------ */

static int phase_baseline(SDL_Renderer *renderer)
{
    SDL_RendererInfo info;
    size_t initial_vram;

    printf("\n=== PHASE 1: BASELINE VRAM AND RENDERER INFO ===\n");

    initial_vram = (size_t)pvr_mem_available();
    printf("  Initial pvr_mem_available(): %zu bytes\n", initial_vram);

    REQUIRE(SDL_GetRendererInfo(renderer, &info));
    printf("  Renderer: %s\n", info.name);
    printf("  Flags:    0x%08lx\n", (unsigned long)info.flags);
    printf("  Output:   %d x %d\n", OUTPUT_W, OUTPUT_H);

    assertion_check(info.name && SDL_strcmp(info.name, "Dreamcast PVR") == 0,
                    "renderer is Dreamcast PVR");
    assertion_check((info.flags & SDL_RENDERER_TARGETTEXTURE) != 0,
                    "renderer advertises TARGETTEXTURE");

    /* Per-target allocation cost for TARGET_W x TARGET_H RGB565:
     *   backing_w = 256 (POT of 256)
     *   backing_h = 256 (POT of 256)
     *   one allocation = 256 * 256 * 2 = 131,072 bytes
     *   dual (current + next) = 262,144 bytes */
    printf("  Per-target allocation cost (dual RGB565 256x256): %zu bytes\n",
           TARGET_DUAL_COST);
    printf("  Max targets in exhaustion phase: %d\n", MAX_TARGETS);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Phase 2: bounded exhaustion                                        */
/* ------------------------------------------------------------------ */

static int phase_exhaustion(SDL_Renderer *renderer)
{
    SDL_Texture *targets[MAX_TARGETS];
    size_t phase_baseline, pre_vram, post_vram;
    int i;
    int successful_count = 0;
    int exhausted = 0;

    printf("\n=== PHASE 2: BOUNDED VRAM EXHAUSTION ===\n");
    printf("  Creating up to %d 256x256 RGB565 target textures...\n",
           MAX_TARGETS);

    phase_baseline = (size_t)pvr_mem_available();
    printf("  VRAM before exhaustion phase: %zu bytes\n", phase_baseline);

    SDL_ClearError();
    for (i = 0; i < MAX_TARGETS; ++i) {
        SDL_Texture *t;
        size_t attempt_vram = (size_t)pvr_mem_available();

        t = create_target(renderer, TARGET_W, TARGET_H);

        if (!t) {
            size_t failed_vram = (size_t)pvr_mem_available();

            /* Clean exhaustion: SDL_CreateTexture failed. */
            printf("  Allocation %d FAILED: %s\n", i + 1, SDL_GetError());
            printf("  VRAM before failed create: %zu bytes\n", attempt_vram);
            printf("  VRAM after failed create:  %zu bytes\n", failed_vram);
            if (failed_vram != attempt_vram) {
                printf("  FAILED CREATE LEAK: before=%zu after=%zu\n",
                       attempt_vram, failed_vram);
                ++failures;
            } else {
                printf("  Failed create rollback: exact\n");
            }
            exhausted = 1;
            SDL_ClearError();
            break;
        }

        targets[successful_count] = t;
        ++successful_count;

        printf("  Allocation %d/32 SUCCESS: %s\n",
               successful_count, SDL_GetError() ? "error cleared" : "OK");
        SDL_ClearError();
    }

    if (!exhausted && successful_count == MAX_TARGETS) {
        printf("  Reached max target count (%d) without exhaustion.\n",
               MAX_TARGETS);
    }

    post_vram = (size_t)pvr_mem_available();
    printf("  VRAM after exhaustion phase: %zu bytes\n", post_vram);
    printf("  VRAM consumed by phase: %zu bytes\n",
           phase_baseline - post_vram);

    if (exhausted) {
        printf("  CLEAN EXHAUSTION confirmed: SDL_CreateTexture returned NULL.\n");
        printf("  Expected consumption: ~%zu bytes (%d targets x %zu dual)\n",
               successful_count * TARGET_DUAL_COST,
               successful_count, TARGET_DUAL_COST);
    }

    /* Phase 2a: for each successfully created target, select, draw marker,
     * reset to screen. Never select a target that was never created. */
    printf("\n  Phase 2a: exercising %d successfully created targets...\n",
           successful_count);
    for (i = 0; i < successful_count; ++i) {
        REQUIRE(SDL_SetRenderTarget(renderer, targets[i]));
        REQUIRE(clear_to(renderer, 40 + (i % 4) * 30,
                         10 + (i % 8), 20));
        REQUIRE(fill_rect(renderer, 4, 4, 16, 16,
                          (Uint8)(30 + i * 10),
                          (Uint8)(60 + i * 5), 200));
        REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    }

    /* Finish all queued RTT work before any target allocation is released. */
    REQUIRE(clear_to(renderer, 5, 8, 15));
    REQUIRE(present_and_wait(renderer));

    /* Phase 2b: free all targets and check VRAM rollback. */
    printf("\n  Phase 2b: freeing %d targets and checking rollback...\n",
           successful_count);
    pre_vram = (size_t)pvr_mem_available();

    for (i = 0; i < successful_count; ++i) {
        SDL_DestroyTexture(targets[i]);
        targets[i] = NULL;
    }

    post_vram = (size_t)pvr_mem_available();
    printf("  VRAM before free: %zu bytes\n", pre_vram);
    printf("  VRAM after free:  %zu bytes\n", post_vram);
    printf("  VRAM recovered:   %zu bytes\n", post_vram - pre_vram);

    if (post_vram != phase_baseline) {
        printf("  VRAM BASELINE MISMATCH: expected=%zu actual=%zu\n",
               phase_baseline, post_vram);
        ++failures;
    } else {
        printf("  VRAM fully recovered to the exact phase baseline.\n");
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Phase 3: known-good recovery after exhaustion                      */
/* ------------------------------------------------------------------ */

static int phase_recovery(SDL_Renderer *renderer)
{
    SDL_Texture *t = NULL;

    printf("\n=== PHASE 3: KNOWN-GOOD RECOVERY ===\n");

    {
        size_t pre = (size_t)pvr_mem_available();
        printf("  VRAM before recovery create: %zu bytes\n", pre);

        t = create_target(renderer, 128, 128);
    }

    if (!t) {
        printf("  RECOVERY FAIL: 128x128 target creation failed after rollback.\n");
        printf("  SDL error: %s\n", SDL_GetError());
        return -1;
    }
    printf("  Recovery create SUCCESS: 128x128 RGB565 target created.\n");
    assertion_check(t != NULL, "recovery target pointer is non-NULL");
    SDL_ClearError();

    /* Render into the recovery target. */
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 20, 20, 40));
    REQUIRE(fill_rect(renderer, 16, 16, 96, 96, 255, 120, 30));
    REQUIRE(fill_rect(renderer, 40, 40, 48, 48, 30, 120, 255));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));

    /* Sample recovery target to screen. */
    REQUIRE(fill_rect(renderer, 0, 0, OUTPUT_W, OUTPUT_H, 5, 8, 15));
    {
        SDL_Rect dst = { 60, 60, 360, 360 };
        REQUIRE(SDL_RenderCopy(renderer, t, NULL, &dst));
    }

    /* Present and synchronize before releasing the sampled target. The
     * single retained hardware screenshot is produced by phase 6. */
    REQUIRE(present_and_wait(renderer));

    {
        size_t pre = (size_t)pvr_mem_available();
        SDL_DestroyTexture(t);
        size_t post = (size_t)pvr_mem_available();
        printf("  VRAM before destroy: %zu bytes\n", pre);
        printf("  VRAM after destroy:  %zu bytes\n", post);
        printf("  VRAM delta:          %zu bytes\n", post - pre);
    }

    /* Final low-load screen recovery frame. */
    printf("  Final recovery frame:\n");
    REQUIRE(clear_to(renderer, 8, 12, 24));
    REQUIRE(fill_rect(renderer, 200, 200, 240, 80, 0, 180, 40));
    REQUIRE(fill_rect(renderer, 10, 400, 620, 12, 0, 160, 40));
    REQUIRE(present_and_wait(renderer));
    printf("  Recovery frame presented.\n");

    return 0;
}

/* ------------------------------------------------------------------ */
/* Phase 4: repeated rollback cycles                                  */
/* ------------------------------------------------------------------ */

static int phase_rollback_cycles(SDL_Renderer *renderer)
{
    SDL_Texture *targets[MAX_TARGETS];
    int cycle;
    size_t initial_baseline;

    printf("\n=== PHASE 4: REPEATED ROLLBACK CYCLES ===\n");
    initial_baseline = (size_t)pvr_mem_available();
    printf("  Fixed cycle baseline: %zu bytes\n", initial_baseline);

    for (cycle = 0; cycle < MAX_ROLLBACK_CYCLES; ++cycle) {
        size_t pre_vram, post_vram;
        int i;
        int successful_count = 0;
        int exhausted = 0;

        printf("\n  Cycle %d/%d:\n", cycle + 1, MAX_ROLLBACK_CYCLES);
        pre_vram = (size_t)pvr_mem_available();

        printf("    Baseline VRAM: %zu bytes\n", initial_baseline);
        printf("    Pre-create VRAM: %zu bytes\n", pre_vram);
        if (pre_vram != initial_baseline) {
            printf("    PRE-CYCLE BASELINE MISMATCH: expected=%zu actual=%zu\n",
                   initial_baseline, pre_vram);
            ++failures;
        }

        /* Allocate targets until exhaustion or max. */
        SDL_ClearError();
        for (i = 0; i < MAX_TARGETS && !exhausted; ++i) {
            SDL_Texture *t = create_target(renderer, TARGET_W, TARGET_H);

            if (!t) {
                printf("    Allocation %d FAILED: %s\n",
                       i + 1, SDL_GetError());
                exhausted = 1;
                SDL_ClearError();
                break;
            }
            targets[successful_count] = t;
            ++successful_count;
        }

        if (!exhausted && successful_count == MAX_TARGETS) {
            printf("    Reached max (%d) without exhaustion.\n", MAX_TARGETS);
        }

        post_vram = (size_t)pvr_mem_available();
        printf("    Post-create VRAM: %zu bytes\n", post_vram);
        printf("    VRAM consumed:    %zu bytes\n", pre_vram - post_vram);
        printf("    Targets created:  %d\n", successful_count);

        /* Free all successfully created targets. */
        for (i = 0; i < successful_count; ++i) {
            SDL_DestroyTexture(targets[i]);
            targets[i] = NULL;
        }

        post_vram = (size_t)pvr_mem_available();
        printf("    Post-free VRAM:   %zu bytes\n", post_vram);
        printf("    VRAM recovered:   %zu bytes\n",
               post_vram - (pre_vram - (size_t)successful_count * TARGET_DUAL_COST));

        /* Detect any loss relative to the fixed pre-cycle baseline. */
        if (post_vram != initial_baseline) {
            printf("    POST-CYCLE BASELINE MISMATCH: expected=%zu actual=%zu\n",
                   initial_baseline, post_vram);
            ++failures;
        }

        /* Verify a small target can still be created. */
        {
            SDL_Texture *t = create_target(renderer, 32, 32);
            if (t) {
                SDL_DestroyTexture(t);
                printf("    Post-cycle sanity create: OK\n");
            } else {
                printf("    Post-cycle sanity create FAILED: %s\n",
                       SDL_GetError());
                ++failures;
            }
            SDL_ClearError();
        }
    }

    printf("\n  Initial baseline VRAM: %zu bytes\n", initial_baseline);
    printf("  Final baseline VRAM:   %zu bytes\n",
           (size_t)pvr_mem_available());

    return 0;
}

/* ------------------------------------------------------------------ */
/* Phase 5: mixed-size fragmentation probe                           */
/* ------------------------------------------------------------------ */

static int phase_mixed_size(SDL_Renderer *renderer)
{
    typedef struct { int w; int h; } SizePair;
    static const SizePair sizes[MAX_MIXED_TARGETS] = {
        { 256, 256 }, { 64, 64 },   { 128, 128 },
        { 256, 256 }, { 64, 64 },   { 128, 128 },
        { 256, 256 }, { 64, 64 },   { 128, 128 },
        { 256, 256 }, { 64, 64 },   { 128, 128 },
        { 256, 256 }, { 64, 64 },   { 128, 128 },
        { 256, 256 }, { 64, 64 },   { 128, 128 },
        { 256, 256 }, { 64, 64 },   { 128, 128 },
        { 256, 256 }, { 64, 64 },   { 128, 128 },
    };

    SDL_Texture *targets[MAX_MIXED_TARGETS];
    int i;
    int successful_count = 0;
    size_t pre_vram;

    printf("\n=== PHASE 5: MIXED-SIZE FRAGMENTATION PROBE ===\n");
    pre_vram = (size_t)pvr_mem_available();
    printf("  Pre-creation VRAM: %zu bytes\n", pre_vram);

    /* Create targets in alternating sizes. */
    for (i = 0; i < MAX_MIXED_TARGETS; ++i) {
        SDL_Texture *t = create_target(renderer, sizes[i].w, sizes[i].h);

        if (!t) {
            printf("  Mixed create %d (%dx%d) FAILED: %s\n",
                   i + 1, sizes[i].w, sizes[i].h, SDL_GetError());
            SDL_ClearError();
            break;
        }
        targets[successful_count] = t;
        ++successful_count;
        printf("  Mixed create %d (%dx%d): OK\n",
               i + 1, sizes[i].w, sizes[i].h);
    }

    printf("  Mixed-size successful creates: %d / %d\n",
           successful_count, MAX_MIXED_TARGETS);

    /* Destroy in deterministic non-creation order (reverse). */
    printf("  Destroying in reverse order...\n");
    for (i = successful_count - 1; i >= 0; --i) {
        SDL_DestroyTexture(targets[i]);
        targets[i] = NULL;
    }

    {
        size_t post_vram = (size_t)pvr_mem_available();
        printf("  Post-mixed VRAM: %zu bytes\n", post_vram);
        printf("  VRAM delta:      %zu bytes\n", pre_vram - post_vram);
    }

    /* Verify a 256x256 target still succeeds after mixed-size churn. */
    {
        SDL_Texture *t = create_target(renderer, 256, 256);
        int ok = 0;

        if (!t) {
            printf("  Post-mixed 256x256 create FAILED: %s\n",
                   SDL_GetError());
            ++failures;
        } else {
            printf("  Post-mixed 256x256 create: OK\n");
            ok = 1;
            REQUIRE(SDL_SetRenderTarget(renderer, t));
            REQUIRE(clear_to(renderer, 30, 20, 50));
            REQUIRE(fill_rect(renderer, 16, 16, 224, 224, 200, 200, 40));
            REQUIRE(SDL_SetRenderTarget(renderer, NULL));
            REQUIRE(clear_to(renderer, 5, 8, 15));
            REQUIRE(present_and_wait(renderer));
            SDL_DestroyTexture(t);
            printf("  Post-mixed 256x256 draw and destroy: OK\n");
        }
        assertion_check(ok, "post-mixed 256x256 create succeeded");
    }
    SDL_ClearError();

    return 0;
}

/* ------------------------------------------------------------------ */
/* Phase 6: final recovery screenshot                                 */
/* ------------------------------------------------------------------ */

static int phase_final_screenshot(SDL_Renderer *renderer)
{
    SDL_Texture *recovery_target = NULL;

    printf("\n=== PHASE 6: FINAL RECOVERY SCREENSHOT ===\n");

    recovery_target = create_target(renderer, 128, 128);
    if (!recovery_target) {
        printf("  Final recovery create FAILED: %s\n", SDL_GetError());
        return -1;
    }
    printf("  Final recovery target (128x128): OK\n");

    /* Render into the recovery target. */
    REQUIRE(SDL_SetRenderTarget(renderer, recovery_target));
    REQUIRE(clear_to(renderer, 10, 10, 20));
    /* Green corner markers. */
    REQUIRE(fill_rect(renderer, 0, 0, 16, 16, 0, 220, 40));
    REQUIRE(fill_rect(renderer, 112, 0, 16, 16, 0, 220, 40));
    REQUIRE(fill_rect(renderer, 0, 112, 16, 16, 0, 220, 40));
    REQUIRE(fill_rect(renderer, 112, 112, 16, 16, 0, 220, 40));
    /* Red center square. */
    REQUIRE(fill_rect(renderer, 40, 40, 48, 48, 200, 30, 30));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));

    /* Compose final screen: sampled target + green status bar + markers. */
    REQUIRE(clear_to(renderer, 8, 12, 24));

    /* Sampled recovery target in upper-left. */
    {
        SDL_Rect dst = { 40, 40, 360, 360 };
        REQUIRE(SDL_RenderCopy(renderer, recovery_target, NULL, &dst));
    }

    /* Green status bar at bottom. */
    REQUIRE(fill_rect(renderer, 20, 420, 600, 24, 0, 160, 40));

    /* Distinct corner markers (white). */
    REQUIRE(fill_rect(renderer, 10, 10, 12, 12, 255, 255, 255));
    REQUIRE(fill_rect(renderer, 618, 10, 12, 12, 255, 255, 255));
    REQUIRE(fill_rect(renderer, 10, 460, 12, 12, 255, 255, 255));
    REQUIRE(fill_rect(renderer, 618, 460, 12, 12, 255, 255, 255));

    REQUIRE(present_and_wait(renderer));
    if (screenshot() < 0) return -1;
    SDL_DestroyTexture(recovery_target);

    printf("  Final recovery frame presented and captured.\n");
    printf("  Expected visual: sampled 128x128 target (upper-left),\n");
    printf("    green status bar (bottom), white corner markers.\n");
    printf("    No missing geometry or corruption.\n");

    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    int driver;
    int rc = 1;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    driver = find_pvr_driver();
    if (driver < 0) {
        printf("FAIL: could not find Dreamcast PVR renderer driver\n");
        goto done;
    }

    window = SDL_CreateWindow("PVR RTT VRAM stress test", 0, 0,
                              OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        printf("FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        goto done;
    }

    renderer = SDL_CreateRenderer(window, driver,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) {
        printf("FAIL: SDL_CreateRenderer: %s\n", SDL_GetError());
        goto done;
    }

    /* Run all phases sequentially. */
    if (phase_baseline(renderer) < 0 ||
        phase_exhaustion(renderer) < 0 ||
        phase_recovery(renderer) < 0 ||
        phase_rollback_cycles(renderer) < 0 ||
        phase_mixed_size(renderer) < 0 ||
        phase_final_screenshot(renderer) < 0) {
        printf("\n=== EARLY TERMINATION ===\n");
        goto done;
    }

    printf("\n=== ALL PHASES COMPLETE ===\n");
    printf("Total checks: %d, failures: %d\n", checks, failures);
    printf("%s\n", failures ? "FAIL" : "PASS");

    rc = failures ? 1 : 0;

done:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
