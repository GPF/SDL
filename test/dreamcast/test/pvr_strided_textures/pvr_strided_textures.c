/* Focused native PVR fixed-class strided RGB565 validation. */

#include <SDL.h>
#include <dc/video.h>

#include <stdio.h>

#define SCREEN_W 640
#define SCREEN_H 480
#define HOLD_MS 6000

#ifdef HARDWARE_SCREENSHOTS
static const char *const screenshot_path =
    "/pc/pvr_strided_textures-phase6_2.ppm";
#endif

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

static void fill_pattern(Uint16 *pixels, int pitch_pixels, int w, int h,
                         Uint16 a, Uint16 b)
{
    int x, y;
    for (y = 0; y < h; ++y) {
        Uint16 *row = pixels + y * pitch_pixels;
        for (x = 0; x < pitch_pixels; ++x) {
            row[x] = 0xf81fu;
        }
        for (x = 0; x < w; ++x) {
            row[x] = ((x / 32 + y / 32) & 1) ? a : b;
        }
        row[0] = 0xf800u;
        row[w - 1] = 0xffffu;
    }
    for (x = 0; x < w; ++x) {
        pixels[(h - 1) * pitch_pixels + x] = 0x07e0u;
    }
}

static SDL_Texture *make_static(SDL_Renderer *renderer, int w, int h)
{
    const int pitch_pixels = w + 17;
    Uint16 *pixels;
    SDL_Texture *texture;

    if (h > 480 || pitch_pixels > 657) {
        SDL_SetError("test dimensions exceed fixed staging bounds");
        return NULL;
    }
    pixels = (Uint16 *)SDL_malloc((size_t)pitch_pixels * h * sizeof(*pixels));
    if (!pixels) {
        return NULL;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) {
        SDL_free(pixels);
        return NULL;
    }
    fill_pattern(pixels, pitch_pixels, w, h, 0x001fu, 0x8410u);
    if (SDL_UpdateTexture(texture, NULL, pixels, pitch_pixels * 2) < 0) {
        SDL_free(pixels);
        SDL_DestroyTexture(texture);
        return NULL;
    }
    SDL_free(pixels);
    return texture;
}

static SDL_Texture *make_streaming(SDL_Renderer *renderer, int w, int h)
{
    SDL_Texture *texture;
    void *pixels;
    int pitch;
    int x, y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!texture || SDL_LockTexture(texture, NULL, &pixels, &pitch) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    printf("streaming %dx%d pitch=%d (expected fixed-class pitch >= %d)\n",
           w, h, pitch, w * 2);
    for (y = 0; y < h; ++y) {
        Uint16 *row = (Uint16 *)((Uint8 *)pixels + y * pitch);
        for (x = 0; x < w; ++x) {
            row[x] = ((x / 24 + y / 24) & 1) ? 0x07ffu : 0xf800u;
        }
    }
    SDL_UnlockTexture(texture);
    return texture;
}

static int draw_frame(SDL_Renderer *renderer, SDL_Texture *wide,
                      SDL_Texture *stream, SDL_Texture *pot)
{
    SDL_Rect wide_dst = { 0, 0, 320, 240 };
    SDL_Rect stream_dst = { 320, 0, 320, 240 };
    SDL_Rect pot_dst = { 120, 260, 400, 180 };

    if (SDL_SetRenderDrawColor(renderer, 8, 8, 16, 255) < 0 ||
        SDL_RenderClear(renderer) < 0 ||
        SDL_SetTextureBlendMode(wide, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, wide, NULL, &wide_dst) < 0 ||
        SDL_SetTextureBlendMode(stream, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, stream, NULL, &stream_dst) < 0 ||
        SDL_SetTextureBlendMode(pot, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, pot, NULL, &pot_dst) < 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *wide = NULL;
    SDL_Texture *stream = NULL;
    SDL_Texture *pot = NULL;
    int driver;
    int result = 1;
    int frame;
    SDL_Rect patch = { 100, 100, 64, 64 };
    Uint16 update[64 * 80];
    int x, y;

    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    driver = find_pvr_driver();
    if (driver < 0) {
        printf("Dreamcast PVR renderer is not available\n");
        goto done;
    }
    window = SDL_CreateWindow("PVR strided textures", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, 0);
    renderer = window ? SDL_CreateRenderer(window, driver,
                                            SDL_RENDERER_ACCELERATED |
                                            SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer) {
        goto failed;
    }

    wide = make_static(renderer, 600, 240);
    stream = make_streaming(renderer, 580, 240);
    pot = make_static(renderer, 300, 180);
    if (!wide || !stream || !pot) {
        goto failed;
    }
    printf("expected: 600/580 RGB565 textures use the renderer's 640 stride;\n");
    printf("expected: 300-pixel texture remains POT; POT and strided panels coexist.\n");

    for (y = 0; y < 64; ++y) {
        for (x = 0; x < 80; ++x) {
            update[y * 80 + x] = (x < 64) ? 0xffe0u : 0x1234u;
        }
    }
    if (SDL_UpdateTexture(wide, &patch, update, 80 * 2) < 0) {
        goto failed;
    }
    for (frame = 0; frame < 3; ++frame) {
        if (draw_frame(renderer, wide, stream, pot) < 0) {
            goto failed;
        }
        SDL_RenderPresent(renderer);
    }
#ifdef HARDWARE_SCREENSHOTS
    vid_waitvbl();
    if (vid_screen_shot(screenshot_path) < 0) {
        SDL_SetError("hardware screenshot failed");
        goto failed;
    }
    printf("saved %s\n", screenshot_path);
#endif
    printf("Holding strided texture frame for %d ms\n", HOLD_MS);
    SDL_Delay(HOLD_MS);
    result = 0;
    goto done;

failed:
    printf("PVR strided texture test failed: %s\n", SDL_GetError());
done:
    SDL_DestroyTexture(pot);
    SDL_DestroyTexture(stream);
    SDL_DestroyTexture(wide);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
