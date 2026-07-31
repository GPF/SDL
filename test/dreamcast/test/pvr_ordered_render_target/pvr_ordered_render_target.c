/* Focused physical-Dreamcast validation for ordered native-PVR render targets. */

#include <SDL.h>
#include <kos.h>

#include <stdio.h>
#include <string.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define TARGET_W 128
#define TARGET_H 128
#define TARGET_PAIR_BYTES ((size_t)TARGET_W * TARGET_H * 2 * 2)
#define PAGE_COUNT 5
#ifndef PAGE_HOLD_MS
#define PAGE_HOLD_MS 6000
#endif

#define ALLOC_STAGE_HINT "SDL_DC_PVR_TEST_ALLOC_STAGE"
#define ALLOC_COUNTDOWN_HINT "SDL_DC_PVR_TEST_ALLOC_COUNTDOWN"
#define ORDER_ERROR_TEXT "render targets must be rendered before the screen pass"

#ifdef DC_PVR_ORDERED_RENDER_TARGET_HARDWARE_SCREENSHOT
static const char *const capture_paths[PAGE_COUNT] = {
    "/pc/pvr_ordered_render_target-page1.ppm",
    "/pc/pvr_ordered_render_target-page2.ppm",
    "/pc/pvr_ordered_render_target-page3.ppm",
    "/pc/pvr_ordered_render_target-page4.ppm",
    "/pc/pvr_ordered_render_target-page5.ppm"
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

static void check(SDL_bool condition, const char *description)
{
    ++checks;
    if (condition) {
        printf("  API PASS: %s\n", description);
    } else {
        ++failures;
        printf("  API FAIL: %s (SDL error: %s)\n", description, SDL_GetError());
    }
}

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

static SDL_Renderer *create_renderer(SDL_Window *window, int driver,
                                     SDL_bool batching)
{
    SDL_Renderer *renderer;

    SDL_SetHint(SDL_HINT_RENDER_BATCHING, batching ? "1" : "0");
    renderer = SDL_CreateRenderer(window, driver,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) {
        fail_call("SDL_CreateRenderer(ordered PVR)");
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

static int draw_triangle(SDL_Renderer *renderer)
{
    const SDL_Vertex vertices[3] = {
        { { 40.0f, 340.0f }, { 20, 90, 220, 255 }, { 0.0f, 0.0f } },
        { { 280.0f, 340.0f }, { 20, 90, 220, 255 }, { 0.0f, 0.0f } },
        { { 160.0f, 80.0f }, { 20, 90, 220, 255 }, { 0.0f, 0.0f } }
    };

    return SDL_RenderGeometry(renderer, NULL, vertices, 3, NULL, 0);
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
#ifdef DC_PVR_ORDERED_RENDER_TARGET_HARDWARE_SCREENSHOT
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

static void check_rejected_target(SDL_Renderer *renderer, SDL_Texture *target,
                                  const char *description)
{
    int result;

    SDL_ClearError();
    result = SDL_SetRenderTarget(renderer, target);
    check(result < 0, description);
    check(SDL_strstr(SDL_GetError(), ORDER_ERROR_TEXT) != NULL,
          "ordering rejection sets the expected SDL error");
    check(SDL_GetRenderTarget(renderer) == NULL,
          "rejected target switch leaves SDL on the screen");
}

static int allocation_checks(SDL_Renderer *renderer)
{
    SDL_RendererInfo info;
    SDL_Texture *failed;
    SDL_Texture *probe;
    size_t baseline;
    size_t after_failed;
    size_t after_probe;
    size_t after_destroy;

    printf("\n=== CAPABILITY AND VRAM CHECKS ===\n");
    REQUIRE(SDL_GetRendererInfo(renderer, &info));
    check((info.flags & SDL_RENDERER_TARGETTEXTURE) != 0,
          "ordered renderer instance advertises TARGETTEXTURE");

    baseline = (size_t)pvr_mem_available();
    printf("  VRAM after ordered renderer creation: %zu\n", baseline);
    check(baseline > 5U * 1024U * 1024U,
          "ordered renderer creation did not reserve a 2 MiB logical screen pair");

    SDL_SetHint(ALLOC_STAGE_HINT, "vram");
    SDL_SetHint(ALLOC_COUNTDOWN_HINT, "1");
    SDL_ClearError();
    failed = create_target(renderer);
    check(failed == NULL, "injected second target allocation fails");
    after_failed = (size_t)pvr_mem_available();
    printf("  VRAM after injected failure: %zu (delta %zu)\n",
           after_failed, baseline - after_failed);
    check(after_failed <= baseline && baseline - after_failed <= 128U,
          "failed second target allocation rolls back payload without a leak");
    SDL_SetHint(ALLOC_STAGE_HINT, "none");
    SDL_SetHint(ALLOC_COUNTDOWN_HINT, "0");
    SDL_ClearError();

    probe = create_target(renderer);
    if (!probe) {
        return fail_call("create allocation probe target");
    }
    after_probe = (size_t)pvr_mem_available();
    printf("  One %dx%d target pair consumed: %zu bytes (expected %zu)\n",
           TARGET_W, TARGET_H, baseline - after_probe, TARGET_PAIR_BYTES);
    check(baseline - after_probe >= TARGET_PAIR_BYTES &&
              baseline - after_probe <= TARGET_PAIR_BYTES + 128U,
          "target consumes two RGB565 POT allocations plus allocator metadata");
    SDL_DestroyTexture(probe);
    after_destroy = (size_t)pvr_mem_available();
    printf("  VRAM after probe destruction: %zu (delta %zu)\n",
           after_destroy, baseline - after_destroy);
    check(after_destroy <= baseline && baseline - after_destroy <= 128U,
          "destroyed target restores VRAM payload without a leak");
    return 0;
}

static int page_allowed(SDL_Renderer *renderer, SDL_Texture *a, SDL_Texture *b)
{
    SDL_Rect viewport = { 3, 5, 620, 450 };
    SDL_Rect clip = { 0, 0, 620, 450 };

    printf("\nPAGE 1: allowed multi-target order and target preservation\n");
    REQUIRE(SDL_SetRenderTarget(renderer, a));
    REQUIRE(clear_to(renderer, 100, 10, 15));
    REQUIRE(fill(renderer, 0, 0, 32, 32, 0, 240, 40));
    check(SDL_SetRenderTarget(renderer, a) == 0,
          "repeated selection of target A is a no-op");

    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(SDL_RenderSetViewport(renderer, &viewport));
    REQUIRE(SDL_RenderSetClipRect(renderer, &clip));
    REQUIRE(SDL_RenderFlush(renderer));
    REQUIRE(SDL_SetRenderTarget(renderer, b));
    REQUIRE(clear_to(renderer, 10, 20, 120));
    REQUIRE(fill(renderer, 96, 0, 32, 32, 255, 220, 0));

    REQUIRE(SDL_SetRenderTarget(renderer, a));
    REQUIRE(fill(renderer, 96, 96, 32, 32, 255, 255, 0));
    REQUIRE(SDL_SetRenderTarget(renderer, b));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    check(SDL_GetRenderTarget(renderer) == NULL,
          "empty target-to-screen queue switch succeeds");
    REQUIRE(SDL_RenderSetViewport(renderer, NULL));
    REQUIRE(SDL_RenderSetClipRect(renderer, NULL));
    check(SDL_SetRenderTarget(renderer, NULL) == 0,
          "repeated screen selection is a no-op");

    REQUIRE(clear_to(renderer, 6, 8, 18));
    REQUIRE(copy_target(renderer, a, 40, 80, 240, 240));
    REQUIRE(copy_target(renderer, b, 360, 80, 240, 240));
    REQUIRE(fill(renderer, 0, 440, 640, 40, 0, 150, 40));
    return capture_page(renderer, 0, "allowed order",
                        "red A at left retains green/yellow corners; blue B at right retains yellow corner; green footer");
}

static int page_clear_rejection(SDL_Renderer *renderer, SDL_Texture *target)
{
    printf("\nPAGE 2: screen clear rejection and same-frame recovery\n");
    REQUIRE(clear_to(renderer, 8, 8, 20));
    REQUIRE(fill(renderer, 40, 80, 220, 280, 200, 20, 20));
    check_rejected_target(renderer, target,
                          "screen clear followed by target selection is rejected");
    REQUIRE(fill(renderer, 380, 80, 220, 280, 20, 190, 40));
    return capture_page(renderer, 1, "clear rejection recovery",
                        "red left panel and green right panel both survive the rejected switch");
}

static int page_next_frame(SDL_Renderer *renderer, SDL_Texture *target)
{
    printf("\nPAGE 3: target selection is restored by screen present\n");
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    REQUIRE(clear_to(renderer, 80, 10, 100));
    REQUIRE(fill(renderer, 32, 32, 64, 64, 255, 180, 0));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 4, 8, 16));
    REQUIRE(copy_target(renderer, target, 160, 40, 320, 320));
    return capture_page(renderer, 2, "next-frame target recovery",
                        "purple target with centered orange square, proving selection succeeded after present");
}

static int page_flush_rejection(SDL_Renderer *renderer, SDL_Texture *target)
{
    printf("\nPAGE 4: explicit flush is not a frame boundary\n");
    REQUIRE(draw_triangle(renderer));
    REQUIRE(SDL_RenderFlush(renderer));
    check_rejected_target(renderer, target,
                          "screen geometry then SDL_RenderFlush then target is rejected");
    REQUIRE(fill(renderer, 360, 100, 240, 240, 220, 80, 20));
    return capture_page(renderer, 3, "flush rejection recovery",
                        "blue left triangle and orange right panel survive; flush did not reset ordering");
}

static int page_nonbatched_and_target_present(SDL_Renderer *renderer)
{
    SDL_Texture *target = create_target(renderer);
    SDL_Texture *destroy_selected;

    printf("\nPAGE 5: target-selected present, non-batched rejection, lifecycle recovery\n");
    if (!target) {
        return fail_call("create page 5 target");
    }
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    REQUIRE(clear_to(renderer, 10, 80, 90));
    REQUIRE(fill(renderer, 32, 32, 64, 64, 240, 240, 0));
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    check(SDL_strstr(SDL_GetError(), "cannot present while a render target is selected") != NULL,
          "target-selected present sets a clear SDL error");
    check(SDL_GetRenderTarget(renderer) == target,
          "target-selected present leaves the target selected for recovery");
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));

    destroy_selected = create_target(renderer);
    if (!destroy_selected) {
        SDL_DestroyTexture(target);
        return fail_call("create selected-destruction target");
    }
    REQUIRE(SDL_SetRenderTarget(renderer, destroy_selected));
    REQUIRE(clear_to(renderer, 30, 30, 30));
    SDL_DestroyTexture(destroy_selected);
    check(SDL_GetRenderTarget(renderer) == NULL,
          "destroying the selected target safely restores the screen");

    REQUIRE(copy_target(renderer, target, 160, 40, 320, 320));
    check_rejected_target(renderer, target,
                          "non-batched screen geometry immediately closes target selection");
    REQUIRE(fill(renderer, 20, 400, 600, 40, 0, 150, 40));
    if (capture_page(renderer, 4, "target-present and lifecycle recovery",
                     "teal target with yellow center and green footer after target-present and selected-destroy recovery") < 0) {
        SDL_DestroyTexture(target);
        return -1;
    }
    SDL_DestroyTexture(target);
    return 0;
}

static int incomplete_destruction_checks(SDL_Window *window, int driver)
{
    SDL_Renderer *renderer;
    SDL_Texture *target;

    printf("\n=== INCOMPLETE-FRAME DESTRUCTION CHECKS ===\n");
    renderer = create_renderer(window, driver, SDL_TRUE);
    if (!renderer) {
        return -1;
    }
    target = create_target(renderer);
    if (!target) {
        SDL_DestroyRenderer(renderer);
        return fail_call("create incomplete target");
    }
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    REQUIRE(clear_to(renderer, 20, 20, 20));
    SDL_DestroyRenderer(renderer);
    check(SDL_TRUE, "renderer destruction with incomplete target work returned");

    renderer = create_renderer(window, driver, SDL_TRUE);
    if (!renderer) {
        return -1;
    }
    REQUIRE(clear_to(renderer, 20, 20, 20));
    SDL_DestroyRenderer(renderer);
    check(SDL_TRUE, "renderer destruction with incomplete screen work returned");
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *a = NULL;
    SDL_Texture *b = NULL;
    int driver;
    int rc = 1;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    driver = find_pvr_driver();
    if (driver < 0) {
        printf("FAIL: Dreamcast PVR renderer not found\n");
        goto done;
    }
    window = SDL_CreateWindow("PVR ordered render targets", 0, 0,
                              OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        printf("FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        goto done;
    }
    renderer = create_renderer(window, driver, SDL_TRUE);
    if (!renderer || allocation_checks(renderer) < 0) {
        goto done;
    }
    a = create_target(renderer);
    b = create_target(renderer);
    if (!a || !b) {
        fail_call("create page targets");
        goto done;
    }
    if (page_allowed(renderer, a, b) < 0 ||
        page_clear_rejection(renderer, a) < 0 ||
        page_next_frame(renderer, a) < 0 ||
        page_flush_rejection(renderer, a) < 0) {
        goto done;
    }
    SDL_DestroyTexture(a);
    SDL_DestroyTexture(b);
    a = NULL;
    b = NULL;
    SDL_DestroyRenderer(renderer);
    renderer = create_renderer(window, driver, SDL_FALSE);
    if (!renderer || page_nonbatched_and_target_present(renderer) < 0) {
        goto done;
    }
    SDL_DestroyRenderer(renderer);
    renderer = NULL;
    if (incomplete_destruction_checks(window, driver) < 0) {
        goto done;
    }

    printf("\n=== ORDERED RTT SUMMARY ===\n");
    printf("Checks: %d  Failures: %d\n", checks, failures);
    printf("%s\n", failures ? "FAIL" : "PASS");
    rc = failures ? 1 : 0;

done:
    SDL_DestroyTexture(a);
    SDL_DestroyTexture(b);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
