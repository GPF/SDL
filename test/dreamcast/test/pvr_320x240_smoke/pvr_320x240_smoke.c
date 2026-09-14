/* Focused 320x240 smoke test for the native PVR renderer.
 *
 * Covers: full-target clear, opaque/translucent overlap, nearest and
 * bilinear texture filtering, viewport/clip edges, and target rendering
 * before the final screen pass.
 *
 * 320x240 is a supported video mode but requires only a smoke test — it is
 * not a separate renderer acceptance phase.
 */

#include <SDL.h>
#include <kos.h>

#include <stdio.h>
#include <string.h>

#define OUTPUT_W 320
#define OUTPUT_H 240
#define TARGET_W 64
#define TARGET_H 64
#define PAGE_COUNT 3
#ifndef PAGE_HOLD_MS
#define PAGE_HOLD_MS 6000
#endif

#define ALLOC_STAGE_HINT "SDL_DC_PVR_TEST_ALLOC_STAGE"
#define ALLOC_COUNTDOWN_HINT "SDL_DC_PVR_TEST_ALLOC_COUNTDOWN"

#ifdef DC_PVR_320X240_HARDWARE_SCREENSHOT
static const char *const capture_paths[PAGE_COUNT] = {
    "/pc/pvr_320x240-page1.ppm",
    "/pc/pvr_320x240-page2.ppm",
    "/pc/pvr_320x240-page3.ppm"
};
#endif

static int checks;
static int failures;

static int fail_call(const char *call)
{
    printf("FAIL: %s: %s\n", call, SDL_GetError());
    ++failures;
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return fail_call(#call); } while (0)

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

static SDL_Renderer *create_renderer(SDL_Window *window, int driver)
{
    SDL_Renderer *renderer;

    renderer = SDL_CreateRenderer(window, driver,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC |
                                  SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) {
        fail_call("SDL_CreateRenderer(320x240 PVR)");
    }
    return renderer;
}

static SDL_Texture *create_target(SDL_Renderer *renderer)
{
    return SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                             SDL_TEXTUREACCESS_TARGET, TARGET_W, TARGET_H);
}

static int set_color(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    return 0;
}

static int clear_to(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(set_color(renderer, r, g, b));
    REQUIRE(SDL_RenderClear(renderer));
    return 0;
}

static int fill(SDL_Renderer *renderer, int x, int y, int w, int h,
                Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Rect rect = { x, y, w, h };

    REQUIRE(set_color(renderer, r, g, b));
    REQUIRE(SDL_RenderFillRect(renderer, &rect));
    return 0;
}

static int copy_target(SDL_Renderer *renderer, SDL_Texture *texture,
                       int x, int y, int w, int h)
{
    SDL_Rect dst = { x, y, w, h };
    return SDL_RenderCopy(renderer, texture, NULL, &dst);
}

static int capture_page(SDL_Renderer *renderer, int page, const char *name,
                        const char *expectation)
{
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (*SDL_GetError()) {
        return fail_call("valid screen SDL_RenderPresent");
    }
    printf("PAGE %d/%d PRESENTED: %s\n", page + 1, PAGE_COUNT, name);
    printf("  VISUAL EXPECTATION: %s\n", expectation);
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
#ifdef DC_PVR_320X240_HARDWARE_SCREENSHOT
    if (vid_screen_shot(capture_paths[page]) < 0) {
        printf("  CAPTURE FAIL: %s\n", capture_paths[page]);
        ++failures;
        return -1;
    }
    printf("  CAPTURE OK: %s\n", capture_paths[page]);
#endif
    SDL_Delay(PAGE_HOLD_MS);
    return 0;
}

static int page1_full_clear_and_overlap(SDL_Renderer *renderer)
{
    printf("\nPAGE 1: full-target clear and opaque/translucent overlap\n");

    REQUIRE(clear_to(renderer, 0, 0, 0));
    REQUIRE(fill(renderer, 0, 0, 320, 240, 10, 10, 60));

    /* Opaque quad */
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255));
    {
        SDL_Rect r = { 80, 60, 80, 80 };
        REQUIRE(SDL_RenderFillRect(renderer, &r));
    }

    /* Translucent quad overlapping opaque */
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND));
    REQUIRE(SDL_SetRenderDrawColor(renderer, 60, 255, 60, 128));
    {
        SDL_Rect r = { 100, 80, 80, 80 };
        REQUIRE(SDL_RenderFillRect(renderer, &r));
    }

    return capture_page(renderer, 0, "full clear + overlap",
                        "black background, blue full-screen, red opaque quad, green translucent quad overlapping red");
}

static int page2_filtering_and_viewport(SDL_Renderer *renderer, void *user_data)
{
    (void)user_data;
    SDL_Rect viewport = { 10, 10, 280, 200 };
    SDL_Rect clip = { 40, 40, 200, 140 };
    SDL_Texture *nearest = NULL;
    SDL_Texture *linear = NULL;

    printf("\nPAGE 2: nearest/bilinear filtering and viewport/clip edges\n");

    /* Create filtering targets BEFORE any screen draw to respect
     * the ordered-render-target contract. */
    nearest = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_TARGET, TARGET_W, TARGET_H);
    if (!nearest) {
        return fail_call("create nearest target");
    }
    SDL_SetTextureScaleMode(nearest, SDL_ScaleModeNearest);

    linear = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_TARGET, TARGET_W, TARGET_H);
    if (!linear) {
        SDL_DestroyTexture(nearest);
        return fail_call("create bilinear target");
    }
    SDL_SetTextureScaleMode(linear, SDL_ScaleModeLinear);

    /* Render to nearest target */
    REQUIRE(SDL_SetRenderTarget(renderer, nearest));
    REQUIRE(clear_to(renderer, 255, 128, 0));
    /* Draw a 4x4 checker in the target so scaling is visible */
    {
        int x, y;
        Uint8 r, g, b;
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                if ((x + y) & 1) {
                    r = 0; g = 0; b = 255;
                } else {
                    r = 255; g = 255; b = 0;
                }
                fill(renderer, x * 16, y * 16, 16, 16, r, g, b);
            }
        }
    }
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));

    /* Render to bilinear target */
    REQUIRE(SDL_SetRenderTarget(renderer, linear));
    REQUIRE(clear_to(renderer, 0, 255, 128));
    {
        int x, y;
        Uint8 r, g, b;
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                if ((x + y) & 1) {
                    r = 0; g = 0; b = 255;
                } else {
                    r = 255; g = 255; b = 0;
                }
                fill(renderer, x * 16, y * 16, 16, 16, r, g, b);
            }
        }
    }
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));

    /* Now switch to screen and draw — all target work is done. */
    REQUIRE(SDL_RenderSetViewport(renderer, &viewport));
    REQUIRE(SDL_RenderSetClipRect(renderer, &clip));

    /* Clear viewport area */
    REQUIRE(clear_to(renderer, 5, 5, 15));

    /* Draw a checkerboard on screen so filtering differences are visible */
    {
        int x, y;
        Uint8 r, g, b;
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                if ((x + y) & 1) {
                    r = 255; g = 255; b = 255;
                } else {
                    r = 0; g = 0; b = 0;
                }
                fill(renderer, 50 + x * 24, 50 + y * 24, 24, 24, r, g, b);
            }
        }
    }

    /* Copy nearest-scaled target to screen — exercises nearest filtering */
    {
        SDL_Rect dst = { 10, 140, 64, 64 };
        REQUIRE(SDL_RenderCopy(renderer, nearest, NULL, &dst));
    }

    /* Copy bilinear-scaled target to screen — exercises bilinear filtering */
    {
        SDL_Rect dst = { 90, 140, 64, 64 };
        REQUIRE(SDL_RenderCopy(renderer, linear, NULL, &dst));
    }

    SDL_DestroyTexture(nearest);
    SDL_DestroyTexture(linear);

    return capture_page(renderer, 1, "filtering + viewport/clip",
                        "blue viewport area with white/black checker pattern, clip edges at (40,40)-(240,180), nearest-scaled texture at left (sharp edges), bilinear-scaled texture at right (smooth edges)");
}

static int page3_target_before_screen(SDL_Renderer *renderer)
{
    SDL_Texture *target;
    SDL_Rect viewport = { 0, 0, 320, 240 };

    printf("\nPAGE 3: target rendering before final screen pass\n");

    target = create_target(renderer);
    if (!target) {
        return fail_call("create page3 target");
    }

    /* Render to target first */
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    REQUIRE(clear_to(renderer, 20, 60, 20));
    REQUIRE(fill(renderer, 0, 0, 32, 32, 255, 255, 0));
    REQUIRE(fill(renderer, 32, 32, 32, 32, 0, 255, 255));
    REQUIRE(fill(renderer, 0, 32, 32, 32, 255, 0, 255));
    REQUIRE(fill(renderer, 32, 0, 32, 32, 0, 0, 255));

    /* Now render to screen */
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(SDL_RenderSetViewport(renderer, &viewport));
    REQUIRE(clear_to(renderer, 0, 0, 0));
    REQUIRE(copy_target(renderer, target, 128, 80, 128, 128));

    SDL_DestroyTexture(target);
    return capture_page(renderer, 2, "target -> screen",
                        "black screen with 2x2 colored corner pattern (128x128) copied from target in the center");
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *target = NULL;
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
        printf("FAIL: Dreamcast PVR renderer not found\n");
        goto done;
    }
    window = SDL_CreateWindow("PVR 320x240 smoke test", 0, 0,
                              OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        printf("FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        goto done;
    }
    renderer = create_renderer(window, driver);
    if (!renderer) {
        goto done;
    }

    target = create_target(renderer);
    if (!target) {
        goto done;
    }

    if (page1_full_clear_and_overlap(renderer) < 0 ||
        page2_filtering_and_viewport(renderer, target) < 0 ||
        page3_target_before_screen(renderer) < 0) {
        goto done;
    }

    printf("\n=== 320x240 SMOKE SUMMARY ===\n");
    printf("Checks: %d  Failures: %d\n", checks, failures);
    printf("%s\n", failures ? "FAIL" : "PASS");
    rc = failures ? 1 : 0;

done:
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
