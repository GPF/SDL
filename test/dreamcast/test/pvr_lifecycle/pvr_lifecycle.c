/*
  Dreamcast native PVR renderer lifecycle regression test.

  This repeatedly transfers PVR ownership to and from SDL_Renderer, exercises
  both presented and flushed-but-not-presented destruction, and verifies that
  a failed renderer creation does not poison the next valid creation.
*/

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#define LIFECYCLE_ITERATIONS 32
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

static int validate_vsync(SDL_Renderer *renderer)
{
    SDL_RendererInfo info;

    if (SDL_RenderSetVSync(renderer, 1) < 0) {
        return SDL_SetError("Enabling fixed PVR vsync failed: %s", SDL_GetError());
    }
    if (SDL_GetRendererInfo(renderer, &info) < 0 ||
        !(info.flags & SDL_RENDERER_PRESENTVSYNC)) {
        return SDL_SetError("PVR renderer did not report enabled vsync");
    }

    SDL_ClearError();
    if (SDL_RenderSetVSync(renderer, 0) < 0) {
        return SDL_SetError("SDL_RenderSetVSync wrapper unexpectedly failed");
    }
    if (strstr(SDL_GetError(), "always synchronized") == NULL) {
        return SDL_SetError("PVR renderer did not reject unsupported vsync disable");
    }

    SDL_ClearError();
    if (SDL_RenderSetVSync(renderer, 1) < 0 ||
        SDL_GetRendererInfo(renderer, &info) < 0 ||
        !(info.flags & SDL_RENDERER_PRESENTVSYNC)) {
        return SDL_SetError("Re-enabling fixed PVR vsync failed");
    }
    return 0;
}

static int exercise_renderer(SDL_Renderer *renderer, int iteration)
{
    SDL_Texture *texture;
    SDL_Rect fill = { 24, 24, 128, 96 };
    SDL_Rect destination = { 96, 72, 104, 56 };
    Uint16 pixels[13 * 7];
    int i;

    if (validate_vsync(renderer) < 0) {
        return -1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 13, 7);
    if (!texture) {
        return -1;
    }
    for (i = 0; i < (int)SDL_arraysize(pixels); ++i) {
        pixels[i] = 0xf800u;
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, 13 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(texture);
        return -1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 80, 255, 160);
    SDL_RenderFillRect(renderer, &fill);
    SDL_RenderCopy(renderer, texture, NULL, &destination);

    if (iteration & 1) {
        /* Leave an active scene referencing the texture for destroy cleanup. */
        if (SDL_RenderFlush(renderer) < 0) {
            SDL_DestroyTexture(texture);
            return -1;
        }
    } else {
        SDL_RenderPresent(renderer);
    }

    /* Let renderer destruction exercise texture cleanup on half the loops. */
    if ((iteration & 3) == 0) {
        SDL_DestroyTexture(texture);
    }
    return 0;
}

static int display_diagnostic(SDL_Renderer *renderer)
{
    SDL_Texture *texture;
    SDL_Rect blue = { 64, 64, 256, 160 };
    SDL_Rect destination = { 160, 120, 256, 160 };
    Uint16 pixels[13 * 7];
    int frame;
    int i;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 13, 7);
    if (!texture) {
        return -1;
    }
    for (i = 0; i < (int)SDL_arraysize(pixels); ++i) {
        pixels[i] = 0xf800u;
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, 13 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(texture);
        return -1;
    }

    printf("Lifecycle stress passed; displaying stable Phase 0 frame for %d ms\n",
           DIAGNOSTIC_HOLD_MS);
    for (frame = 0; frame < DIAGNOSTIC_FRAMES; ++frame) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 80, 255, 255);
        SDL_RenderFillRect(renderer, &blue);
        SDL_RenderCopy(renderer, texture, NULL, &destination);
        SDL_RenderPresent(renderer);
    }

    /* Render completion is not a wall-clock vblank wait. Keep PVR alive so
     * the diagnostic framebuffer remains visible on physical hardware. */
    SDL_Delay(DIAGNOSTIC_HOLD_MS);

    SDL_DestroyTexture(texture);
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer;
    int driver_index;
    int iteration;
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

    window = SDL_CreateWindow("PVR lifecycle", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto done;
    }

    for (iteration = 0; iteration < LIFECYCLE_ITERATIONS; ++iteration) {
        renderer = SDL_CreateRenderer(window, driver_index,
                                      SDL_RENDERER_ACCELERATED |
                                      SDL_RENDERER_TARGETTEXTURE);
        if (renderer) {
            printf("Iteration %d: unsupported target renderer creation succeeded\n", iteration);
            SDL_DestroyRenderer(renderer);
            goto done;
        }

        renderer = SDL_CreateRenderer(window, driver_index,
                                      SDL_RENDERER_ACCELERATED |
                                      SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            printf("Iteration %d: renderer creation failed: %s\n",
                   iteration, SDL_GetError());
            goto done;
        }
        if (exercise_renderer(renderer, iteration) < 0) {
            printf("Iteration %d: renderer exercise failed: %s\n",
                   iteration, SDL_GetError());
            SDL_DestroyRenderer(renderer);
            goto done;
        }
        SDL_DestroyRenderer(renderer);
    }

    renderer = SDL_CreateRenderer(window, driver_index,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("Final diagnostic renderer creation failed: %s\n", SDL_GetError());
        goto done;
    }
    if (display_diagnostic(renderer) < 0) {
        printf("Final diagnostic frame failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        goto done;
    }
    SDL_DestroyRenderer(renderer);

    printf("PASS: %d native PVR renderer lifecycle iterations\n",
           LIFECYCLE_ITERATIONS);
    result = 0;

done:
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return result;
}
