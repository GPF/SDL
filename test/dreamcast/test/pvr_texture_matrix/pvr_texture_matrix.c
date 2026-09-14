#include <SDL.h>
#include <kos.h>

#include <stdio.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define PAGE_COUNT 4
#define PAGE_HOLD_MS 12000
#define BYTES_PER_PIXEL 2

typedef struct MatrixStats
{
    int cases;
    int creates;
    int updates;
    int locks;
    int invalid_checks;
    int pages;
    int live_textures;
    int failures;
    int capture_failures;
} MatrixStats;

typedef struct DimensionCase
{
    int w;
    int h;
    Uint32 format;
    int access;
} DimensionCase;

static MatrixStats stats;

#ifdef PVR_TEXTURE_MATRIX_HARDWARE_SCREENSHOT
static const char *const hardware_paths[PAGE_COUNT] = {
    "/pc/pvr_texture_matrix-hardware-page1.ppm",
    "/pc/pvr_texture_matrix-hardware-page2.ppm",
    "/pc/pvr_texture_matrix-hardware-page3.ppm",
    "/pc/pvr_texture_matrix-hardware-page4.ppm"
};
#endif

static const DimensionCase dimension_cases[] = {
    { 1, 513, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING },
    { 2, 257, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STATIC },
    { 3, 129, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING },
    { 7, 65, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC },
    { 8, 8, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STREAMING },
    { 9, 63, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STATIC },
    { 15, 15, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING },
    { 16, 16, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STATIC },
    { 17, 31, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING },
    { 31, 17, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC },
    { 32, 32, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STREAMING },
    { 33, 15, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STATIC },
    { 63, 9, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING },
    { 64, 64, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STATIC },
    { 65, 7, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING },
    { 127, 3, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC },
    { 128, 128, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STREAMING },
    { 129, 2, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STATIC },
    { 255, 255, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING },
    { 256, 256, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STATIC },
    { 257, 127, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING },
    { 319, 240, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC },
    { 320, 240, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STREAMING },
    { 321, 241, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STATIC },
    { 511, 129, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING },
    { 512, 128, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STATIC },
    { 513, 65, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING },
    { 513, 257, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC }
};

static int fail(const char *what)
{
    ++stats.failures;
    printf("FAIL: %s: %s\n", what, SDL_GetError());
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return fail(#call); } while (0)

static int next_pot(int value)
{
    int result = 8;

    while (result < value) {
        result <<= 1;
    }
    return result;
}

static const char *format_name(Uint32 format)
{
    switch (format) {
    case SDL_PIXELFORMAT_RGB565:
        return "RGB565";
    case SDL_PIXELFORMAT_ARGB1555:
        return "ARGB1555";
    case SDL_PIXELFORMAT_ARGB4444:
        return "ARGB4444";
    default:
        return "unknown";
    }
}

static Uint16 pack_pixel(Uint32 format, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (format == SDL_PIXELFORMAT_RGB565) {
        return (Uint16)(((Uint16)(r >> 3) << 11) |
                        ((Uint16)(g >> 2) << 5) | (Uint16)(b >> 3));
    }
    if (format == SDL_PIXELFORMAT_ARGB1555) {
        return (Uint16)(((a >= 128 ? 1u : 0u) << 15) |
                        ((Uint16)(r >> 3) << 10) |
                        ((Uint16)(g >> 3) << 5) | (Uint16)(b >> 3));
    }
    return (Uint16)(((Uint16)(a >> 4) << 12) |
                    ((Uint16)(r >> 4) << 8) |
                    ((Uint16)(g >> 4) << 4) | (Uint16)(b >> 4));
}

static Uint16 pattern_pixel(Uint32 format, int x, int y, int w, int h)
{
    Uint8 r = (Uint8)(((x / 2 + y / 3) & 1) ? 38 : 78);
    Uint8 g = (Uint8)(((x / 3 + y / 2) & 1) ? 48 : 88);
    Uint8 b = (Uint8)(((x + y) & 1) ? 86 : 42);
    Uint8 a = 255;

    if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
        r = 230;
        g = 230;
        b = 230;
    }
    if (x == w / 2) {
        r = 20;
        g = 210;
        b = 220;
    }
    if (y == h / 2) {
        r = 220;
        g = 40;
        b = 210;
    }
    if (x == 0 && y == 0) {
        r = 255; g = 24; b = 24;
    } else if (x == w - 1 && y == 0) {
        r = 24; g = 255; b = 24;
    } else if (x == 0 && y == h - 1) {
        r = 24; g = 48; b = 255;
    } else if (x == w - 1 && y == h - 1) {
        r = 255; g = 240; b = 24;
    }
    if (format == SDL_PIXELFORMAT_ARGB1555 && x > 0 && y > 0 &&
        x < w - 1 && y < h - 1 && ((x + 2 * y) % 7) == 0) {
        a = 0;
    } else if (format == SDL_PIXELFORMAT_ARGB4444 && w > 1) {
        a = (Uint8)(48 + (207 * x) / (w - 1));
    }
    return pack_pixel(format, r, g, b, a);
}

static void fill_pattern(Uint8 *pixels, int pitch, Uint32 format, int w, int h)
{
    int x;
    int y;

    for (y = 0; y < h; ++y) {
        Uint8 *row = pixels + (size_t)y * pitch;

        for (x = 0; x < w; ++x) {
            const Uint16 value = pattern_pixel(format, x, y, w, h);

            SDL_memcpy(row + (size_t)x * BYTES_PER_PIXEL, &value,
                       sizeof(value));
        }
    }
}

static SDL_Texture *create_texture(SDL_Renderer *renderer, Uint32 format,
                                   int access, int w, int h)
{
    SDL_Texture *texture = SDL_CreateTexture(renderer, format, access, w, h);

    if (!texture) {
        fail("SDL_CreateTexture");
        return NULL;
    }
    ++stats.creates;
    ++stats.live_textures;
    return texture;
}

static void destroy_texture(SDL_Texture **texture)
{
    if (*texture) {
        SDL_DestroyTexture(*texture);
        *texture = NULL;
        --stats.live_textures;
    }
}

static int update_pattern(SDL_Texture *texture, Uint32 format, int w, int h,
                          int extra_pitch)
{
    const int pitch = w * BYTES_PER_PIXEL + extra_pitch;
    Uint8 *pixels = (Uint8 *)SDL_malloc((size_t)pitch * h);
    int result;

    if (!pixels) {
        return fail("SDL_malloc update pattern");
    }
    SDL_memset(pixels, 0x5a, (size_t)pitch * h);
    fill_pattern(pixels, pitch, format, w, h);
    result = SDL_UpdateTexture(texture, NULL, pixels, pitch);
    SDL_free(pixels);
    if (result < 0) {
        return fail("SDL_UpdateTexture full pattern");
    }
    ++stats.updates;
    return 0;
}

static int update_solid(SDL_Texture *texture, Uint32 format,
                        const SDL_Rect *rect, int extra_pitch,
                        Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    const int pitch = rect->w * BYTES_PER_PIXEL + extra_pitch;
    Uint8 *pixels = (Uint8 *)SDL_malloc((size_t)pitch * rect->h);
    const Uint16 value = pack_pixel(format, r, g, b, a);
    int x;
    int y;
    int result;

    if (!pixels) {
        return fail("SDL_malloc update solid");
    }
    SDL_memset(pixels, 0xa5, (size_t)pitch * rect->h);
    for (y = 0; y < rect->h; ++y) {
        Uint8 *row = pixels + (size_t)y * pitch;

        for (x = 0; x < rect->w; ++x) {
            SDL_memcpy(row + (size_t)x * BYTES_PER_PIXEL, &value,
                       sizeof(value));
        }
    }
    result = SDL_UpdateTexture(texture, rect, pixels, pitch);
    SDL_free(pixels);
    if (result < 0) {
        return fail("SDL_UpdateTexture partial solid");
    }
    ++stats.updates;
    return 0;
}

static int lock_pattern(SDL_Texture *texture, Uint32 format, int w, int h)
{
    void *pixels = NULL;
    int pitch = 0;
    const int expected_pitch = next_pot(w) * BYTES_PER_PIXEL;

    REQUIRE(SDL_LockTexture(texture, NULL, &pixels, &pitch));
    if (!pixels || pitch != expected_pitch || pitch < w * BYTES_PER_PIXEL) {
        SDL_UnlockTexture(texture);
        SDL_SetError("full lock pitch %d, expected %d", pitch, expected_pitch);
        return fail("streaming full lock pitch");
    }
    fill_pattern((Uint8 *)pixels, pitch, format, w, h);
    SDL_UnlockTexture(texture);
    ++stats.locks;
    return 0;
}

static int lock_solid(SDL_Texture *texture, Uint32 format, int logical_w,
                      const SDL_Rect *rect, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    void *pixels = NULL;
    int pitch = 0;
    const int expected_pitch = next_pot(logical_w) * BYTES_PER_PIXEL;
    const Uint16 value = pack_pixel(format, r, g, b, a);
    int x;
    int y;

    REQUIRE(SDL_LockTexture(texture, rect, &pixels, &pitch));
    if (!pixels || pitch != expected_pitch) {
        SDL_UnlockTexture(texture);
        SDL_SetError("partial lock pitch %d, expected %d", pitch, expected_pitch);
        return fail("streaming partial lock pitch");
    }
    for (y = 0; y < rect->h; ++y) {
        Uint16 *row = (Uint16 *)((Uint8 *)pixels + (size_t)y * pitch);

        for (x = 0; x < rect->w; ++x) {
            row[x] = value;
        }
    }
    SDL_UnlockTexture(texture);
    ++stats.locks;
    return 0;
}

static int exercise_static_edges(SDL_Texture *texture, Uint32 format, int w, int h)
{
    SDL_Rect rect;
    const int iw = w > 4 ? 3 : 1;
    const int ih = h > 4 ? 3 : 1;

    rect = (SDL_Rect){ 0, 0, 1, 1 };
    REQUIRE(update_solid(texture, format, &rect, 0, 255, 0, 0, 255));
    rect.x = w - 1;
    REQUIRE(update_solid(texture, format, &rect, 3, 0, 255, 0, 255));
    rect.y = h - 1;
    REQUIRE(update_solid(texture, format, &rect, 1, 255, 255, 0, 255));
    rect.x = 0;
    REQUIRE(update_solid(texture, format, &rect, 5, 0, 0, 255, 255));
    rect = (SDL_Rect){ 0, 0, w, 1 };
    REQUIRE(update_solid(texture, format, &rect, 1, 255, 80, 80, 255));
    rect.y = h - 1;
    REQUIRE(update_solid(texture, format, &rect, 3, 80, 80, 255, 255));
    rect = (SDL_Rect){ 0, 0, 1, h };
    REQUIRE(update_solid(texture, format, &rect, 1, 80, 255, 80, 255));
    rect.x = w - 1;
    REQUIRE(update_solid(texture, format, &rect, 5, 255, 80, 255, 255));
    rect = (SDL_Rect){ w > iw ? (w - iw) / 2 : 0,
                       h > ih ? (h - ih) / 2 : 0, iw, ih };
    REQUIRE(update_solid(texture, format, &rect, 3, 255, 255, 255, 255));
    rect = (SDL_Rect){ w - iw, h > ih ? 1 : 0, iw, ih };
    REQUIRE(update_solid(texture, format, &rect, 1, 255, 140, 0, 255));
    rect = (SDL_Rect){ w > iw ? 1 : 0, h - ih, iw, ih };
    REQUIRE(update_solid(texture, format, &rect, 3, 0, 220, 220, 255));
    rect = (SDL_Rect){ w - iw, h - ih, iw, ih };
    REQUIRE(update_solid(texture, format, &rect, 5, 240, 240, 0, 255));
    REQUIRE(update_solid(texture, format, &rect, 1, 240, 0, 120, 255));
    if (w > 2 && h > 2) {
        rect = (SDL_Rect){ 1, 1, w - 2, h - 2 };
        REQUIRE(update_solid(texture, format, &rect, 3, 35, 60, 95, 255));
    }
    REQUIRE(update_pattern(texture, format, w, h, 5));
    rect = (SDL_Rect){ w - 1, 0, 1, h };
    REQUIRE(update_solid(texture, format, &rect, 3, 255, 120, 0, 255));
    rect = (SDL_Rect){ 0, h - 1, w, 1 };
    REQUIRE(update_solid(texture, format, &rect, 1, 40, 80, 255, 255));
    return 0;
}

static int exercise_streaming_edges(SDL_Texture *texture, Uint32 format,
                                    int w, int h)
{
    SDL_Rect rect;
    const int iw = w > 4 ? 3 : 1;
    const int ih = h > 4 ? 3 : 1;

    rect = (SDL_Rect){ 0, 0, 1, 1 };
    REQUIRE(lock_solid(texture, format, w, &rect, 255, 0, 0, 255));
    rect.x = w - 1;
    REQUIRE(lock_solid(texture, format, w, &rect, 0, 255, 0, 255));
    rect.y = h - 1;
    REQUIRE(lock_solid(texture, format, w, &rect, 255, 255, 0, 255));
    rect.x = 0;
    REQUIRE(lock_solid(texture, format, w, &rect, 0, 0, 255, 255));
    rect = (SDL_Rect){ 0, h > 1 ? h / 2 : 0, w, 1 };
    REQUIRE(lock_solid(texture, format, w, &rect, 255, 80, 80, 255));
    rect = (SDL_Rect){ w > 1 ? w / 2 : 0, 0, 1, h };
    REQUIRE(lock_solid(texture, format, w, &rect, 80, 255, 80, 255));
    rect = (SDL_Rect){ w > iw ? (w - iw) / 2 : 0,
                       h > ih ? (h - ih) / 2 : 0, iw, ih };
    REQUIRE(lock_solid(texture, format, w, &rect, 255, 255, 255, 255));
    rect = (SDL_Rect){ w - iw, h > ih ? 1 : 0, iw, ih };
    REQUIRE(lock_solid(texture, format, w, &rect, 255, 140, 0, 255));
    rect = (SDL_Rect){ w > iw ? 1 : 0, h - ih, iw, ih };
    REQUIRE(lock_solid(texture, format, w, &rect, 0, 220, 220, 255));
    rect = (SDL_Rect){ w - iw, h - ih, iw, ih };
    REQUIRE(lock_solid(texture, format, w, &rect, 240, 240, 0, 255));
    REQUIRE(lock_solid(texture, format, w, &rect, 240, 0, 120, 255));
    REQUIRE(lock_pattern(texture, format, w, h));
    rect = (SDL_Rect){ 0, 0, 1, h };
    REQUIRE(lock_solid(texture, format, w, &rect, 0, 255, 80, 255));
    rect = (SDL_Rect){ 0, 0, w, 1 };
    REQUIRE(lock_solid(texture, format, w, &rect, 255, 120, 0, 255));
    return 0;
}

static int run_dimension_matrix(SDL_Renderer *renderer)
{
    size_t i;

    for (i = 0; i < SDL_arraysize(dimension_cases); ++i) {
        const DimensionCase *test = &dimension_cases[i];
        const int backing_w = next_pot(test->w);
        const int backing_h = next_pot(test->h);
        const int cpu_pitch = backing_w * BYTES_PER_PIXEL;
        const size_t allocation_size = (size_t)cpu_pitch * backing_h;
        SDL_Texture *texture;

        printf("MATRIX %02u: %dx%d -> %dx%d pitch=%d bytes=%lu %s %s\n",
               (unsigned)(i + 1), test->w, test->h, backing_w, backing_h,
               cpu_pitch, (unsigned long)allocation_size,
               format_name(test->format),
               test->access == SDL_TEXTUREACCESS_STATIC ? "static" : "streaming");
        texture = create_texture(renderer, test->format, test->access,
                                 test->w, test->h);
        if (!texture) {
            return -1;
        }
        if (test->access == SDL_TEXTUREACCESS_STATIC) {
            if (update_pattern(texture, test->format, test->w, test->h,
                               (int)(i % 3) * 2 + 1) < 0) {
                destroy_texture(&texture);
                return -1;
            }
        } else if (lock_pattern(texture, test->format, test->w, test->h) < 0) {
            destroy_texture(&texture);
            return -1;
        }
        destroy_texture(&texture);
        ++stats.cases;
    }
    return 0;
}

static int expect_update_result(SDL_Texture *texture, const SDL_Rect *rect,
                                const void *pixels, int pitch, int expected,
                                const char *name)
{
    const int result = SDL_UpdateTexture(texture, rect, pixels, pitch);

    ++stats.invalid_checks;
    if ((expected == 0 && result != 0) || (expected < 0 && result >= 0)) {
        SDL_SetError("%s returned %d, expected %s", name, result,
                     expected == 0 ? "success/no-op" : "failure");
        return fail("invalid update contract");
    }
    SDL_ClearError();
    return 0;
}

static int expect_lock_failure(SDL_Texture *texture, const SDL_Rect *rect,
                               const char *name)
{
    void *pixels = NULL;
    int pitch = 0;
    const int result = SDL_LockTexture(texture, rect, &pixels, &pitch);

    ++stats.invalid_checks;
    if (result >= 0) {
        SDL_UnlockTexture(texture);
        SDL_SetError("%s unexpectedly succeeded", name);
        return fail("invalid lock contract");
    }
    SDL_ClearError();
    return 0;
}

static int run_invalid_api_checks(SDL_Renderer *renderer)
{
    SDL_Texture *static_texture = NULL;
    SDL_Texture *streaming_texture = NULL;
    SDL_Texture *invalid = NULL;
    Uint16 pixels[8 * 8];
    SDL_Rect rect;

    SDL_memset(pixels, 0, sizeof(pixels));
    static_texture = create_texture(renderer, SDL_PIXELFORMAT_RGB565,
                                    SDL_TEXTUREACCESS_STATIC, 8, 8);
    streaming_texture = create_texture(renderer, SDL_PIXELFORMAT_RGB565,
                                       SDL_TEXTUREACCESS_STREAMING, 8, 8);
    if (!static_texture || !streaming_texture) {
        destroy_texture(&static_texture);
        destroy_texture(&streaming_texture);
        return -1;
    }

    rect = (SDL_Rect){ 9, 0, 1, 1 };
    REQUIRE(expect_update_result(static_texture, &rect, pixels, 2, 0,
                                 "fully outside update (SDL core no-op)"));
    rect = (SDL_Rect){ 7, 0, 2, 1 };
    REQUIRE(expect_update_result(static_texture, &rect, pixels, 4, 0,
                                 "right-clipped update (SDL core clip)"));
    rect = (SDL_Rect){ 0, 7, 1, 2 };
    REQUIRE(expect_update_result(static_texture, &rect, pixels, 2, 0,
                                 "bottom-clipped update (SDL core clip)"));
    rect = (SDL_Rect){ -1, 0, 2, 1 };
    REQUIRE(expect_update_result(static_texture, &rect, pixels, 4, 0,
                                 "negative-x clipped update (SDL core clip)"));
    rect = (SDL_Rect){ 0, 0, 8, 1 };
    REQUIRE(expect_update_result(static_texture, &rect, pixels, 14, -1,
                                 "short pitch (backend rejection)"));
    REQUIRE(expect_update_result(static_texture, &rect, NULL, 16, -1,
                                 "null pixels (SDL core rejection)"));
    rect = (SDL_Rect){ 0, 0, 0, 1 };
    REQUIRE(expect_update_result(static_texture, &rect, pixels, 2, 0,
                                 "zero-width update (SDL core no-op)"));
    rect = (SDL_Rect){ 0, 0, 1, 0 };
    REQUIRE(expect_update_result(static_texture, &rect, pixels, 2, 0,
                                 "zero-height update (SDL core no-op)"));

    rect = (SDL_Rect){ 0, 0, 0, 1 };
    REQUIRE(expect_lock_failure(streaming_texture, &rect,
                                "zero-width lock (backend rejection)"));
    rect = (SDL_Rect){ 0, 0, 1, 0 };
    REQUIRE(expect_lock_failure(streaming_texture, &rect,
                                "zero-height lock (backend rejection)"));
    rect = (SDL_Rect){ 8, 0, 1, 1 };
    REQUIRE(expect_lock_failure(streaming_texture, &rect,
                                "outside lock (backend rejection)"));
    rect = (SDL_Rect){ 7, 0, 2, 1 };
    REQUIRE(expect_lock_failure(streaming_texture, &rect,
                                "right-extended lock (backend rejection)"));
    rect = (SDL_Rect){ 0, 7, 1, 2 };
    REQUIRE(expect_lock_failure(streaming_texture, &rect,
                                "bottom-extended lock (backend rejection)"));
    rect = (SDL_Rect){ -1, 0, 1, 1 };
    REQUIRE(expect_lock_failure(streaming_texture, &rect,
                                "negative lock (backend rejection)"));

    invalid = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 1025, 8);
    ++stats.invalid_checks;
    if (invalid) {
        SDL_DestroyTexture(invalid);
        SDL_SetError("over-maximum texture unexpectedly succeeded");
        destroy_texture(&static_texture);
        destroy_texture(&streaming_texture);
        return fail("maximum dimension (SDL core rejection)");
    }
    SDL_ClearError();
    invalid = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_TARGET, 8, 8);
    ++stats.invalid_checks;
    if (invalid) {
        SDL_DestroyTexture(invalid);
        SDL_SetError("target texture unexpectedly succeeded");
        destroy_texture(&static_texture);
        destroy_texture(&streaming_texture);
        return fail("target access (backend rejection)");
    }
    SDL_ClearError();
    destroy_texture(&static_texture);
    destroy_texture(&streaming_texture);
    return 0;
}

static int reset_page(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_RenderSetViewport(renderer, NULL));
    REQUIRE(SDL_RenderSetClipRect(renderer, NULL));
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderClear(renderer));
    return 0;
}

static int fill_rect(SDL_Renderer *renderer, int x, int y, int w, int h,
                     Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Rect rect = { x, y, w, h };

    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderFillRect(renderer, &rect));
    return 0;
}

static int draw_probe(SDL_Renderer *renderer, SDL_Texture *texture,
                      int x, int y, int w, int h)
{
    SDL_Rect dst = { x, y, w, h };

    REQUIRE(fill_rect(renderer, x - 2, y - 2, w + 4, h + 4, 255, 0, 255));
    REQUIRE(SDL_RenderCopy(renderer, texture, NULL, &dst));
    return 0;
}

static int make_initialized_texture(SDL_Renderer *renderer, Uint32 format,
                                    int access, int w, int h,
                                    SDL_Texture **out)
{
    SDL_Texture *texture = create_texture(renderer, format, access, w, h);

    if (!texture) {
        return -1;
    }
    if (access == SDL_TEXTUREACCESS_STATIC) {
        if (update_pattern(texture, format, w, h, 3) < 0) {
            destroy_texture(&texture);
            return -1;
        }
    } else if (lock_pattern(texture, format, w, h) < 0) {
        destroy_texture(&texture);
        return -1;
    }
    REQUIRE(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE));
    *out = texture;
    return 0;
}

static int draw_page1(SDL_Renderer *renderer)
{
    static const int sizes[] = { 1, 7, 8, 9, 15, 16, 17, 31, 32, 33 };
    int i;

    REQUIRE(reset_page(renderer, 5, 10, 22));
    REQUIRE(fill_rect(renderer, 8, 8, 145, 8, 255, 70, 70));
    for (i = 0; i < (int)SDL_arraysize(sizes); ++i) {
        SDL_Texture *texture = NULL;
        const int col = i % 5;
        const int row = i / 5;
        const Uint32 format = (i % 3 == 0) ? SDL_PIXELFORMAT_RGB565 :
                              (i % 3 == 1) ? SDL_PIXELFORMAT_ARGB1555 :
                                             SDL_PIXELFORMAT_ARGB4444;
        const int access = (i & 1) ? SDL_TEXTUREACCESS_STREAMING :
                                     SDL_TEXTUREACCESS_STATIC;

        REQUIRE(make_initialized_texture(renderer, format, access,
                                         sizes[i], sizes[i], &texture));
        REQUIRE(draw_probe(renderer, texture, 20 + col * 124, 35 + row * 205,
                           92, 155));
        destroy_texture(&texture);
    }
    return 0;
}

static int draw_page2(SDL_Renderer *renderer)
{
    static const int widths[] = { 1, 129, 13, 17, 63, 320, 321 };
    static const int heights[] = { 129, 1, 9, 33, 65, 240, 241 };
    static const int draw_w[] = { 42, 170, 90, 90, 110, 145, 145 };
    static const int draw_h[] = { 180, 42, 82, 150, 150, 108, 108 };
    static const int draw_x[] = { 18, 78, 270, 380, 500, 165, 355 };
    static const int draw_y[] = { 35, 35, 35, 35, 35, 300, 300 };
    int i;

    REQUIRE(reset_page(renderer, 14, 7, 20));
    REQUIRE(fill_rect(renderer, 8, 8, 145, 8, 70, 255, 70));
    for (i = 0; i < (int)SDL_arraysize(widths); ++i) {
        SDL_Texture *texture = NULL;
        const Uint32 format = (i % 3 == 0) ? SDL_PIXELFORMAT_ARGB4444 :
                              (i % 3 == 1) ? SDL_PIXELFORMAT_RGB565 :
                                             SDL_PIXELFORMAT_ARGB1555;
        const int access = (i & 1) ? SDL_TEXTUREACCESS_STATIC :
                                     SDL_TEXTUREACCESS_STREAMING;

        REQUIRE(make_initialized_texture(renderer, format, access,
                                         widths[i], heights[i], &texture));
        REQUIRE(draw_probe(renderer, texture, draw_x[i], draw_y[i],
                           draw_w[i], draw_h[i]));
        destroy_texture(&texture);
    }
    return 0;
}

static int draw_page3(SDL_Renderer *renderer)
{
    SDL_Texture *static_texture = NULL;
    SDL_Texture *streaming_texture = NULL;
    SDL_Texture *ordering = NULL;
    SDL_Texture *other = NULL;
    SDL_Rect full = { 0, 0, 17, 9 };
    SDL_Rect update = { 0, 0, 17, 9 };

    REQUIRE(reset_page(renderer, 6, 16, 16));
    REQUIRE(fill_rect(renderer, 8, 8, 145, 8, 70, 180, 255));
    REQUIRE(make_initialized_texture(renderer, SDL_PIXELFORMAT_RGB565,
                                     SDL_TEXTUREACCESS_STATIC, 33, 17,
                                     &static_texture));
    REQUIRE(make_initialized_texture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                     SDL_TEXTUREACCESS_STREAMING, 31, 17,
                                     &streaming_texture));
    REQUIRE(exercise_static_edges(static_texture, SDL_PIXELFORMAT_RGB565,
                                  33, 17));
    REQUIRE(exercise_streaming_edges(streaming_texture,
                                     SDL_PIXELFORMAT_ARGB1555, 31, 17));
    REQUIRE(draw_probe(renderer, static_texture, 25, 45, 270, 140));
    REQUIRE(draw_probe(renderer, streaming_texture, 345, 45, 270, 140));

    REQUIRE(make_initialized_texture(renderer, SDL_PIXELFORMAT_RGB565,
                                     SDL_TEXTUREACCESS_STATIC, 17, 9,
                                     &ordering));
    REQUIRE(make_initialized_texture(renderer, SDL_PIXELFORMAT_RGB565,
                                     SDL_TEXTUREACCESS_STATIC, 13, 9, &other));
    REQUIRE(draw_probe(renderer, ordering, 20, 250, 125, 90));
    update = (SDL_Rect){ 0, 0, 17, 9 };
    REQUIRE(update_solid(ordering, SDL_PIXELFORMAT_RGB565, &update, 3,
                         20, 220, 70, 255));
    REQUIRE(draw_probe(renderer, ordering, 165, 250, 125, 90));
    REQUIRE(draw_probe(renderer, other, 310, 250, 125, 90));
    update = (SDL_Rect){ 5, 2, 4, 4 };
    REQUIRE(update_solid(ordering, SDL_PIXELFORMAT_RGB565, &update, 1,
                         255, 40, 40, 255));
    REQUIRE(draw_probe(renderer, other, 455, 250, 125, 90));
    REQUIRE(draw_probe(renderer, ordering, 165, 365, 125, 90));
    REQUIRE(SDL_SetTextureScaleMode(ordering, SDL_ScaleModeNearest));
    REQUIRE(SDL_RenderCopy(renderer, ordering, &full,
                           &(SDL_Rect){ 315, 365, 125, 90 }));
    REQUIRE(SDL_SetTextureScaleMode(ordering, SDL_ScaleModeLinear));
    REQUIRE(SDL_RenderCopy(renderer, ordering, &full,
                           &(SDL_Rect){ 465, 365, 125, 90 }));
    destroy_texture(&static_texture);
    destroy_texture(&streaming_texture);
    destroy_texture(&ordering);
    destroy_texture(&other);
    return 0;
}

static int draw_alpha_checker(SDL_Renderer *renderer, int x, int y, int w, int h)
{
    int row;
    int col;
    const int cell = 12;

    for (row = 0; row < h; row += cell) {
        for (col = 0; col < w; col += cell) {
            const Uint8 shade = ((row / cell + col / cell) & 1) ? 45 : 125;

            REQUIRE(fill_rect(renderer, x + col, y + row,
                              SDL_min(cell, w - col), SDL_min(cell, h - row),
                              shade, shade, shade));
        }
    }
    return 0;
}

static int draw_page4(SDL_Renderer *renderer)
{
    static const Uint32 formats[] = {
        SDL_PIXELFORMAT_RGB565,
        SDL_PIXELFORMAT_ARGB1555,
        SDL_PIXELFORMAT_ARGB4444
    };
    int i;

    REQUIRE(reset_page(renderer, 18, 10, 5));
    REQUIRE(fill_rect(renderer, 8, 8, 145, 8, 255, 220, 70));
    for (i = 0; i < 3; ++i) {
        SDL_Texture *nearest = NULL;
        SDL_Texture *linear = NULL;
        const int x = 15 + i * 210;

        REQUIRE(make_initialized_texture(renderer, formats[i],
                                         SDL_TEXTUREACCESS_STATIC, 17, 9,
                                         &nearest));
        REQUIRE(make_initialized_texture(renderer, formats[i],
                                         SDL_TEXTUREACCESS_STREAMING, 17, 9,
                                         &linear));
        REQUIRE(SDL_SetTextureScaleMode(nearest, SDL_ScaleModeNearest));
        REQUIRE(SDL_SetTextureScaleMode(linear, SDL_ScaleModeLinear));
        if (formats[i] != SDL_PIXELFORMAT_RGB565) {
            REQUIRE(SDL_SetTextureBlendMode(nearest, SDL_BLENDMODE_BLEND));
            REQUIRE(SDL_SetTextureBlendMode(linear, SDL_BLENDMODE_BLEND));
            REQUIRE(draw_alpha_checker(renderer, x, 35, 190, 400));
        }
        REQUIRE(draw_probe(renderer, nearest, x + 5, 45, 180, 165));
        REQUIRE(draw_probe(renderer, linear, x + 5, 255, 180, 165));
        destroy_texture(&nearest);
        destroy_texture(&linear);
    }
    return 0;
}

static int present_page(SDL_Renderer *renderer, int page)
{
    SDL_RenderPresent(renderer);
    ++stats.pages;
    printf("PAGE %d/%d presented\n", page + 1, PAGE_COUNT);
#ifdef PVR_TEXTURE_MATRIX_HARDWARE_SCREENSHOT
    /* Allow the PVR presentation buffers to advance to the visible page. */
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
    if (vid_screen_shot(hardware_paths[page]) < 0) {
        ++stats.capture_failures;
        printf("CAPTURE FAIL page %d: %s\n", page + 1, hardware_paths[page]);
    } else {
        printf("CAPTURE OK page %d: %s\n", page + 1, hardware_paths[page]);
    }
    SDL_Delay(500);
#else
    SDL_Delay(PAGE_HOLD_MS);
#endif
    return 0;
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

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_RendererInfo info;
    int driver;
    int result = 1;

    (void)argc;
    (void)argv;
    SDL_zero(stats);
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fail("SDL_Init");
        goto cleanup;
    }
    driver = find_pvr_driver();
    if (driver < 0) {
        fail("find Dreamcast PVR driver");
        goto cleanup;
    }
    window = SDL_CreateWindow("PVR texture matrix", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        fail("SDL_CreateWindow");
        goto cleanup;
    }
    renderer = SDL_CreateRenderer(window, driver,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fail("SDL_CreateRenderer");
        goto cleanup;
    }
    if (SDL_GetRendererInfo(renderer, &info) < 0 ||
        info.max_texture_width != 1024 || info.max_texture_height != 1024 ||
        info.num_texture_formats != 3) {
        fail("renderer info contract");
        goto cleanup;
    }
    if (run_dimension_matrix(renderer) < 0 ||
        run_invalid_api_checks(renderer) < 0) {
        goto cleanup;
    }
    if (draw_page1(renderer) < 0 || present_page(renderer, 0) < 0 ||
        draw_page2(renderer) < 0 || present_page(renderer, 1) < 0 ||
        draw_page3(renderer) < 0 || present_page(renderer, 2) < 0 ||
        draw_page4(renderer) < 0 || present_page(renderer, 3) < 0) {
        goto cleanup;
    }
    if (stats.live_textures != 0 || stats.pages != PAGE_COUNT ||
        stats.capture_failures != 0 || stats.failures != 0) {
        SDL_SetError("final counts live=%d pages=%d captures=%d failures=%d",
                     stats.live_textures, stats.pages, stats.capture_failures,
                     stats.failures);
        fail("final accounting");
        goto cleanup;
    }
    printf("PASS: cases=%d creates=%d updates=%d locks=%d invalid=%d pages=%d "
           "formats=3 access=2\n",
           stats.cases, stats.creates, stats.updates, stats.locks,
           stats.invalid_checks, stats.pages);
    result = 0;

cleanup:
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    if (result != 0) {
        printf("FAIL SUMMARY: cases=%d creates=%d updates=%d locks=%d "
               "invalid=%d pages=%d live=%d failures=%d captures=%d\n",
               stats.cases, stats.creates, stats.updates, stats.locks,
               stats.invalid_checks, stats.pages, stats.live_textures,
               stats.failures, stats.capture_failures);
    }
    return result;
}
