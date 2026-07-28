/*
  Dreamcast native PVR renderer CPU-clipping validation.

  Three held pages cover effective bounds and clear semantics, clipped
  attribute interpolation and fan seams, then textured/winding/degenerate and
  painter-order cases. Visual success in Flycast does not replace hardware
  validation of PVR right/bottom edge rasterization.
*/

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#ifndef PAGE_HOLD_MS
#define PAGE_HOLD_MS 12000
#endif

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

static int draw_triangle(SDL_Renderer *renderer, SDL_Texture *texture,
                         SDL_BlendMode blend, const SDL_Vertex vertices[3])
{
    if (texture) {
        if (SDL_SetTextureBlendMode(texture, blend) < 0) {
            return -1;
        }
    } else if (SDL_SetRenderDrawBlendMode(renderer, blend) < 0) {
        return -1;
    }
    return SDL_RenderGeometry(renderer, texture, vertices, 3, NULL, 0);
}

static int clear_semantics_preamble(SDL_Renderer *renderer)
{
    const SDL_Rect viewport = { 80, 60, 480, 360 };
    const SDL_Rect clip = { 20, 20, 100, 80 };
    const SDL_Vertex clipped_triangle[3] = {
        { { -20.0f, -20.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } },
        { { 180.0f, 20.0f }, { 255, 0, 0, 255 }, { 0.0f, 0.0f } },
        { { 40.0f, 160.0f }, { 0, 255, 0, 255 }, { 0.0f, 0.0f } }
    };

    if (SDL_RenderSetViewport(renderer, &viewport) < 0 ||
        SDL_RenderSetClipRect(renderer, &clip) < 0 ||
        SDL_SetRenderDrawColor(renderer, 255, 96, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_NONE, clipped_triangle) < 0 ||
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0) {
        return -1;
    }

    /* The orange full-target clear precedes a clipped draw. This black clear
     * follows that draw and precedes the visible page. Any orange outside the
     * viewport proves that clear incorrectly honored viewport or clip state. */
    return 0;
}

static int render_bounds_page(SDL_Renderer *renderer)
{
    const SDL_Rect viewport = { 40, 30, 560, 420 };
    const SDL_Rect inside_clip = { 30, 20, 100, 60 };
    const SDL_Rect partial_clip = { -25, 100, 80, 60 };
    const SDL_Rect outside_clip = { 600, 0, 20, 20 };
    const SDL_Rect empty_clip = { 0, 0, 0, 50 };
    const SDL_Rect empty_viewport = { 10, 10, 0, 100 };
    const SDL_Rect first_changed_clip = { 180, 20, 60, 50 };
    const SDL_Rect second_changed_clip = { 250, 20, 60, 50 };
    const SDL_Rect one_pixel_wide = { 400, 20, 1, 100 };
    const SDL_Rect one_pixel_high = { 400, 140, 100, 1 };
    const SDL_Rect leakage_clip = { 390, 250, 110, 80 };

    if (clear_semantics_preamble(renderer) < 0 ||
        SDL_RenderSetViewport(renderer, &viewport) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 0, 72, 220, 255,
                  -30, 10, 100, 50) < 0 ||
        SDL_RenderSetClipRect(renderer, &inside_clip) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 0, 220, 80, 255,
                  0, 0, 180, 120) < 0 ||
        SDL_RenderSetClipRect(renderer, &partial_clip) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 255, 220, 0, 255,
                  -80, 80, 180, 100) < 0 ||
        SDL_RenderSetClipRect(renderer, &outside_clip) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255,
                  0, 0, 560, 420) < 0 ||
        SDL_RenderSetClipRect(renderer, &empty_clip) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255,
                  0, 0, 560, 420) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        SDL_RenderSetViewport(renderer, &empty_viewport) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255,
                  0, 0, 200, 200) < 0 ||
        SDL_RenderSetViewport(renderer, &viewport) < 0 ||
        SDL_RenderSetClipRect(renderer, &first_changed_clip) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 220, 32, 32, 255,
                  160, 0, 100, 90) < 0 ||
        SDL_RenderSetClipRect(renderer, &second_changed_clip) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 32, 220, 80, 255,
                  230, 0, 100, 90) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 0, 220, 220, 255,
                  330, 20, 45, 50) < 0 ||
        SDL_RenderSetClipRect(renderer, &one_pixel_wide) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255,
                  380, 0, 60, 140) < 0 ||
        SDL_RenderSetClipRect(renderer, &one_pixel_high) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 255, 255, 255, 255,
                  380, 120, 140, 50) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 24, 24, 24, 255,
                  380, 240, 130, 100) < 0 ||
        SDL_RenderSetClipRect(renderer, &leakage_clip) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 255, 0, 255, 255,
                  350, 210, 200, 160) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        SDL_RenderSetViewport(renderer, NULL) < 0) {
        return -1;
    }

    printf("Page 1/3: black outside viewport; bounded blue/green/yellow blocks,\n");
    printf("changed red/green/cyan blocks, two one-pixel white strips, and a\n");
    printf("magenta block with no leakage into its dark guard area. No orange.\n");
    return 0;
}

static int render_interpolation_page(SDL_Renderer *renderer)
{
    const SDL_Rect left_clip = { 40, 35, 100, 95 };
    const SDL_Rect right_clip = { 180, 35, 100, 95 };
    const SDL_Rect top_clip = { 320, 35, 100, 95 };
    const SDL_Rect bottom_clip = { 460, 35, 100, 95 };
    const SDL_Rect multi_clip = { 40, 175, 140, 120 };
    const SDL_Rect seven_clip = { 220, 175, 120, 100 };
    const SDL_Rect alpha_clip = { 390, 175, 150, 120 };
    const SDL_Vertex left_triangle[3] = {
        { { 5.0f, 80.0f }, { 255, 0, 0, 255 }, { 0.0f, 0.0f } },
        { { 110.0f, 45.0f }, { 0, 255, 0, 255 }, { 0.0f, 0.0f } },
        { { 100.0f, 120.0f }, { 0, 0, 255, 255 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex right_triangle[3] = {
        { { 210.0f, 45.0f }, { 255, 0, 0, 255 }, { 0.0f, 0.0f } },
        { { 315.0f, 80.0f }, { 0, 255, 0, 255 }, { 0.0f, 0.0f } },
        { { 220.0f, 120.0f }, { 0, 0, 255, 255 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex top_triangle[3] = {
        { { 330.0f, 70.0f }, { 255, 0, 0, 255 }, { 0.0f, 0.0f } },
        { { 410.0f, 5.0f }, { 0, 255, 0, 255 }, { 0.0f, 0.0f } },
        { { 400.0f, 120.0f }, { 0, 0, 255, 255 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex bottom_triangle[3] = {
        { { 470.0f, 45.0f }, { 255, 0, 0, 255 }, { 0.0f, 0.0f } },
        { { 550.0f, 70.0f }, { 0, 255, 0, 255 }, { 0.0f, 0.0f } },
        { { 520.0f, 165.0f }, { 0, 0, 255, 255 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex multi_triangle[3] = {
        { { -20.0f, 235.0f }, { 255, 0, 0, 255 }, { 0.0f, 0.0f } },
        { { 110.0f, 135.0f }, { 0, 255, 0, 255 }, { 0.0f, 0.0f } },
        { { 230.0f, 330.0f }, { 0, 0, 255, 255 }, { 0.0f, 0.0f } }
    };
    /* This triangle/rectangle pair produces the proven seven-vertex maximum. */
    const SDL_Vertex seven_triangle[3] = {
        { { -21.47f, -6.96f }, { 255, 0, 0, 160 }, { 0.0f, 0.0f } },
        { { 296.48f, 221.56f }, { 0, 255, 0, 224 }, { 0.0f, 0.0f } },
        { { 688.17f, 677.43f }, { 0, 0, 255, 96 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex alpha_triangle[3] = {
        { { 350.0f, 150.0f }, { 255, 32, 32, 0 }, { 0.0f, 0.0f } },
        { { 570.0f, 190.0f }, { 32, 255, 32, 128 }, { 0.0f, 0.0f } },
        { { 470.0f, 330.0f }, { 32, 32, 255, 255 }, { 0.0f, 0.0f } }
    };

    if (SDL_RenderSetViewport(renderer, NULL) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0 ||
        SDL_RenderSetClipRect(renderer, &left_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_NONE, left_triangle) < 0 ||
        SDL_RenderSetClipRect(renderer, &right_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_NONE, right_triangle) < 0 ||
        SDL_RenderSetClipRect(renderer, &top_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_NONE, top_triangle) < 0 ||
        SDL_RenderSetClipRect(renderer, &bottom_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_NONE, bottom_triangle) < 0 ||
        SDL_RenderSetClipRect(renderer, &multi_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_NONE, multi_triangle) < 0 ||
        SDL_RenderSetClipRect(renderer, &seven_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_BLEND, seven_triangle) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 32, 32, 80, 255,
                  alpha_clip.x, alpha_clip.y, alpha_clip.w, alpha_clip.h) < 0 ||
        SDL_RenderSetClipRect(renderer, &alpha_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_BLEND, alpha_triangle) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0) {
        return -1;
    }

    printf("Page 2/3: four edge-crossing RGB triangles on top, a multi-edge\n");
    printf("triangle, a seven-vertex translucent gradient without fan seams,\n");
    printf("and a smooth clipped RGB/alpha triangle on a blue guard block.\n");
    return 0;
}

static int render_texture_order_page(SDL_Renderer *renderer, SDL_Texture *texture)
{
    const SDL_Rect viewport = { 40, 20, 560, 440 };
    const SDL_Rect texture_clip_a = { 20, 20, 160, 130 };
    const SDL_Rect texture_clip_b = { 220, 20, 160, 130 };
    const SDL_Rect outside_clip = { 20, 200, 100, 80 };
    const SDL_Rect zero_clip = { 160, 200, 100, 80 };
    const SDL_Rect order_clip = { 300, 190, 200, 180 };
    const SDL_Vertex textured_ccw[3] = {
        { { -20.0f, -10.0f }, { 255, 64, 64, 255 }, { 0.0f, 0.0f } },
        { { 210.0f, 30.0f }, { 64, 255, 64, 224 }, { 1.0f, 0.0f } },
        { { 80.0f, 180.0f }, { 64, 64, 255, 160 }, { 0.5f, 1.0f } }
    };
    const SDL_Vertex textured_cw[3] = {
        { { 280.0f, 180.0f }, { 64, 64, 255, 160 }, { 0.5f, 1.0f } },
        { { 410.0f, 30.0f }, { 64, 255, 64, 224 }, { 1.0f, 0.0f } },
        { { 180.0f, -10.0f }, { 255, 64, 64, 255 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex fully_outside[3] = {
        { { 180.0f, 200.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } },
        { { 260.0f, 220.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } },
        { { 220.0f, 280.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } }
    };
    const SDL_Vertex zero_area[3] = {
        { { 175.0f, 220.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } },
        { { 205.0f, 240.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } },
        { { 250.0f, 270.0f }, { 255, 255, 255, 255 }, { 0.0f, 0.0f } }
    };

    if (SDL_RenderSetViewport(renderer, &viewport) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0 ||
        SDL_RenderSetClipRect(renderer, &texture_clip_a) < 0 ||
        draw_triangle(renderer, texture, SDL_BLENDMODE_NONE, textured_ccw) < 0 ||
        SDL_RenderSetClipRect(renderer, &texture_clip_b) < 0 ||
        draw_triangle(renderer, texture, SDL_BLENDMODE_NONE, textured_cw) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 24, 24, 24, 255,
                  outside_clip.x, outside_clip.y, outside_clip.w, outside_clip.h) < 0 ||
        SDL_RenderSetClipRect(renderer, &outside_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_NONE, fully_outside) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 24, 24, 24, 255,
                  zero_clip.x, zero_clip.y, zero_clip.w, zero_clip.h) < 0 ||
        SDL_RenderSetClipRect(renderer, &zero_clip) < 0 ||
        draw_triangle(renderer, NULL, SDL_BLENDMODE_NONE, zero_area) < 0 ||
        SDL_RenderSetClipRect(renderer, &order_clip) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 0, 64, 255, 255,
                  280, 170, 150, 110) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_BLEND, 255, 0, 0, 128,
                  330, 210, 150, 110) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_NONE, 0, 220, 80, 255,
                  380, 250, 130, 90) < 0 ||
        fill_rect(renderer, SDL_BLENDMODE_BLEND, 255, 255, 0, 128,
                  430, 290, 100, 70) < 0 ||
        SDL_RenderSetClipRect(renderer, NULL) < 0 ||
        SDL_RenderSetViewport(renderer, NULL) < 0) {
        return -1;
    }

    printf("Page 3/3: matching clipped checkerboard triangles with opposite\n");
    printf("windings; two empty dark guards; ordered blue/red/green/yellow\n");
    printf("overlaps clipped inside the lower-right panel.\n");
    return 0;
}

static int create_checker_texture(SDL_Renderer *renderer, SDL_Texture **texture_out)
{
    SDL_Texture *texture;
    Uint16 pixels[16 * 16];
    int x;
    int y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!texture) {
        return -1;
    }
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const SDL_bool light = (((x >> 1) + (y >> 1)) & 1) ? SDL_TRUE : SDL_FALSE;

            pixels[y * 16 + x] = light ? 0xffffu : 0x001fu;
        }
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, 16 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(texture);
        return -1;
    }
    *texture_out = texture;
    return 0;
}

static int show_page(SDL_Renderer *renderer, int page)
{
    SDL_RenderPresent(renderer);
    printf("Holding clipping page %d for %d ms\n", page, PAGE_HOLD_MS);
    SDL_Delay(PAGE_HOLD_MS);
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
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
    window = SDL_CreateWindow("PVR clipping", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, OUTPUT_W, OUTPUT_H, 0);
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
    if (create_checker_texture(renderer, &texture) < 0 ||
        render_bounds_page(renderer) < 0 || show_page(renderer, 1) < 0 ||
        render_interpolation_page(renderer) < 0 || show_page(renderer, 2) < 0 ||
        render_texture_order_page(renderer, texture) < 0 ||
        show_page(renderer, 3) < 0) {
        printf("Clipping test failed: %s\n", SDL_GetError());
        goto done;
    }

    printf("PASS (API/runtime): inspect and record the platform's visual gates\n");
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
