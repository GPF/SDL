/*
  Dreamcast native PVR per-vertex color and blend-mode validation.

  The same scene is drawn twice. The left panel uses the full-output viewport;
  the right panel uses a viewport at (320, 0), so every coordinate in that
  panel must receive the viewport offset at command execution time.
*/

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#define PANEL_W 320
#define PANEL_H 480
#define DIAGNOSTIC_FRAMES 3
#define DIAGNOSTIC_HOLD_MS 8000

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

static int expect_unsupported_draw_blend(SDL_Renderer *renderer, SDL_BlendMode mode,
                                         const char *name)
{
    SDL_ClearError();
    if (SDL_SetRenderDrawBlendMode(renderer, mode) == 0) {
        return SDL_SetError("Dreamcast PVR unexpectedly accepted %s draw blending", name);
    }
    if (SDL_GetError()[0] == '\0') {
        return SDL_SetError("Dreamcast PVR rejected %s without an SDL error", name);
    }
    return 0;
}

static int validate_blend_modes(SDL_Renderer *renderer, SDL_Texture *texture)
{
    SDL_BlendMode custom;

    if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) < 0 ||
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0) {
        return SDL_SetError("Dreamcast PVR rejected NONE or BLEND: %s", SDL_GetError());
    }

    if (expect_unsupported_draw_blend(renderer, SDL_BLENDMODE_ADD, "ADD") < 0 ||
        expect_unsupported_draw_blend(renderer, SDL_BLENDMODE_MOD, "MOD") < 0 ||
        expect_unsupported_draw_blend(renderer, SDL_BLENDMODE_MUL, "MUL") < 0) {
        return -1;
    }

    custom = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,
                                        SDL_BLENDFACTOR_ONE,
                                        SDL_BLENDOPERATION_SUBTRACT,
                                        SDL_BLENDFACTOR_ONE,
                                        SDL_BLENDFACTOR_ONE,
                                        SDL_BLENDOPERATION_SUBTRACT);
    if (expect_unsupported_draw_blend(renderer, custom, "custom subtract") < 0) {
        return -1;
    }

    SDL_ClearError();
    if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD) == 0) {
        return SDL_SetError("Dreamcast PVR unexpectedly accepted ADD texture blending");
    }
    if (SDL_GetError()[0] == '\0') {
        return SDL_SetError("Dreamcast PVR rejected ADD texture blending without an SDL error");
    }

    return SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
}

static int fill_rect(SDL_Renderer *renderer, SDL_BlendMode blend,
                     Uint8 r, Uint8 g, Uint8 b, Uint8 a,
                     int x, int y, int w, int h)
{
    SDL_Rect rect = { x, y, w, h };

    if (SDL_SetRenderDrawBlendMode(renderer, blend) < 0 ||
        SDL_SetRenderDrawColor(renderer, r, g, b, a) < 0) {
        return -1;
    }
    return SDL_RenderFillRect(renderer, &rect);
}

static int draw_panel(SDL_Renderer *renderer, SDL_Texture *texture)
{
    const SDL_Vertex color_triangle[3] = {
        { { 20.0f, 20.0f }, { 255, 0, 0, 255 }, { 0.0f, 0.0f } },
        { { 140.0f, 20.0f }, { 0, 255, 0, 255 }, { 0.0f, 0.0f } },
        { { 80.0f, 120.0f }, { 0, 0, 255, 255 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex alpha_triangle[3] = {
        { { 170.0f, 20.0f }, { 255, 255, 255, 0 }, { 0.0f, 0.0f } },
        { { 290.0f, 20.0f }, { 255, 255, 255, 128 }, { 0.0f, 0.0f } },
        { { 230.0f, 120.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex textured_triangle[3] = {
        { { 20.0f, 150.0f }, { 255, 64, 64, 255 }, { 0.0f, 0.0f } },
        { { 140.0f, 150.0f }, { 64, 255, 64, 160 }, { 1.0f, 0.0f } },
        { { 80.0f, 260.0f }, { 64, 64, 255, 32 }, { 0.5f, 1.0f } }
    };
    SDL_Rect modulated_copy = { 180, 160, 96, 80 };

    if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderGeometry(renderer, NULL, color_triangle, 3, NULL, 0) < 0) {
        return -1;
    }

    if (fill_rect(renderer, SDL_BLENDMODE_NONE, 0, 32, 96, 255,
                  165, 15, 130, 110) < 0 ||
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0 ||
        SDL_RenderGeometry(renderer, NULL, alpha_triangle, 3, NULL, 0) < 0) {
        return -1;
    }

    if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) < 0 ||
        SDL_RenderGeometry(renderer, texture, textured_triangle, 3, NULL, 0) < 0) {
        return -1;
    }

    if (fill_rect(renderer, SDL_BLENDMODE_NONE, 32, 32, 32, 255,
                  170, 150, 116, 100) < 0 ||
        SDL_SetTextureColorMod(texture, 160, 64, 255) < 0 ||
        SDL_SetTextureAlphaMod(texture, 128) < 0 ||
        SDL_RenderCopy(renderer, texture, NULL, &modulated_copy) < 0 ||
        SDL_SetTextureColorMod(texture, 255, 255, 255) < 0 ||
        SDL_SetTextureAlphaMod(texture, 255) < 0) {
        return -1;
    }

    /* These overlap in command order: opaque blue, translucent red, opaque
     * green, then translucent yellow. All remain in the transparent list. */
    if (fill_rect(renderer, SDL_BLENDMODE_NONE, 0, 64, 255, 255,
                  30, 300, 150, 100) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_BLEND, 255, 0, 0, 128,
                  80, 320, 150, 100) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 0, 220, 80, 255,
                  130, 340, 130, 80) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_BLEND, 255, 255, 0, 128,
                  180, 360, 100, 70) < 0) {
        return -1;
    }

    return 0;
}

static int display_scene(SDL_Renderer *renderer, SDL_Texture *texture)
{
    SDL_Rect offset_viewport = { PANEL_W, 0, PANEL_W, PANEL_H };
    int frame;

    printf("Expected: identical left and right panels; RGB vertex triangles,\n");
    printf("smooth alpha gradients, modulated white texture, and ordered overlaps.\n");

    for (frame = 0; frame < DIAGNOSTIC_FRAMES; ++frame) {
        if (SDL_RenderSetViewport(renderer, NULL) < 0 ||
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) < 0 ||
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0 ||
            SDL_RenderClear(renderer) < 0 ||
            draw_panel(renderer, texture) < 0 ||
            SDL_RenderSetViewport(renderer, &offset_viewport) < 0 ||
            draw_panel(renderer, texture) < 0 ||
            SDL_RenderSetViewport(renderer, NULL) < 0) {
            return -1;
        }
        SDL_RenderPresent(renderer);
    }

    printf("Holding vertex-color/blend frame for %d ms\n", DIAGNOSTIC_HOLD_MS);
    SDL_Delay(DIAGNOSTIC_HOLD_MS);
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    Uint16 pixels[8 * 8];
    int driver_index;
    int result = 1;
    int i;

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

    window = SDL_CreateWindow("PVR vertex color and blend",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              640, 480, 0);
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

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!texture) {
        printf("Texture creation failed: %s\n", SDL_GetError());
        goto done;
    }
    for (i = 0; i < (int)SDL_arraysize(pixels); ++i) {
        pixels[i] = 0xffffu;
    }
    if (SDL_UpdateTexture(texture, NULL, pixels,
                          8 * (int)sizeof(Uint16)) < 0 ||
        validate_blend_modes(renderer, texture) < 0 ||
        display_scene(renderer, texture) < 0) {
        printf("Vertex-color/blend test failed: %s\n", SDL_GetError());
        goto done;
    }

    printf("PASS (API/build): visual result still requires real Dreamcast validation\n");
    result = 0;

done:
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return result;
}
