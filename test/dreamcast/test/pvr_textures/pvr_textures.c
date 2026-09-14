/* Focused native Dreamcast PVR Phase 3 texture validation. */

#include <SDL.h>

#include <dc/video.h>

#include <stdio.h>

#define LOGICAL_W 17
#define LOGICAL_H 9
#define PADDED_WORDS 20
#define HOLD_MS 10000
#define HARDWARE_SCREENSHOT_PATH "/pc/pvr_textures-hardware.ppm"

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

static int check_renderer_formats(SDL_Renderer *renderer)
{
    SDL_RendererInfo info;

    if (SDL_GetRendererInfo(renderer, &info) < 0) {
        return -1;
    }
    if (info.num_texture_formats != 3 ||
        info.texture_formats[0] != SDL_PIXELFORMAT_RGB565 ||
        info.texture_formats[1] != SDL_PIXELFORMAT_ARGB1555 ||
        info.texture_formats[2] != SDL_PIXELFORMAT_ARGB4444) {
        return SDL_SetError("PVR renderer did not advertise exactly the three Phase 3 formats");
    }
    return 0;
}

static int check_rejections(SDL_Renderer *renderer, SDL_Texture *texture)
{
    SDL_Texture *target;
    SDL_Rect one_pixel = { 0, 0, 1, 1 };
    Uint16 pixel = 0xffffu;

    SDL_ClearError();
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_TARGET, 8, 8);
    if (target) {
        SDL_DestroyTexture(target);
        return SDL_SetError("PVR renderer unexpectedly accepted a target texture");
    }
    if (SDL_GetError()[0] == '\0') {
        return SDL_SetError("Target texture rejection did not set an SDL error");
    }

    SDL_ClearError();
    if (SDL_SetTextureScaleMode(texture, SDL_ScaleModeBest) == 0) {
        return SDL_SetError("PVR renderer unexpectedly accepted best filtering");
    }
    if (SDL_GetError()[0] == '\0') {
        return SDL_SetError("Unsupported filtering rejection did not set an SDL error");
    }
    if (SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest) < 0) {
        return -1;
    }

    SDL_ClearError();
    if (SDL_UpdateTexture(texture, &one_pixel, &pixel, 1) == 0) {
        return SDL_SetError("PVR renderer unexpectedly accepted a short update pitch");
    }
    if (SDL_GetError()[0] == '\0') {
        return SDL_SetError("Short update pitch rejection did not set an SDL error");
    }
    return 0;
}

static SDL_Texture *create_static_rgb565(SDL_Renderer *renderer)
{
    Uint16 pixels[LOGICAL_H][PADDED_WORDS];
    Uint16 partial[3][6];
    SDL_Rect update = { 5, 3, 4, 3 };
    SDL_Texture *texture;
    int x;
    int y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC,
                                LOGICAL_W, LOGICAL_H);
    if (!texture) {
        return NULL;
    }
    for (y = 0; y < LOGICAL_H; ++y) {
        for (x = 0; x < PADDED_WORDS; ++x) {
            pixels[y][x] = 0xf81fu; /* source-pitch guard: magenta */
        }
        for (x = 0; x < LOGICAL_W; ++x) {
            pixels[y][x] = (x == 0 || y == 0 || x == LOGICAL_W - 1 ||
                            y == LOGICAL_H - 1) ? 0x07e0u : 0x001fu;
        }
    }
    if (SDL_UpdateTexture(texture, NULL, pixels,
                          PADDED_WORDS * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    for (y = 0; y < 3; ++y) {
        for (x = 0; x < 6; ++x) {
            partial[y][x] = (x < update.w) ? 0xf800u : 0xffffu;
        }
    }
    if (SDL_UpdateTexture(texture, &update, partial,
                          6 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    return texture;
}

static SDL_Texture *create_streaming_rgb565(SDL_Renderer *renderer)
{
    SDL_Texture *texture;
    SDL_Rect partial = { 6, 2, 5, 4 };
    void *pixels;
    int pitch;
    int x;
    int y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STREAMING,
                                LOGICAL_W, LOGICAL_H);
    if (!texture) {
        return NULL;
    }
    if (SDL_LockTexture(texture, NULL, &pixels, &pitch) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    for (y = 0; y < LOGICAL_H; ++y) {
        Uint16 *row = (Uint16 *)((Uint8 *)pixels + y * pitch);
        for (x = 0; x < LOGICAL_W; ++x) {
            row[x] = ((x ^ y) & 1) ? 0x07ffu : 0xf800u;
        }
    }
    SDL_UnlockTexture(texture);

    if (SDL_LockTexture(texture, &partial, &pixels, &pitch) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    SDL_ClearError();
    {
        void *nested_pixels;
        int nested_pitch;
        if (SDL_LockTexture(texture, &partial, &nested_pixels, &nested_pitch) == 0) {
            SDL_UnlockTexture(texture);
            SDL_DestroyTexture(texture);
            SDL_SetError("PVR renderer unexpectedly accepted a nested texture lock");
            return NULL;
        }
    }
    for (y = 0; y < partial.h; ++y) {
        Uint16 *row = (Uint16 *)((Uint8 *)pixels + y * pitch);
        for (x = 0; x < partial.w; ++x) {
            row[x] = 0xffe0u;
        }
    }
    SDL_UnlockTexture(texture);
    return texture;
}

static SDL_Texture *create_argb1555(SDL_Renderer *renderer)
{
    Uint16 pixels[8 * 8];
    SDL_Texture *texture;
    int x;
    int y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!texture) {
        return NULL;
    }
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            pixels[y * 8 + x] = ((x + y) & 1) ? 0xfc00u : 0x03ffu;
        }
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, 8 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    return texture;
}

static SDL_Texture *create_argb4444(SDL_Renderer *renderer)
{
    Uint16 pixels[8 * 8];
    SDL_Texture *texture;
    int x;
    int y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB4444,
                                SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!texture) {
        return NULL;
    }
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            const Uint16 alpha = (Uint16)((x * 15 / 7) << 12);
            pixels[y * 8 + x] = (Uint16)(alpha | 0x00ffu);
        }
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, 8 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    return texture;
}

static int fill(SDL_Renderer *renderer, SDL_BlendMode blend,
                Uint8 r, Uint8 g, Uint8 b, Uint8 a, SDL_Rect rect)
{
    if (SDL_SetRenderDrawBlendMode(renderer, blend) < 0 ||
        SDL_SetRenderDrawColor(renderer, r, g, b, a) < 0) {
        return -1;
    }
    return SDL_RenderFillRect(renderer, &rect);
}

static int draw_scene(SDL_Renderer *renderer, SDL_Texture *rgb,
                      SDL_Texture *stream, SDL_Texture *a1555,
                      SDL_Texture *a4444)
{
    SDL_Rect rgb_dst = { 20, 20, 190, 100 };
    SDL_Rect stream_dst = { 230, 20, 190, 100 };
    SDL_Rect a1555_bg = { 440, 20, 80, 100 };
    SDL_Rect a1555_dst = { 440, 20, 80, 100 };
    SDL_Rect a4444_bg = { 540, 20, 80, 100 };
    SDL_Rect a4444_dst = { 540, 20, 80, 100 };
    SDL_Rect nearest_dst = { 20, 150, 280, 120 };
    SDL_Rect linear_dst = { 340, 150, 280, 120 };
    SDL_Rect mod_bg = { 20, 300, 180, 80 };
    SDL_Rect mod_dst = { 20, 300, 180, 80 };
    SDL_Rect alpha_guard = { 230, 300, 180, 80 };
    SDL_Rect alpha_dst = { 230, 300, 180, 80 };
    SDL_Rect blue = { 440, 300, 130, 100 };
    SDL_Rect green = { 500, 340, 120, 80 };
    SDL_Rect ordered = { 470, 320, 120, 80 };

    if (SDL_SetRenderDrawColor(renderer, 12, 12, 18, 255) < 0 ||
        SDL_RenderClear(renderer) < 0 ||
        SDL_SetTextureBlendMode(rgb, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, rgb, NULL, &rgb_dst) < 0 ||
        SDL_RenderCopy(renderer, stream, NULL, &stream_dst) < 0 ||
        fill(renderer, SDL_BLENDMODE_NONE, 0, 40, 180, 255, a1555_bg) < 0 ||
        SDL_SetTextureBlendMode(a1555, SDL_BLENDMODE_BLEND) < 0 ||
        SDL_RenderCopy(renderer, a1555, NULL, &a1555_dst) < 0 ||
        fill(renderer, SDL_BLENDMODE_NONE, 140, 40, 0, 255, a4444_bg) < 0 ||
        SDL_SetTextureBlendMode(a4444, SDL_BLENDMODE_BLEND) < 0 ||
        SDL_RenderCopy(renderer, a4444, NULL, &a4444_dst) < 0) {
        return -1;
    }

    if (SDL_SetTextureScaleMode(stream, SDL_ScaleModeNearest) < 0 ||
        SDL_SetTextureBlendMode(stream, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, stream, NULL, &nearest_dst) < 0 ||
        SDL_RenderFlush(renderer) < 0 ||
        SDL_SetTextureScaleMode(stream, SDL_ScaleModeLinear) < 0 ||
        SDL_RenderCopy(renderer, stream, NULL, &linear_dst) < 0) {
        return -1;
    }

    if (fill(renderer, SDL_BLENDMODE_NONE, 0, 60, 180, 255, mod_bg) < 0 ||
        SDL_SetTextureBlendMode(rgb, SDL_BLENDMODE_BLEND) < 0 ||
        SDL_SetTextureColorMod(rgb, 255, 128, 128) < 0 ||
        SDL_SetTextureAlphaMod(rgb, 128) < 0 ||
        SDL_RenderCopy(renderer, rgb, NULL, &mod_dst) < 0 ||
        SDL_SetTextureColorMod(rgb, 255, 255, 255) < 0 ||
        SDL_SetTextureAlphaMod(rgb, 255) < 0) {
        return -1;
    }

    /* Red RGB565 texels have zero low blue bits. If sampled texture alpha is
     * accidentally enabled, this red/cyan checker disappears over the guard. */
    if (fill(renderer, SDL_BLENDMODE_NONE, 100, 0, 100, 255, alpha_guard) < 0 ||
        SDL_SetTextureBlendMode(stream, SDL_BLENDMODE_BLEND) < 0 ||
        SDL_RenderCopy(renderer, stream, NULL, &alpha_dst) < 0 ||
        fill(renderer, SDL_BLENDMODE_NONE, 0, 70, 255, 255, blue) < 0 ||
        SDL_SetTextureBlendMode(a4444, SDL_BLENDMODE_BLEND) < 0 ||
        SDL_RenderCopy(renderer, a4444, NULL, &ordered) < 0 ||
        fill(renderer, SDL_BLENDMODE_NONE, 0, 220, 80, 255, green) < 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *rgb = NULL;
    SDL_Texture *stream = NULL;
    SDL_Texture *a1555 = NULL;
    SDL_Texture *a4444 = NULL;
    int driver;
    int result = 1;
    int frame;

    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    driver = find_pvr_driver();
    if (driver < 0) {
        printf("Dreamcast PVR renderer is not available\n");
        goto done;
    }
    window = SDL_CreateWindow("PVR Phase 3 textures", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
    if (!window) {
        goto failed;
    }
    renderer = SDL_CreateRenderer(window, driver, SDL_RENDERER_ACCELERATED |
                                                   SDL_RENDERER_PRESENTVSYNC);
    if (!renderer || check_renderer_formats(renderer) < 0) {
        goto failed;
    }
    rgb = create_static_rgb565(renderer);
    stream = create_streaming_rgb565(renderer);
    a1555 = create_argb1555(renderer);
    a4444 = create_argb4444(renderer);
    if (!rgb || !stream || !a1555 || !a4444 ||
        check_rejections(renderer, rgb) < 0) {
        goto failed;
    }
    for (frame = 0; frame < 3; ++frame) {
        if (draw_scene(renderer, rgb, stream, a1555, a4444) < 0) {
            goto failed;
        }
        SDL_RenderPresent(renderer);
    }
#ifdef PVR_TEXTURE_HARDWARE_SCREENSHOT
    {
        /* RenderPresent waits for rendering, while the PVR page flip occurs
         * from the vblank handler. Wait for that flip before reading the
         * currently displayed framebuffer through KOS. */
        vid_waitvbl();
        if (vid_screen_shot(HARDWARE_SCREENSHOT_PATH) < 0) {
            SDL_SetError("Could not save hardware screenshot to %s",
                         HARDWARE_SCREENSHOT_PATH);
            goto failed;
        }
        printf("Saved hardware screenshot to %s\n",
               HARDWARE_SCREENSHOT_PATH);
    }
#endif
    printf("Expected: bordered NPOT static update; streaming checker with yellow partial lock;\n");
    printf("1555 cutouts and 4444 fade; sharp nearest and smooth linear panels;\n");
    printf("modulation, opaque RGB565 red texels, and blue/alpha/green painter order.\n");
    printf("Holding Phase 3 texture frame for %d ms\n", HOLD_MS);
    SDL_Delay(HOLD_MS);
    printf("PASS (API/runtime): physical Dreamcast texture validation pending\n");
    result = 0;
    goto done;

failed:
    printf("PVR texture test failed: %s\n", SDL_GetError());
done:
    SDL_DestroyTexture(a4444);
    SDL_DestroyTexture(a1555);
    SDL_DestroyTexture(stream);
    SDL_DestroyTexture(rgb);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
