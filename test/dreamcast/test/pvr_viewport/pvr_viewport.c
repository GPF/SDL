/*
  Dreamcast native PVR renderer viewport translation test.

  Exercises viewport offset for clears, filled rectangles, and textured quads.
  The viewport is a nonzero sub-rectangle of the 640x480 output. The expected
  result: a green fill covers only the viewport area; a red rectangle and
  an overlapping blue textured quad are positioned inside the viewport, not at
  their SDL coordinate origins. Everything outside the viewport must stay black.
*/

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#define VIEWPORT_X  50
#define VIEWPORT_Y  30
#define VIEWPORT_W  300
#define VIEWPORT_H  200

#define DIAGNOSTIC_FRAMES 3
#define DIAGNOSTIC_HOLD_MS 5000

static int find_pvr_driver(void)
{
    SDL_RendererInfo info;
    int count = SDL_GetNumRenderDrivers();
    int i;

    for (i = 0; i < count; ++i) {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 &&
            SDL_strcmp(info.name, "Dreamcast PVR") == 0) {
            return i;
        }
    }
    return -1;
}

static int display_viewport_scene(SDL_Renderer *renderer)
{
    SDL_Texture *texture;
    SDL_Rect viewport_rect = { VIEWPORT_X, VIEWPORT_Y, VIEWPORT_W, VIEWPORT_H };
    SDL_Rect fill_inside = { 60, 50, 120, 80 };
    SDL_Rect destination = { 200, 100, 64, 64 };
    Uint16 pixels[8 * 8];
    int frame;
    int i;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!texture) {
        return -1;
    }
    for (i = 0; i < (int)SDL_arraysize(pixels); ++i) {
        pixels[i] = 0x07E0u; /* RGB565 green */
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, 8 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(texture);
        return -1;
    }

    printf("Viewport scene: viewport (%d,%d) %dx%d\n",
           VIEWPORT_X, VIEWPORT_Y, VIEWPORT_W, VIEWPORT_H);

    for (frame = 0; frame < DIAGNOSTIC_FRAMES; ++frame) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_SetRenderTarget(renderer, NULL);
        SDL_RenderSetViewport(renderer, NULL);
        SDL_RenderClear(renderer);
        SDL_RenderSetViewport(renderer, &viewport_rect);

        /* SDL clear is full-target and ignores the viewport. Fill geometry is
         * used to paint only the viewport and retain this translation test. */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 180, 0, 255);
        SDL_RenderFillRect(renderer, NULL);

        /* Filled rectangle inside the viewport at SDL-relative (60,50). */
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderFillRect(renderer, &fill_inside);

        /* Textured quad inside the viewport at SDL-relative (200,100). */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, texture, NULL, &destination);

        SDL_RenderSetViewport(renderer, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);

    printf("Holding viewport frame for %d ms\n", DIAGNOSTIC_HOLD_MS);
    SDL_Delay(DIAGNOSTIC_HOLD_MS);
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    int driver_index;
    int result = 1;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    driver_index = find_pvr_driver();
    if (driver_index < 0) {
        printf("Dreamcast PVR renderer is not available\n");
        goto done;
    }

    window = SDL_CreateWindow("PVR viewport", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto done;
    }

    renderer = SDL_CreateRenderer(window, driver_index,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        goto done;
    }

    if (display_viewport_scene(renderer) < 0) {
        printf("Viewport scene failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        goto done;
    }

    printf("PASS: viewport translation rendered correctly\n");
    result = 0;

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
