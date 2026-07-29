/* Physical Dreamcast validation for the guarded native-PVR RTT prototype. */

#include <SDL.h>
#include <kos.h>

#include <stdio.h>
#include <string.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define PAGE_COUNT 12
#ifndef PAGE_HOLD_MS
#define PAGE_HOLD_MS 8000
#endif

#ifdef DC_PVR_RENDER_TARGET_HARDWARE_SCREENSHOT
static const char *const capture_paths[PAGE_COUNT] = {
    "/pc/pvr_render_target_phase5b_page1.ppm",
    "/pc/pvr_render_target_phase5b_page2.ppm",
    "/pc/pvr_render_target_phase5b_page3.ppm",
    "/pc/pvr_render_target_phase5b_page4.ppm",
    "/pc/pvr_render_target_phase5b_page5.ppm",
    "/pc/pvr_render_target_phase5b_page6.ppm",
    "/pc/pvr_render_target_phase5b_page7.ppm",
    "/pc/pvr_render_target_phase5b_page8.ppm",
    "/pc/pvr_render_target_phase5b_page9.ppm",
    "/pc/pvr_render_target_phase5b_page10.ppm",
    "/pc/pvr_render_target_phase5b_page11.ppm",
    "/pc/pvr_render_target_phase5b_page12.ppm"
};
#endif

static int checks;
static int failures;

static int failed_call(const char *call)
{
    printf("FAIL: %s: %s\n", call, SDL_GetError());
    ++failures;
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return failed_call(#call); } while (0)

static void check(SDL_bool condition, const char *description)
{
    ++checks;
    if (condition) {
        printf("  API PASS: %s\n", description);
    } else {
        ++failures;
        printf("  API FAIL: %s: %s\n", description, SDL_GetError());
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

static int color(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    return SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

static int fill(SDL_Renderer *renderer, int x, int y, int w, int h)
{
    SDL_Rect rect = { x, y, w, h };
    return SDL_RenderFillRect(renderer, &rect);
}

static int clear_to(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(color(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderClear(renderer));
    return 0;
}

static SDL_Texture *target(SDL_Renderer *renderer, int w, int h)
{
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                             SDL_TEXTUREACCESS_TARGET, w, h);
    if (!texture) {
        failed_call("SDL_CreateTexture(RGB565 TARGET)");
    }
    return texture;
}

static int screen_panel(SDL_Renderer *renderer, int x, int y, int w, int h,
                        Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(color(renderer, r, g, b, 255));
    REQUIRE(fill(renderer, x, y, w, h));
    return 0;
}

static int copy(SDL_Renderer *renderer, SDL_Texture *texture,
                int x, int y, int w, int h)
{
    SDL_Rect dst = { x, y, w, h };
    return SDL_RenderCopy(renderer, texture, NULL, &dst);
}

static int present_page(SDL_Renderer *renderer, int page, const char *name,
                        const char *expectation)
{
    SDL_RenderPresent(renderer);
    printf("PAGE %d/%d PRESENTED: %s\n", page + 1, PAGE_COUNT, name);
    printf("  VISUAL EXPECTATION: %s\n", expectation);
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
#ifdef DC_PVR_RENDER_TARGET_HARDWARE_SCREENSHOT
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

static int page1_api(SDL_Renderer *renderer)
{
    SDL_RendererInfo info;
    SDL_Texture *t;
    SDL_Texture *bad;
    SDL_Rect viewport = { 17, 19, 200, 150 };
    SDL_Rect clip = { 7, 9, 80, 60 };
    SDL_Rect got;
    Uint16 pixel = 0xffff;
    void *locked = NULL;
    int pitch = 0;

    printf("PAGE 1: API prechecks and state restoration\n");
    REQUIRE(SDL_GetRendererInfo(renderer, &info));
    check((info.flags & SDL_RENDERER_TARGETTEXTURE) != 0,
          "prototype advertises TARGETTEXTURE");
    bad = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                            SDL_TEXTUREACCESS_TARGET, 64, 64);
    check(bad == NULL, "ARGB1555 target is rejected");
    SDL_DestroyTexture(bad);
    SDL_ClearError();

    t = target(renderer, 64, 64);
    if (!t) {
        return -1;
    }
    REQUIRE(SDL_RenderSetViewport(renderer, &viewport));
    REQUIRE(SDL_RenderSetClipRect(renderer, &clip));
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    check(SDL_GetRenderTarget(renderer) == t, "GetRenderTarget returns selected target");
    check(SDL_UpdateTexture(t, NULL, &pixel, 2) < 0,
          "UpdateTexture on target fails cleanly");
    SDL_ClearError();
    check(SDL_LockTexture(t, NULL, &locked, &pitch) < 0,
          "LockTexture on target is rejected");
    SDL_ClearError();
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    SDL_RenderGetViewport(renderer, &got);
    check(SDL_memcmp(&got, &viewport, sizeof(got)) == 0,
          "screen viewport restored after target reset");
    SDL_RenderGetClipRect(renderer, &got);
    check(SDL_memcmp(&got, &clip, sizeof(got)) == 0,
          "screen clip restored after target reset");
    REQUIRE(SDL_RenderSetViewport(renderer, NULL));
    REQUIRE(SDL_RenderSetClipRect(renderer, NULL));
    REQUIRE(clear_to(renderer, 8, 12, 24));
    REQUIRE(screen_panel(renderer, 70, 80, 500, 320,
                         failures ? 160 : 0, failures ? 0 : 150, 40));
    SDL_DestroyTexture(t);
    return present_page(renderer, 0, "API precheck",
                        "one large green panel; red means an API assertion failed");
}

static int page2_target_rendering(SDL_Renderer *renderer)
{
    SDL_Texture *t = target(renderer, 160, 120);

    printf("PAGE 2: target rendering, orientation, clear, viewport and clip\n");
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 12, 12, 28));
    REQUIRE(screen_panel(renderer, 0, 0, 32, 32, 255, 0, 0));
    REQUIRE(screen_panel(renderer, 128, 0, 32, 32, 0, 255, 0));
    REQUIRE(screen_panel(renderer, 0, 88, 32, 32, 0, 0, 255));
    REQUIRE(screen_panel(renderer, 128, 88, 32, 32, 255, 255, 0));
    {
        SDL_Rect vp = { 40, 30, 80, 60 };
        SDL_Rect clip = { 10, 10, 60, 40 };
        REQUIRE(SDL_RenderSetViewport(renderer, &vp));
        REQUIRE(SDL_RenderSetClipRect(renderer, &clip));
        REQUIRE(screen_panel(renderer, 0, 0, 80, 60, 255, 0, 255));
        REQUIRE(SDL_RenderSetClipRect(renderer, NULL));
        REQUIRE(SDL_RenderSetViewport(renderer, NULL));
    }
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 5, 8, 15));
    REQUIRE(copy(renderer, t, 80, 60, 480, 360));
    SDL_DestroyTexture(t);
    return present_page(renderer, 1, "target rendering",
                        "four distinct colored corners in correct orientation and a clipped magenta center");
}

static int page3_screen_persistence(SDL_Renderer *renderer)
{
    SDL_Texture *t = target(renderer, 64, 64);

    printf("PAGE 3: CRITICAL screen persistence S1 -> target -> S2\n");
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 8, 8, 18));
    REQUIRE(screen_panel(renderer, 40, 50, 220, 150, 220, 25, 25)); /* S1 */
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 20, 20, 120));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(screen_panel(renderer, 380, 280, 220, 150, 20, 220, 45)); /* S2 */
    SDL_DestroyTexture(t);
    return present_page(renderer, 2, "screen persistence",
                        "BOTH the upper-left red S1 block and lower-right green S2 block");
}

static int page4_two_targets(SDL_Renderer *renderer)
{
    SDL_Texture *a = target(renderer, 128, 128);
    SDL_Texture *b = target(renderer, 128, 128);

    printf("PAGE 4: target A -> target B -> screen\n");
    if (!a || !b) { SDL_DestroyTexture(a); SDL_DestroyTexture(b); return -1; }
    REQUIRE(SDL_SetRenderTarget(renderer, a));
    REQUIRE(clear_to(renderer, 100, 0, 0));
    REQUIRE(screen_panel(renderer, 16, 16, 96, 96, 255, 140, 0));
    REQUIRE(SDL_SetRenderTarget(renderer, b));
    REQUIRE(clear_to(renderer, 0, 60, 20));
    REQUIRE(screen_panel(renderer, 16, 16, 96, 96, 0, 220, 255));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 10, 10, 22));
    REQUIRE(copy(renderer, a, 60, 110, 220, 220));
    REQUIRE(copy(renderer, b, 360, 110, 220, 220));
    SDL_DestroyTexture(a);
    SDL_DestroyTexture(b);
    return present_page(renderer, 3, "two targets",
                        "orange/red target A at left and cyan/green target B at right");
}

static int page5_blends(SDL_Renderer *renderer)
{
    SDL_Texture *t = target(renderer, 256, 128);
    int i;
    const SDL_BlendMode modes[4] = { SDL_BLENDMODE_NONE, SDL_BLENDMODE_BLEND,
                                     SDL_BLENDMODE_ADD, SDL_BLENDMODE_MUL };

    printf("PAGE 5: NONE / BLEND / ADD / MUL into RGB565 target\n");
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 35, 35, 70));
    for (i = 0; i < 4; ++i) {
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(color(renderer, 70, 35, 25, 255));
        REQUIRE(fill(renderer, i * 64 + 4, 8, 56, 112));
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, modes[i]));
        REQUIRE(color(renderer, 30, 220, 160, 128));
        REQUIRE(fill(renderer, i * 64 + 18, 28, 38, 72));
    }
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 6, 8, 18));
    REQUIRE(SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest));
    REQUIRE(copy(renderer, t, 64, 80, 512, 256));
    SDL_DestroyTexture(t);
    return present_page(renderer, 4, "target blend modes",
                        "four distinct ordered columns: NONE, BLEND, ADD, MUL; no corruption");
}

static SDL_Texture *checker(SDL_Renderer *renderer)
{
    Uint16 pixels[8 * 8];
    SDL_Texture *texture;
    int x, y;

    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            pixels[y * 8 + x] = ((x ^ y) & 1) ? 0xffff : 0x001f;
        }
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!texture || SDL_UpdateTexture(texture, NULL, pixels, 8 * 2) < 0) {
        SDL_DestroyTexture(texture);
        failed_call("create initialized checker texture");
        return NULL;
    }
    return texture;
}

static int page6_filter_mod(SDL_Renderer *renderer)
{
    SDL_Texture *src = checker(renderer);
    SDL_Texture *t = target(renderer, 128, 128);

    printf("PAGE 6: filtering and modulation of completed target\n");
    if (!src || !t) { SDL_DestroyTexture(src); SDL_DestroyTexture(t); return -1; }
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 15, 15, 30));
    REQUIRE(SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest));
    REQUIRE(copy(renderer, src, 8, 8, 112, 112));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 8, 8, 18));
    REQUIRE(SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest));
    REQUIRE(copy(renderer, t, 30, 90, 260, 260));
    REQUIRE(SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear));
    REQUIRE(SDL_SetTextureColorMod(t, 255, 120, 120));
    REQUIRE(SDL_SetTextureAlphaMod(t, 150));
    REQUIRE(SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND));
    REQUIRE(copy(renderer, t, 350, 90, 260, 260));
    SDL_DestroyTexture(src);
    SDL_DestroyTexture(t);
    return present_page(renderer, 5, "filtering and modulation",
                        "sharp blue/white checker left; smoother red-modulated translucent checker right");
}

static int page7_reselection(SDL_Renderer *renderer)
{
    SDL_Texture *t = target(renderer, 160, 120);

    printf("PAGE 7: CRITICAL target persistence across reselection\n");
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 12, 12, 24));
    REQUIRE(screen_panel(renderer, 8, 8, 64, 48, 240, 25, 25)); /* T1 */
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 4, 6, 12));
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(screen_panel(renderer, 88, 64, 64, 48, 20, 230, 45)); /* T2 */
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 5, 8, 15));
    REQUIRE(copy(renderer, t, 40, 60, 560, 360));
    SDL_DestroyTexture(t);
    return present_page(renderer, 6, "target reselection persistence",
                        "BOTH red T1 upper-left and green T2 lower-right inside the sampled target");
}

static int page8_target_present(SDL_Renderer *renderer)
{
    SDL_Texture *t = target(renderer, 160, 120);

    printf("PAGE 8: CRITICAL target persistence across RenderPresent\n");
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 12, 12, 24));
    REQUIRE(screen_panel(renderer, 8, 8, 64, 48, 25, 80, 245)); /* T1 */
    SDL_RenderPresent(renderer);
    REQUIRE(screen_panel(renderer, 88, 64, 64, 48, 245, 220, 20)); /* T2 */
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 5, 8, 15));
    REQUIRE(copy(renderer, t, 40, 60, 560, 360));
    SDL_DestroyTexture(t);
    return present_page(renderer, 7, "target-present persistence",
                        "BOTH blue T1 upper-left and yellow T2 lower-right inside the sampled target");
}

static int page9_feedback_lifecycle(SDL_Renderer *renderer)
{
    SDL_Texture *t = target(renderer, 128, 128);
    SDL_Texture *selected;
    SDL_bool rejected;
    int i;

    printf("PAGE 9: self-feedback rejection and lifecycle recovery\n");
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 18, 18, 35));
    SDL_ClearError();
    rejected = SDL_RenderCopy(renderer, t, NULL, NULL) < 0;
    check(rejected && SDL_strstr(SDL_GetError(), "active render target") != NULL,
          "active-target feedback rejected with stable error");
    check(SDL_GetRenderTarget(renderer) == t,
          "feedback rejection leaves target selected");
    SDL_ClearError();
    REQUIRE(screen_panel(renderer, 20, 20, 88, 88, 0, 220, 80));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 8, 8, 18));
    REQUIRE(copy(renderer, t, 210, 90, 220, 220));
    SDL_DestroyTexture(t);
    for (i = 0; i < 5; ++i) {
        t = target(renderer, 32, 32);
        if (!t) return -1;
        REQUIRE(SDL_SetRenderTarget(renderer, t));
        REQUIRE(clear_to(renderer, (Uint8)(40 + i * 30), 20, 80));
        REQUIRE(SDL_SetRenderTarget(renderer, NULL));
        SDL_DestroyTexture(t);
    }
    selected = target(renderer, 32, 32);
    if (!selected) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, selected));
    REQUIRE(clear_to(renderer, 80, 20, 20));
    SDL_DestroyTexture(selected);
    check(SDL_GetRenderTarget(renderer) == NULL,
          "destroying selected target resets to screen");
    REQUIRE(screen_panel(renderer, 40, 400, 560, 24,
                         failures ? 180 : 0, failures ? 0 : 180, 40));
    return present_page(renderer, 8, "feedback and lifecycle",
                        "green recovered target centered and green status bar at bottom");
}

static int page10_guards(SDL_Renderer *renderer)
{
    SDL_Texture *t = target(renderer, 127, 93);

    printf("PAGE 10: NPOT guard geometry and padding-corruption probe\n");
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 10, 10, 22));
    REQUIRE(screen_panel(renderer, 0, 0, 127, 5, 255, 255, 255));
    REQUIRE(screen_panel(renderer, 0, 88, 127, 5, 255, 0, 255));
    REQUIRE(screen_panel(renderer, 0, 0, 5, 93, 0, 255, 255));
    REQUIRE(screen_panel(renderer, 122, 0, 5, 93, 255, 255, 0));
    REQUIRE(screen_panel(renderer, 30, 22, 67, 49, 220, 30, 30));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 0, 20, 0));
    REQUIRE(copy(renderer, t, 66, 54, 508, 372));
    SDL_DestroyTexture(t);
    return present_page(renderer, 9, "guard geometry",
                        "intact white/magenta/cyan/yellow border, red center, no flip or stale padding");
}

static int page11_repeated_seeds(SDL_Renderer *renderer)
{
    SDL_Texture *t = target(renderer, 64, 64);
    int i;

    printf("PAGE 11: repeated preservation seeds with stable markers\n");
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 5, 5, 15));
    REQUIRE(screen_panel(renderer, 8, 8, 24, 24, 200, 50, 50)); /* marker A */
    for (i = 0; i < 6; ++i) {
        int x = 36 + (i % 3) * 10;
        int y = 40 + (i / 3) * 12;
        Uint8 r = (Uint8)(180 + i * 10);
        Uint8 g = (Uint8)(60 + i * 8);
        Uint8 b = (Uint8)(30 + i * 5);

        REQUIRE(screen_panel(renderer, x, y, 8, 8, r, g, b));
    }
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 2, 4, 8));
    REQUIRE(copy(renderer, t, 100, 200, 440, 280));
    SDL_DestroyTexture(t);
    return present_page(renderer, 10, "repeated seeds",
                        "marker A plus six ordered gradient cells, no accumulating edge contamination");
}

static int page12_allocation_failure(SDL_Renderer *renderer)
{
    SDL_Texture *t;
    int i;

    printf("PAGE 12: allocation-failure and rollback coverage\n");
    t = target(renderer, 256, 256);
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 40, 20, 10));
    REQUIRE(screen_panel(renderer, 16, 16, 224, 224, 100, 100, 180));
    SDL_DestroyTexture(t);

    /* Repeated create/destroy cycle to exercise allocator rollback. */
    for (i = 0; i < 8; ++i) {
        t = target(renderer, 32, 32);
        if (!t) {
            printf("  CREATE FAIL at iteration %d, stopping\n", i);
            goto page12_done;
        }
        REQUIRE(SDL_SetRenderTarget(renderer, t));
        REQUIRE(clear_to(renderer, (Uint8)(20 + i * 20), 10, 50));
        REQUIRE(SDL_SetRenderTarget(renderer, NULL));
        SDL_DestroyTexture(t);
    }
    t = target(renderer, 32, 32);
    if (!t) return -1;
    REQUIRE(SDL_SetRenderTarget(renderer, t));
    REQUIRE(clear_to(renderer, 80, 20, 20));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 5, 8, 15));
    REQUIRE(screen_panel(renderer, 40, 400, 560, 24,
                         0, 160, 40));
page12_done:
    return present_page(renderer, 11, "allocation failure",
                        "green recovery bar at bottom; any failed creates stop early");
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_RendererInfo info;
    int driver;
    int rc = 1;

    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return failed_call("SDL_Init");
    driver = find_pvr_driver();
    if (driver < 0) { failed_call("find Dreamcast PVR driver"); goto done; }
    window = SDL_CreateWindow("PVR RTT physical validation", 0, 0,
                              OUTPUT_W, OUTPUT_H, 0);
    if (!window) { failed_call("SDL_CreateWindow"); goto done; }
    renderer = SDL_CreateRenderer(window, driver,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) { failed_call("SDL_CreateRenderer"); goto done; }
    REQUIRE(SDL_GetRendererInfo(renderer, &info));
    printf("Renderer: %s flags=%08lx\n", info.name, (unsigned long)info.flags);

    if (page1_api(renderer) < 0 || page2_target_rendering(renderer) < 0 ||
        page3_screen_persistence(renderer) < 0 || page4_two_targets(renderer) < 0 ||
        page5_blends(renderer) < 0 || page6_filter_mod(renderer) < 0 ||
        page7_reselection(renderer) < 0 || page8_target_present(renderer) < 0 ||
        page9_feedback_lifecycle(renderer) < 0 || page10_guards(renderer) < 0 ||
        page11_repeated_seeds(renderer) < 0 || page12_allocation_failure(renderer) < 0) {
        goto done;
    }
    printf("API SUMMARY: %d checks, %d failures\n", checks, failures);
    printf("%s (API/build): screenshots require direct physical inspection\n",
           failures ? "FAIL" : "PASS");
    rc = failures ? 1 : 0;

done:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
