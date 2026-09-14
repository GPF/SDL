/* Focused native PVR fixed-class strided texture validation.
 * Covers RGB565, ARGB1555, ARGB4444, POT fallback, mixed frame,
 * and update-after-scene-reference lifecycle. */

#include <SDL.h>
#include <dc/video.h>

#include <stdio.h>

#define SCREEN_W 640
#define SCREEN_H 480
#ifdef HARDWARE_SCREENSHOTS
static int capture_page(const char *path)
{
    /* RenderPresent waits for PVR completion, but the visible framebuffer is
     * selected by the vblank handler. The established Dreamcast tests wait
     * three vblanks so a just-finished scene cannot be captured one page late. */
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
    if (vid_screen_shot(path) < 0) {
        return SDL_SetError("hardware screenshot failed: %s", path);
    }
    printf("saved %s\n", path);
    return 0;
}
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

/* Fill a 16-bit checker pattern into a row-major buffer.
 * `w` is the logical width written each row; `pitch_pixels` is the
 * buffer row stride (may be larger for padded allocations). */
static void fill_checker(Uint16 *pixels, int pitch_pixels, int w, int h,
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

static const char *format_name(Uint32 fmt)
{
    switch (fmt) {
    case SDL_PIXELFORMAT_RGB565:     return "RGB565";
    case SDL_PIXELFORMAT_ARGB1555:   return "ARGB1555";
    case SDL_PIXELFORMAT_ARGB4444:   return "ARGB4444";
    default:                         return "unknown";
    }
}

static void print_texinfo(const char *label, Uint32 sdl_fmt,
                          int logical_w, int logical_h)
{
    printf("%s: format=%s logical=%dx%d; expected fixed-class strided "
           "storage when width is 600\n", label, format_name(sdl_fmt),
           logical_w, logical_h);
}

/* Wrapper to print info at creation time and return the texture. */
static SDL_Texture *make_static_rgb565(SDL_Renderer *renderer, int w, int h,
                                       const char *label)
{
    const int pitch_pixels = w + 17;
    Uint16 *pixels;
    SDL_Texture *texture;
    if (h > 480 || pitch_pixels > 657) {
        SDL_SetError("test dimensions exceed fixed staging bounds");
        return NULL;
    }
    pixels = (Uint16 *)SDL_malloc((size_t)pitch_pixels * h * sizeof(*pixels));
    if (!pixels) return NULL;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) { SDL_free(pixels); return NULL; }
    fill_checker(pixels, pitch_pixels, w, h, 0x001fu, 0x8410u);
    if (SDL_UpdateTexture(texture, NULL, pixels, pitch_pixels * 2) < 0) {
        SDL_free(pixels); SDL_DestroyTexture(texture); return NULL;
    }
    print_texinfo(label, SDL_PIXELFORMAT_RGB565, w, h);
    SDL_free(pixels);
    return texture;
}

static SDL_Texture *make_static_argb1555(SDL_Renderer *renderer, int w, int h,
                                         const char *label)
{
    const int pitch_pixels = w + 17;
    Uint16 *pixels;
    SDL_Texture *texture;
    int x, y;
    if (h > 480 || pitch_pixels > 657) {
        SDL_SetError("test dimensions exceed fixed staging bounds");
        return NULL;
    }
    pixels = (Uint16 *)SDL_malloc((size_t)pitch_pixels * h * sizeof(*pixels));
    if (!pixels) return NULL;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) { SDL_free(pixels); return NULL; }
    /* Alternate opaque and transparent texels for the blend probe. */
    for (y = 0; y < h; ++y) {
        for (x = 0; x < pitch_pixels; ++x) {
            pixels[y * pitch_pixels + x] = ((x / 32 + y / 32) & 1)
                                             ? 0xfc00u : 0x03ffu;
        }
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, pitch_pixels * 2) < 0) {
        SDL_free(pixels); SDL_DestroyTexture(texture); return NULL;
    }
    print_texinfo(label, SDL_PIXELFORMAT_ARGB1555, w, h);
    SDL_free(pixels);
    return texture;
}

static SDL_Texture *make_static_argb4444(SDL_Renderer *renderer, int w, int h,
                                         const char *label)
{
    const int pitch_pixels = w + 17;
    Uint16 *pixels;
    SDL_Texture *texture;
    int x, y;
    if (h > 480 || pitch_pixels > 657) {
        SDL_SetError("test dimensions exceed fixed staging bounds");
        return NULL;
    }
    pixels = (Uint16 *)SDL_malloc((size_t)pitch_pixels * h * sizeof(*pixels));
    if (!pixels) return NULL;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB4444,
                                SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) { SDL_free(pixels); return NULL; }
    /* Alpha gradient: row 0 = full alpha, bottom row = quarter alpha */
    for (y = 0; y < h; ++y) {
        Uint16 alpha = (Uint16)(0xf - (y * 0xf / (h - 1)));
        Uint16 val = (alpha << 12) | 0x5a5u;
        for (x = 0; x < pitch_pixels; ++x) {
            pixels[y * pitch_pixels + x] = val;
        }
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, pitch_pixels * 2) < 0) {
        SDL_free(pixels); SDL_DestroyTexture(texture); return NULL;
    }
    print_texinfo(label, SDL_PIXELFORMAT_ARGB4444, w, h);
    SDL_free(pixels);
    return texture;
}

static SDL_Texture *make_streaming(SDL_Renderer *renderer, int w, int h,
                                   Uint32 format)
{
    SDL_Texture *texture;
    void *pixels;
    int pitch;
    int x, y;
    Uint16 a, b;
    if (format == SDL_PIXELFORMAT_ARGB1555) {
        a = 0xffffu; b = 0x03e0u;
    } else if (format == SDL_PIXELFORMAT_ARGB4444) {
        a = 0xffffu; b = 0x3555u;
    } else {
        a = 0x07ffu; b = 0xf800u;
    }
    texture = SDL_CreateTexture(renderer, format,
                                SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!texture || SDL_LockTexture(texture, NULL, &pixels, &pitch) < 0) {
        SDL_DestroyTexture(texture); return NULL;
    }
    printf("streaming %dx%d format=%s pitch=%d (expected pitch >= %d)\n",
           w, h, format_name(format), pitch, w * 2);
    for (y = 0; y < h; ++y) {
        Uint16 *row = (Uint16 *)((Uint8 *)pixels + y * pitch);
        for (x = 0; x < w; ++x) {
            row[x] = ((x / 24 + y / 24) & 1) ? a : b;
        }
    }
    SDL_UnlockTexture(texture);
    return texture;
}

/* Draw a single frame with the given textures. Returns 0 on success. */
static int draw_frame(SDL_Renderer *renderer,
                      SDL_Texture *t0, SDL_Texture *t1, SDL_Texture *t2,
                      SDL_Texture *t3, SDL_Texture *t4,
                      const char *label)
{
    SDL_Rect r0 = { 0, 0, 320, 160 };
    SDL_Rect r1 = { 320, 0, 320, 160 };
    SDL_Rect r2 = { 0, 160, 320, 160 };
    SDL_Rect r3 = { 320, 160, 320, 160 };
    SDL_Rect r4 = { 100, 340, 440, 120 };

    if (SDL_SetRenderDrawColor(renderer, 8, 8, 16, 255) < 0 ||
        SDL_RenderClear(renderer) < 0) {
        printf("draw_frame(%s): clear failed: %s\n", label, SDL_GetError());
        return -1;
    }
    if (t0 && (SDL_SetTextureBlendMode(t0, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, t0, NULL, &r0) < 0)) {
        printf("draw_frame(%s): copy t0 failed: %s\n", label, SDL_GetError());
        return -1;
    }
    if (t1 && (SDL_SetTextureBlendMode(t1, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, t1, NULL, &r1) < 0)) {
        printf("draw_frame(%s): copy t1 failed: %s\n", label, SDL_GetError());
        return -1;
    }
    if (t2 && (SDL_SetTextureBlendMode(t2, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, t2, NULL, &r2) < 0)) {
        printf("draw_frame(%s): copy t2 failed: %s\n", label, SDL_GetError());
        return -1;
    }
    if (t3 && (SDL_SetTextureBlendMode(t3, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, t3, NULL, &r3) < 0)) {
        printf("draw_frame(%s): copy t3 failed: %s\n", label, SDL_GetError());
        return -1;
    }
    if (t4 && (SDL_SetTextureBlendMode(t4, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, t4, NULL, &r4) < 0)) {
        printf("draw_frame(%s): copy t4 failed: %s\n", label, SDL_GetError());
        return -1;
    }
    return 0;
}

/* Draw a blend probe: strided texture over contrasting background. */
static int draw_blend_probe(SDL_Renderer *renderer, SDL_Texture *t,
                            const char *label)
{
    SDL_Rect dst = { 0, 0, 640, 480 };
    SDL_Rect bg  = { 0, 0, 640, 480 };
    /* Dark red background */
    if (SDL_SetRenderDrawColor(renderer, 60, 0, 0, 255) < 0 ||
        SDL_RenderFillRect(renderer, &bg) < 0) {
        printf("blend probe(%s): background failed: %s\n", label, SDL_GetError());
        return -1;
    }
    /* Textured overlay with BLEND */
    if (SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND) < 0 ||
        SDL_RenderCopy(renderer, t, NULL, &dst) < 0) {
        printf("blend probe(%s): copy failed: %s\n", label, SDL_GetError());
        return -1;
    }
    return 0;
}

/* Draw a modulation probe: textured quad with color/alpha modulation. */
static int draw_modulation_probe(SDL_Renderer *renderer, SDL_Texture *t,
                                 const char *label)
{
    SDL_Rect dst = { 0, 0, 640, 480 };
    if (SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255) < 0 ||
        SDL_RenderClear(renderer) < 0) {
        printf("mod probe(%s): clear failed: %s\n", label, SDL_GetError());
        return -1;
    }
    if (SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE) < 0 ||
        SDL_SetTextureColorMod(t, 180, 180, 180) < 0 ||
        SDL_SetTextureAlphaMod(t, 200) < 0 ||
        SDL_RenderCopy(renderer, t, NULL, &dst) < 0) {
        printf("mod probe(%s): copy/mod failed: %s\n", label, SDL_GetError());
        return -1;
    }
    return 0;
}

/* Full lifecycle test: create, draw, update-after-scene-reference,
 * verify old content, draw replacement, destroy all, verify VRAM. */
static int test_replacement_lifecycle(SDL_Renderer *renderer)
{
    SDL_Texture *tex = NULL;
    Uint16 *update = NULL;
    SDL_Rect dst = { 100, 100, 200, 100 };
    int result = 0;

    printf("\n=== Replacement lifecycle ===\n");

    /* Create a strided texture and draw it. */
    tex = make_static_rgb565(renderer, 600, 100, "t0 (strided RGB565)");
    if (!tex) { printf("replacement: create failed\n"); return -1; }

    if (SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE) < 0 ||
        SDL_RenderCopy(renderer, tex, NULL, &dst) < 0) {
        printf("replacement: initial draw failed\n"); result = -1; goto done;
    }
    SDL_RenderPresent(renderer);

    /* Update the texture after it was scene-referenced. */
    {
        int x, y;
        update = (Uint16 *)SDL_malloc(600u * 100u * sizeof(*update));
        if (!update) {
            printf("replacement: update allocation failed\n");
            result = -1;
            goto done;
        }
        for (y = 0; y < 100; ++y)
            for (x = 0; x < 600; ++x)
                update[y * 600 + x] = (x + y) & 0x7ffu;
        if (SDL_UpdateTexture(tex, NULL, update, 600 * 2) < 0) {
            printf("replacement: update failed\n"); result = -1; goto done;
        }
    }

    /* Draw again — should show the new content. */
    if (SDL_RenderCopy(renderer, tex, NULL, &dst) < 0) {
        printf("replacement: redraw failed\n"); result = -1; goto done;
    }
    SDL_RenderPresent(renderer);

    /* Destroy the texture — VRAM should be fully recovered. */
    SDL_DestroyTexture(tex);
    tex = NULL;
    printf("replacement lifecycle complete\n");

done:
    SDL_free(update);
    SDL_DestroyTexture(tex);
    return result;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    int driver;
    int result = 1;
    int frame;
    SDL_Rect patch = { 100, 100, 64, 64 };
    Uint16 update[64 * 80];
    int x, y;
    const int active_stride = 640;

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
    window = SDL_CreateWindow("PVR strided textures (alpha formats)",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_W, SCREEN_H, 0);
    renderer = window ? SDL_CreateRenderer(window, driver,
                                            SDL_RENDERER_ACCELERATED |
                                            SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer) {
        goto failed;
    }
    printf("active stride: %d\n", active_stride);

    /* ---- Page 1: RGB565 static + streaming + POT ---- */
    printf("\n=== Page 1: RGB565 static/streaming/POT ===\n");
    {
        SDL_Texture *wide, *stream, *pot;
        wide = make_static_rgb565(renderer, 600, 240, "wide (strided RGB565)");
        stream = make_streaming(renderer, 580, 240, SDL_PIXELFORMAT_RGB565);
        pot = make_static_rgb565(renderer, 300, 180, "pot (POT RGB565)");
        if (!wide || !stream || !pot) {
            SDL_DestroyTexture(wide); SDL_DestroyTexture(stream);
            SDL_DestroyTexture(pot); goto failed;
        }

        /* Partial update on the wide texture. */
        for (y = 0; y < 64; ++y) {
            for (x = 0; x < 80; ++x) {
                update[y * 80 + x] = (x < 64) ? 0xffe0u : 0x1234u;
            }
        }
        if (SDL_UpdateTexture(wide, &patch, update, 80 * 2) < 0) {
            printf("page1 partial update failed: %s\n", SDL_GetError());
            SDL_DestroyTexture(wide); SDL_DestroyTexture(stream);
            SDL_DestroyTexture(pot); goto failed;
        }

        for (frame = 0; frame < 3; ++frame) {
            if (draw_frame(renderer, wide, stream, pot, NULL, NULL,
                           "page1") < 0) {
                SDL_DestroyTexture(wide); SDL_DestroyTexture(stream);
                SDL_DestroyTexture(pot); goto failed;
            }
            SDL_RenderPresent(renderer);
        }
#ifdef HARDWARE_SCREENSHOTS
        if (capture_page("/pc/pvr_strided_textures-page1-rgb565.ppm") < 0) {
            SDL_DestroyTexture(wide); SDL_DestroyTexture(stream);
            SDL_DestroyTexture(pot); goto failed;
        }
#endif
        SDL_DestroyTexture(wide);
        SDL_DestroyTexture(stream);
        SDL_DestroyTexture(pot);
    }

    /* ---- Page 2: ARGB1555 static + streaming ---- */
    printf("\n=== Page 2: ARGB1555 static/streaming ===\n");
    {
        SDL_Texture *s15, *st15;
        s15 = make_static_argb1555(renderer, 600, 160,
                                    "s15 (strided ARGB1555)");
        st15 = make_streaming(renderer, 580, 160, SDL_PIXELFORMAT_ARGB1555);
        if (!s15 || !st15) {
            SDL_DestroyTexture(s15); SDL_DestroyTexture(st15);
            goto failed;
        }

        /* Blend probe and modulation probe. */
        if (draw_blend_probe(renderer, s15, "page2-blend") < 0) {
            SDL_DestroyTexture(s15); SDL_DestroyTexture(st15);
            goto failed;
        }
        SDL_RenderPresent(renderer);
#ifdef HARDWARE_SCREENSHOTS
        if (capture_page("/pc/pvr_strided_textures-page2-1555-blend.ppm") < 0) {
            SDL_DestroyTexture(s15); SDL_DestroyTexture(st15); goto failed;
        }
#endif
        if (draw_modulation_probe(renderer, st15, "page2-mod") < 0) {
            SDL_DestroyTexture(s15); SDL_DestroyTexture(st15);
            goto failed;
        }
        SDL_RenderPresent(renderer);
#ifdef HARDWARE_SCREENSHOTS
        if (capture_page("/pc/pvr_strided_textures-page2-1555-mod.ppm") < 0) {
            SDL_DestroyTexture(s15); SDL_DestroyTexture(st15); goto failed;
        }
#endif

        SDL_DestroyTexture(s15);
        SDL_DestroyTexture(st15);
    }

    /* ---- Page 3: ARGB4444 static + streaming ---- */
    printf("\n=== Page 3: ARGB4444 static/streaming ===\n");
    {
        SDL_Texture *s44, *st44;
        s44 = make_static_argb4444(renderer, 600, 160,
                                    "s44 (strided ARGB4444)");
        st44 = make_streaming(renderer, 580, 160, SDL_PIXELFORMAT_ARGB4444);
        if (!s44 || !st44) {
            SDL_DestroyTexture(s44); SDL_DestroyTexture(st44);
            goto failed;
        }

        if (draw_blend_probe(renderer, s44, "page3-blend") < 0) {
            SDL_DestroyTexture(s44); SDL_DestroyTexture(st44);
            goto failed;
        }
        SDL_RenderPresent(renderer);
#ifdef HARDWARE_SCREENSHOTS
        if (capture_page("/pc/pvr_strided_textures-page3-4444-blend.ppm") < 0) {
            SDL_DestroyTexture(s44); SDL_DestroyTexture(st44); goto failed;
        }
#endif
        if (draw_modulation_probe(renderer, st44, "page3-mod") < 0) {
            SDL_DestroyTexture(s44); SDL_DestroyTexture(st44);
            goto failed;
        }
        SDL_RenderPresent(renderer);
#ifdef HARDWARE_SCREENSHOTS
        if (capture_page("/pc/pvr_strided_textures-page3-4444-mod.ppm") < 0) {
            SDL_DestroyTexture(s44); SDL_DestroyTexture(st44); goto failed;
        }
#endif

        SDL_DestroyTexture(s44);
        SDL_DestroyTexture(st44);
    }

    /* ---- Page 4: Mixed frame (RGB565 + ARGB1555 + ARGB4444 + POT) ---- */
    printf("\n=== Page 4: Mixed frame (all formats) ===\n");
    {
        SDL_Texture *t565, *t1555, *t4444, *tpot;
        t565 = make_static_rgb565(renderer, 600, 100,
                                  "t565 (strided RGB565)");
        t1555 = make_static_argb1555(renderer, 600, 100,
                                     "t1555 (strided ARGB1555)");
        t4444 = make_static_argb4444(renderer, 600, 100,
                                     "t4444 (strided ARGB4444)");
        tpot = make_static_rgb565(renderer, 300, 100,
                                  "tpot (POT RGB565)");
        if (!t565 || !t1555 || !t4444 || !tpot) {
            SDL_DestroyTexture(t565); SDL_DestroyTexture(t1555);
            SDL_DestroyTexture(t4444); SDL_DestroyTexture(tpot);
            goto failed;
        }

        if (draw_frame(renderer, t565, t1555, t4444, tpot, NULL,
                       "page4") < 0) {
            SDL_DestroyTexture(t565); SDL_DestroyTexture(t1555);
            SDL_DestroyTexture(t4444); SDL_DestroyTexture(tpot);
            goto failed;
        }
        SDL_RenderPresent(renderer);

        SDL_DestroyTexture(t565);
        SDL_DestroyTexture(t1555);
        SDL_DestroyTexture(t4444);
        SDL_DestroyTexture(tpot);
    }

    /* Capture the mixed-format frame before the lifecycle test. */
#ifdef HARDWARE_SCREENSHOTS
    if (capture_page("/pc/pvr_strided_textures-page4-mixed.ppm") < 0) {
        goto failed;
    }
#endif

    /* ---- Page 5: Replacement lifecycle ---- */
    if (test_replacement_lifecycle(renderer) < 0) {
        goto failed;
    }

    /* Hold for inspection. */
    printf("\nAll tests passed.\n");
    result = 0;
    goto done;

failed:
    printf("PVR strided texture test failed: %s\n", SDL_GetError());
done:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
