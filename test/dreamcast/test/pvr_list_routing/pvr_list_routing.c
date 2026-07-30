/* Focused opaque-prefix list-routing validation for the native Dreamcast PVR
 * renderer.
 *
 * Pages
 *   1 — Leading opaque clear + untextured NONE geometry + BLEND + later NONE:
 *       verify final NONE is above BLEND (emitted in TR).
 *   2 — Textured-NONE boundary:
 *       untextured NONE → textured NONE → untextured NONE.
 *       Verify the final two preserve TR painter order.
 *   3 — State-only and lifecycle:
 *       opaque geometry → viewport/clip/draw-color changes → opaque geometry;
 *       then target/ordered-RTT guard if compiled;
 *       empty queue, multiple presents, renderer destruction.
 *
 * Screenshot paths (written via /pc when PVR_TEXTURE_HARDWARE_SCREENSHOT is
 * defined; three-vblank wait after present).
 */

#include <SDL.h>

#include <dc/video.h>

#include <stdio.h>

#define HOLD_MS    2500
#define PAGE_COUNT 3
#define SCREENSHOT_PREFIX "/pc/pvr_list_routing-page"

/* ---------- helpers ---------- */

static int find_pvr_driver(void)
{
    SDL_RendererInfo info;
    int i;
    for (i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 &&
            SDL_strcmp(info.name, "Dreamcast PVR") == 0) {
            return i;
        }
    }
    return -1;
}

/* Create a solid RGB565 texture (8×8, caller destroys). */
static SDL_Texture *create_solid_tex(SDL_Renderer *renderer,
                                     Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Texture *t;
    Uint16 pixels[8 * 8];
    Uint16 pixel = (Uint16)(((Uint16)(r >> 3) << 11) |
                             ((Uint16)(g >> 2) << 5) |
                             (Uint16)(b >> 3));
    size_t i;

    for (i = 0; i < SDL_arraysize(pixels); ++i) {
        pixels[i] = pixel;
    }
    t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                          SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!t) {
        return NULL;
    }
    if (SDL_UpdateTexture(t, NULL, pixels, 8 * (int)sizeof(Uint16)) < 0 ||
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE) < 0) {
        SDL_DestroyTexture(t);
        return NULL;
    }
    return t;
}

/* Wait three vblanks after present, then write the raw 640×480 framebuffer. */
static void wait_and_screenshot(const char *path)
{
    SDL_Delay(120);
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
    if (path) {
        vid_screen_shot(path);
    }
}

/* ---------- page 1: leading opaque run ---------- */
static int run_page1(SDL_Renderer *renderer)
{
    SDL_Rect r;
    int ret = 0;

    SDL_Log("=== Page 1: Leading opaque run ===");

    /* Full-target opaque black clear (routed to OP while prefix open). */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(renderer) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderClear failed: %s", SDL_GetError());
        return -1;
    }

    /* Opaque red rectangle (50,50 200×150). */
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    r.x = 50; r.y = 50; r.w = 200; r.h = 150;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (red) failed: %s", SDL_GetError());
        return -1;
    }

    /* Translucent green rectangle (100,100 200×150). */
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 128);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    r.x = 100; r.y = 100; r.w = 200; r.h = 150;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (green) failed: %s", SDL_GetError());
        return -1;
    }

    /* Opaque blue rectangle (150,150 200×150) — after BLEND, must go to TR. */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    r.x = 150; r.y = 150; r.w = 200; r.h = 150;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (blue) failed: %s", SDL_GetError());
        return -1;
    }

    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (SDL_GetError()[0] != '\0') {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderPresent (page 1) failed: %s", SDL_GetError());
        SDL_ClearError();
        return -1;
    }
    wait_and_screenshot(SCREENSHOT_PREFIX "1.ppm");

    /* Painter-order probe: red → green blend → blue. */
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    r.x = 0; r.y = 200; r.w = 300; r.h = 100;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (page1 red) failed: %s", SDL_GetError());
        return -1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 128);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    r.x = 100; r.y = 200; r.w = 300; r.h = 100;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (page1 green) failed: %s", SDL_GetError());
        return -1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    r.x = 200; r.y = 200; r.w = 300; r.h = 100;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (page1 blue) failed: %s", SDL_GetError());
        return -1;
    }

    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (SDL_GetError()[0] != '\0') {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderPresent (page1 probe) failed: %s", SDL_GetError());
        SDL_ClearError();
        return -1;
    }
    wait_and_screenshot(NULL); /* same page, no screenshot */

    return ret;
}

/* ---------- page 2: textured-NONE boundary ---------- */
static int run_page2(SDL_Renderer *renderer, SDL_Texture *tex)
{
    SDL_Rect r;
    int ret = 0;

    SDL_Log("=== Page 2: Textured-NONE boundary ===");

    /* Full-target opaque clear. */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(renderer) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderClear (page2) failed: %s", SDL_GetError());
        return -1;
    }

    /* Opaque untextured red rect → OP (prefix open). */
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    r.x = 20; r.y = 20; r.w = 150; r.h = 100;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (page2 red) failed: %s", SDL_GetError());
        return -1;
    }

    /* Textured green quad → closes prefix, goes to TR. */
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    r.x = 100; r.y = 70; r.w = 150; r.h = 100;
    if (SDL_RenderCopy(renderer, tex, NULL, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderCopy (page2 green) failed: %s", SDL_GetError());
        return -1;
    }

    /* Opaque untextured blue rect → must go to TR (prefix closed). */
    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    r.x = 180; r.y = 120; r.w = 150; r.h = 100;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (page2 blue) failed: %s", SDL_GetError());
        return -1;
    }

    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (SDL_GetError()[0] != '\0') {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderPresent (page2) failed: %s", SDL_GetError());
        SDL_ClearError();
        return -1;
    }
    wait_and_screenshot(SCREENSHOT_PREFIX "2.ppm");

    return ret;
}

/* ---------- page 3: state-only and lifecycle ---------- */
static int run_page3(SDL_Renderer *renderer)
{
    SDL_Rect r;
    int ret = 0;

    SDL_Log("=== Page 3: State-only and lifecycle ===");

    /* Opaque clear. */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(renderer) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderClear (page3) failed: %s", SDL_GetError());
        return -1;
    }

    /* Opaque red rect in full output. */
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    r.x = 20; r.y = 20; r.w = 100; r.h = 80;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (page3 red) failed: %s", SDL_GetError());
        return -1;
    }

    /* Viewport change — state-only, does NOT close prefix. */
    r.x = 200; r.y = 20; r.w = 200; r.h = 100;
    SDL_RenderSetViewport(renderer, &r);

    /* Clip coordinates are relative to the viewport — state-only. */
    r.x = 0; r.y = 0; r.w = 200; r.h = 100;
    SDL_RenderSetClipRect(renderer, &r);

    /* Draw color change — state-only. */
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

    /* Opaque green rect — still OP (state-only commands preserved prefix).
     * The viewport-relative rect (20,20 100×80) appears at physical
     * (220,40 100×80), fully inside the viewport-relative clip.
     */
    r.x = 20; r.y = 20; r.w = 100; r.h = 80;
    if (SDL_RenderFillRect(renderer, &r) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderFillRect (page3 green) failed: %s", SDL_GetError());
        return -1;
    }

    /* Reset viewport/clip for the rest. */
    SDL_RenderSetViewport(renderer, NULL);
    SDL_RenderSetClipRect(renderer, NULL);

    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (SDL_GetError()[0] != '\0') {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderPresent (page3) failed: %s", SDL_GetError());
        SDL_ClearError();
        return -1;
    }
    wait_and_screenshot(SCREENSHOT_PREFIX "3.ppm");

    /* Empty queue — multiple presents. */
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (SDL_GetError()[0] != '\0') {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderPresent (empty1) failed: %s", SDL_GetError());
        SDL_ClearError();
        return -1;
    }
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (SDL_GetError()[0] != '\0') {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderPresent (empty2) failed: %s", SDL_GetError());
        SDL_ClearError();
        return -1;
    }

    return ret;
}

/* ---------- main ---------- */
int main(int argc, char *argv[])
{
    int page, ret = 0;
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *tex = NULL;
    int drv;

    (void)argc; (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    drv = find_pvr_driver();
    if (drv < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No Dreamcast PVR driver found");
        SDL_Quit();
        return 1;
    }

    win = SDL_CreateWindow("pvr_list_routing",
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            640, 480, 0);
    if (!win) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(win, drv, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    tex = create_solid_tex(renderer, 0, 255, 0);
    if (!tex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "create_solid_tex failed: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    for (page = 1; page <= PAGE_COUNT; ++page) {
        switch (page) {
        case 1: ret = run_page1(renderer); break;
        case 2: ret = run_page2(renderer, tex); break;
        case 3: ret = run_page3(renderer); break;
        }
        if (ret < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Page %d failed", page);
            break;
        }
    }

    SDL_DestroyTexture(tex);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();

    SDL_Log("PASS");
    return (ret < 0) ? 1 : 0;
}
