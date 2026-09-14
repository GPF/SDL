/*
  Dreamcast native PVR point and default-POINTS line validation.

  The three pages exercise queued points, SDL-generated axis-aligned rects and
  Bresenham points, endpoint ownership, viewport translation, CPU clipping,
  and ordered clears. The patterns follow the SDL software renderer's coverage
  and blending behavior as the reference.
*/

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#ifndef PAGE_HOLD_MS
#define PAGE_HOLD_MS 8000
#endif

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

static int set_draw(SDL_Renderer *renderer, SDL_BlendMode blend,
                    Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (SDL_SetRenderDrawBlendMode(renderer, blend) < 0) {
        return -1;
    }
    return SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

static int fill(SDL_Renderer *renderer, int x, int y, int w, int h)
{
    const SDL_Rect rect = { x, y, w, h };
    return SDL_RenderFillRect(renderer, &rect);
}

static int point_marker(SDL_Renderer *renderer, int x, int y,
                        Uint8 r, Uint8 g, Uint8 b)
{
    if (set_draw(renderer, SDL_BLENDMODE_NONE, r / 4, g / 4, b / 4, 255) < 0 ||
        fill(renderer, x - 4, y - 4, 9, 9) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 0, 0, 255) < 0 ||
        fill(renderer, x - 2, y - 2, 5, 5) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, r, g, b, 255) < 0) {
        return -1;
    }
    return SDL_RenderDrawPoint(renderer, x, y);
}

static int clear_black(SDL_Renderer *renderer)
{
    if (SDL_RenderSetViewport(renderer, NULL) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 0, 0, 255) < 0) {
        return -1;
    }
    return SDL_RenderClear(renderer);
}

static int render_points_page(SDL_Renderer *renderer)
{
    const SDL_Point isolated[] = { { 80, 70 }, { 110, 70 }, { 111, 70 },
                                   { 112, 70 }, { 150, 70 } };
    const SDL_Point clip_points[] = { { 220, 120 }, { 260, 90 },
                                      { 299, 120 }, { 260, 149 },
                                      { 300, 120 }, { 260, 150 } };
    const SDL_Rect clip = { 220, 90, 80, 60 };
    const SDL_Rect one_pixel = { 400, 120, 1, 1 };

    /* This point must be erased by the following ordered full-target clear. */
    if (clear_black(renderer) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 128, 0, 255) < 0 ||
        SDL_RenderDrawPoint(renderer, 30, 30) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 32, 32, 32, 255) < 0 ||
        fill(renderer, 0, 0, 8, 8) < 0 || fill(renderer, 632, 472, 8, 8) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255) < 0 ||
        SDL_RenderDrawPoint(renderer, 0, 0) < 0 ||
        SDL_RenderDrawPoint(renderer, 639, 479) < 0 ||
        SDL_RenderDrawPoints(renderer, isolated, (int)SDL_arraysize(isolated)) < 0 ||
        point_marker(renderer, 360, 70, 0, 255, 255) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 40, 40, 40, 255) < 0 ||
        fill(renderer, 210, 80, 101, 81) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 0, 0, 255) < 0 ||
        fill(renderer, 220, 90, 80, 60) < 0 ||
        SDL_RenderSetClipRect(renderer, &clip) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 255, 0, 255) < 0 ||
        SDL_RenderDrawPoints(renderer, clip_points, (int)SDL_arraysize(clip_points)) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 32, 32, 32, 255) < 0 ||
        fill(renderer, 394, 114, 13, 13) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 0, 0, 255) < 0 ||
        fill(renderer, 399, 119, 3, 3) < 0 ||
        SDL_RenderSetClipRect(renderer, &one_pixel) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255) < 0 ||
        SDL_RenderDrawPoint(renderer, 400, 120) < 0 ||
        SDL_RenderDrawPoint(renderer, 401, 120) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0) {
        return -1;
    }

    /* NONE duplicates replace; BLEND duplicates accumulate independently. */
    if (set_draw(renderer, SDL_BLENDMODE_NONE, 48, 48, 48, 255) < 0 ||
        fill(renderer, 60, 190, 240, 80) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 64, 64, 96) < 0 ||
        SDL_RenderDrawPoint(renderer, 110, 230) < 0 ||
        SDL_RenderDrawPoint(renderer, 110, 230) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_BLEND, 64, 255, 64, 96) < 0 ||
        SDL_RenderDrawPoint(renderer, 190, 230) < 0 ||
        SDL_RenderDrawPoint(renderer, 230, 230) < 0 ||
        SDL_RenderDrawPoint(renderer, 230, 230) < 0 ||
        point_marker(renderer, 500, 350, 255, 0, 255) < 0) {
        return -1;
    }

    printf("Page 1/3 points: corner, isolated/adjacent, duplicate NONE/BLEND,\n");
    printf("clip edges, one-pixel clip, and ordered-clear probes.\n");
    return 0;
}

static int render_lines_page(SDL_Renderer *renderer)
{
    const SDL_Point connected[] = { { 40, 300 }, { 110, 260 }, { 180, 310 }, { 250, 270 } };
    const SDL_Point closed[] = { { 330, 260 }, { 400, 260 }, { 400, 320 },
                                 { 330, 320 }, { 330, 260 } };
    const SDL_Point interior_repeat[] = { { 40, 390 }, { 105, 350 }, { 105, 350 }, { 170, 390 } };
    const SDL_Point final_repeat[] = { { 230, 390 }, { 300, 350 }, { 300, 350 } };
    const SDL_Point zero_closing[] = { { 370, 390 }, { 430, 350 }, { 370, 390 }, { 370, 390 } };

    if (clear_black(renderer) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255) < 0 ||
        SDL_RenderDrawLine(renderer, 30, 40, 250, 40) < 0 ||
        SDL_RenderDrawLine(renderer, 290, 20, 290, 130) < 0 ||
        SDL_RenderDrawLine(renderer, 30, 90, 130, 190) < 0 ||
        SDL_RenderDrawLine(renderer, 30, 220, 250, 180) < 0 ||
        SDL_RenderDrawLine(renderer, 250, 140, 30, 180) < 0 ||
        SDL_RenderDrawLine(renderer, 330, 210, 370, 20) < 0 ||
        SDL_RenderDrawLine(renderer, 430, 20, 390, 210) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 220, 255, 255) < 0 ||
        SDL_RenderDrawLine(renderer, 500, 40, 590, 110) < 0 ||
        SDL_RenderDrawLine(renderer, 590, 150, 500, 220) < 0 ||
        point_marker(renderer, 540, 260, 255, 255, 0) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 255, 0, 255) < 0 ||
        SDL_RenderDrawLine(renderer, 540, 260, 540, 260) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 32, 32, 32, 255) < 0 ||
        fill(renderer, 566, 256, 9, 9) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 0, 0, 255) < 0 ||
        fill(renderer, 568, 258, 5, 5) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_BLEND, 255, 255, 0, 96) < 0 ||
        SDL_RenderDrawLine(renderer, 570, 260, 570, 260) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_BLEND, 255, 96, 96, 96) < 0 ||
        SDL_RenderDrawLines(renderer, connected, (int)SDL_arraysize(connected)) < 0 ||
        SDL_RenderDrawLines(renderer, closed, (int)SDL_arraysize(closed)) < 0 ||
        SDL_RenderDrawLines(renderer, interior_repeat, (int)SDL_arraysize(interior_repeat)) < 0 ||
        SDL_RenderDrawLines(renderer, final_repeat, (int)SDL_arraysize(final_repeat)) < 0 ||
        SDL_RenderDrawLines(renderer, zero_closing, (int)SDL_arraysize(zero_closing)) < 0) {
        return -1;
    }

    printf("Page 2/3 lines: axes, 45/shallow/steep/reversed, independent and\n");
    printf("connected/closed strips, plus all zero-length endpoint cases.\n");
    return 0;
}

static int render_viewport_clip_page(SDL_Renderer *renderer)
{
    const SDL_Rect viewport = { 70, 50, 500, 380 };
    const SDL_Rect clip = { 40, 35, 330, 240 };
    const SDL_Rect one_wide = { 410, 30, 1, 180 };
    const SDL_Rect one_high = { 390, 300, 100, 1 };
    const SDL_Rect changed = { 260, 280, 180, 70 };
    const SDL_Point crossing[] = { { -40, 180 }, { 130, -30 }, { 260, 290 }, { 430, 80 } };

    /* Orange and its first clipped line are erased by the ordered black clear. */
    if (clear_black(renderer) < 0 ||
        SDL_RenderSetViewport(renderer, &viewport) < 0 ||
        SDL_RenderSetClipRect(renderer, &clip) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 96, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0 ||
        SDL_RenderDrawLine(renderer, 0, 0, 499, 379) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0 ||
        SDL_RenderSetViewport(renderer, &viewport) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 24, 24, 24, 255) < 0 ||
        fill(renderer, 0, 0, 500, 380) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 0, 180, 255, 255) < 0 ||
        SDL_RenderDrawPoint(renderer, 0, 0) < 0 ||
        SDL_RenderDrawLine(renderer, 10, 20, 250, 20) < 0 ||
        SDL_RenderDrawLine(renderer, 20, 30, 20, 220) < 0 ||
        SDL_RenderSetClipRect(renderer, &clip) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 0, 255, 255) < 0 ||
        SDL_RenderDrawLine(renderer, clip.x + clip.w - 1, 20,
                          clip.x + clip.w - 1, 300) < 0 ||
        SDL_RenderDrawLine(renderer, 20, clip.y + clip.h - 1,
                          440, clip.y + clip.h - 1) < 0 ||
        SDL_RenderDrawLine(renderer, clip.x + clip.w, 20,
                          clip.x + clip.w, 300) < 0 ||
        SDL_RenderDrawLine(renderer, 20, clip.y + clip.h,
                          440, clip.y + clip.h) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255) < 0 ||
        SDL_RenderDrawLines(renderer, crossing, (int)SDL_arraysize(crossing)) < 0 ||
        SDL_RenderDrawPoint(renderer, clip.x, clip.y) < 0 ||
        SDL_RenderDrawPoint(renderer, clip.x + clip.w - 1, clip.y + clip.h - 1) < 0 ||
        SDL_RenderDrawPoint(renderer, clip.x + clip.w, clip.y + clip.h - 1) < 0 ||
        SDL_RenderDrawPoint(renderer, clip.x + clip.w - 1, clip.y + clip.h) < 0 ||
        SDL_RenderSetClipRect(renderer, &one_wide) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_NONE, 255, 255, 0, 255) < 0 ||
        SDL_RenderDrawLine(renderer, 350, 30, 470, 180) < 0 ||
        SDL_RenderSetClipRect(renderer, &one_high) < 0 ||
        SDL_RenderDrawLine(renderer, 360, 270, 480, 330) < 0 ||
        SDL_RenderSetClipRect(renderer, &changed) < 0 ||
        set_draw(renderer, SDL_BLENDMODE_BLEND, 255, 0, 255, 160) < 0 ||
        SDL_RenderDrawLine(renderer, 220, 260, 480, 360) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        SDL_RenderSetViewport(renderer, NULL) < 0) {
        return -1;
    }

    printf("Page 3/3 viewport/clipping: translated primitives, all clip edges,\n");
    printf("multi-edge lines, one-pixel strips, guards, clip changes, clears.\n");
    return 0;
}

static void show_page(SDL_Renderer *renderer, int page)
{
    SDL_RenderPresent(renderer);
    printf("Holding points/lines page %d for %d ms\n", page, PAGE_HOLD_MS);
    SDL_Delay(PAGE_HOLD_MS);
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    int driver_index;
    int result = 1;

    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    driver_index = find_pvr_driver();
    if (driver_index < 0) {
        printf("Dreamcast PVR renderer is not available\n");
        goto done;
    }
    window = SDL_CreateWindow("PVR points and lines", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto done;
    }
    renderer = SDL_CreateRenderer(window, driver_index,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        goto done;
    }
    if (render_points_page(renderer) < 0) {
        goto failed;
    }
    show_page(renderer, 1);
    if (render_lines_page(renderer) < 0) {
        goto failed;
    }
    show_page(renderer, 2);
    if (render_viewport_clip_page(renderer) < 0) {
        goto failed;
    }
    show_page(renderer, 3);

    printf("PASS (API/runtime): physical Dreamcast visual validation is still required\n");
    result = 0;
    goto done;

failed:
    printf("Points/lines test failed: %s\n", SDL_GetError());
done:
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return result;
}
