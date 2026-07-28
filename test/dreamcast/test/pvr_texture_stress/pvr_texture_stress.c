#include <SDL.h>
#include <kos.h>

#include <stdio.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define STATIC_CYCLES 50
#define STREAMING_CYCLES 20
#define MIXED_TEXTURES 12
#define EXHAUST_W 256
#define EXHAUST_H 256
#define EXHAUST_BACKING_W 256
#define EXHAUST_BACKING_H 256
#define EXHAUST_BYTES ((size_t)EXHAUST_BACKING_W * EXHAUST_BACKING_H * 2)
#define EXHAUST_CAP 64
#define FINAL_HOLD_MS 12000

#define PRIVATE_ALLOC_STAGE_HINT "SDL_DC_PVR_TEST_ALLOC_STAGE"
#define PRIVATE_ALLOC_COUNTDOWN_HINT "SDL_DC_PVR_TEST_ALLOC_COUNTDOWN"

typedef struct StressStats
{
    int texture_creates;
    int expected_create_failures;
    int texture_destroys;
    int locks;
    int unlocks;
    int destroyed_while_locked;
    int unlocks_without_lock;
    int updates;
    int draws;
    int exhaustion_allocations;
    int exhaustion_freed;
    int recovered_allocations;
    int renderer_creates;
    int renderer_destroys;
} StressStats;

static StressStats stats;
static Uint16 pattern_pixels[128 * 128];

static int fail_message(const char *operation)
{
    printf("FAIL: %s: %s\n", operation, SDL_GetError());
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return fail_message(#call); } while (0)

static Uint16 format_pixel(Uint32 format, int x, int y, int seed)
{
    const Uint8 r = (Uint8)((x * 19 + seed * 37) & 0xff);
    const Uint8 g = (Uint8)((y * 23 + seed * 53) & 0xff);
    const Uint8 b = (Uint8)(((x + y) * 13 + seed * 29) & 0xff);

    if (format == SDL_PIXELFORMAT_RGB565) {
        return (Uint16)(((Uint16)(r >> 3) << 11) |
                        ((Uint16)(g >> 2) << 5) | (b >> 3));
    }
    if (format == SDL_PIXELFORMAT_ARGB1555) {
        return (Uint16)(0x8000u | ((Uint16)(r >> 3) << 10) |
                        ((Uint16)(g >> 3) << 5) | (b >> 3));
    }
    return (Uint16)(0xf000u | ((Uint16)(r >> 4) << 8) |
                    ((Uint16)(g >> 4) << 4) | (b >> 4));
}

static void make_pattern(Uint32 format, int w, int h, int seed)
{
    int x;
    int y;

    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            pattern_pixels[y * w + x] = format_pixel(format, x, y, seed);
        }
    }
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

static SDL_Renderer *create_renderer(SDL_Window *window, int driver)
{
    SDL_Renderer *renderer = SDL_CreateRenderer(window, driver,
                                                SDL_RENDERER_ACCELERATED);

    if (renderer) {
        ++stats.renderer_creates;
    }
    return renderer;
}

static void destroy_renderer(SDL_Renderer **renderer, int associated_textures)
{
    if (*renderer) {
        stats.texture_destroys += associated_textures;
        SDL_DestroyRenderer(*renderer);
        *renderer = NULL;
        ++stats.renderer_destroys;
    }
}

static SDL_Texture *create_texture(SDL_Renderer *renderer, Uint32 format,
                                   int access, int w, int h)
{
    SDL_Texture *texture = SDL_CreateTexture(renderer, format, access, w, h);

    if (texture) {
        ++stats.texture_creates;
    }
    return texture;
}

static void destroy_texture(SDL_Texture **texture)
{
    if (*texture) {
        SDL_DestroyTexture(*texture);
        *texture = NULL;
        ++stats.texture_destroys;
    }
}

static int update_full(SDL_Texture *texture, Uint32 format, int w, int h,
                       int seed)
{
    make_pattern(format, w, h, seed);
    if (SDL_UpdateTexture(texture, NULL, pattern_pixels,
                          w * (int)sizeof(Uint16)) < 0) {
        return fail_message("SDL_UpdateTexture full");
    }
    ++stats.updates;
    return 0;
}

static int update_rect(SDL_Texture *texture, Uint32 format,
                       const SDL_Rect *rect, int seed)
{
    int x;
    int y;

    for (y = 0; y < rect->h; ++y) {
        for (x = 0; x < rect->w; ++x) {
            pattern_pixels[y * rect->w + x] =
                format_pixel(format, rect->x + x, rect->y + y, seed);
        }
    }
    if (SDL_UpdateTexture(texture, rect, pattern_pixels,
                          rect->w * (int)sizeof(Uint16)) < 0) {
        return fail_message("SDL_UpdateTexture partial");
    }
    ++stats.updates;
    return 0;
}

static int draw_texture(SDL_Renderer *renderer, SDL_Texture *texture,
                        int x, int y, int w, int h)
{
    SDL_Rect destination = { x, y, w, h };

    if (SDL_RenderCopy(renderer, texture, NULL, &destination) < 0) {
        return fail_message("SDL_RenderCopy");
    }
    ++stats.draws;
    return 0;
}

static int present_checked(SDL_Renderer *renderer)
{
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (*SDL_GetError()) {
        return fail_message("SDL_RenderPresent");
    }
    return 0;
}

static int phase_static(SDL_Renderer *renderer)
{
    static const Uint32 formats[6] = {
        SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_ARGB1555,
        SDL_PIXELFORMAT_ARGB4444, SDL_PIXELFORMAT_RGB565,
        SDL_PIXELFORMAT_ARGB1555, SDL_PIXELFORMAT_ARGB4444
    };
    static const int widths[6] = { 8, 17, 31, 64, 96, 128 };
    static const int heights[6] = { 8, 9, 23, 48, 65, 96 };
    int i;

    printf("Phase static: %d cycles\n", STATIC_CYCLES);
    for (i = 0; i < STATIC_CYCLES; ++i) {
        const int variant = i % 6;
        const int w = widths[variant];
        const int h = heights[variant];
        const Uint32 format = formats[variant];
        SDL_Rect edge = { w - 1, h - 1, 1, 1 };
        SDL_Rect interior = { w > 4 ? 2 : 0, h > 4 ? 2 : 0,
                              w > 4 ? 3 : 1, h > 4 ? 3 : 1 };
        SDL_Texture *texture = create_texture(renderer, format,
                                              SDL_TEXTUREACCESS_STATIC, w, h);

        if (!texture) {
            return fail_message("static SDL_CreateTexture");
        }
        if (update_full(texture, format, w, h, i) < 0 ||
            update_rect(texture, format, &edge, i + 101) < 0 ||
            update_rect(texture, format, &interior, i + 203) < 0 ||
            draw_texture(renderer, texture, (i * 37) % 560,
                         (i * 29) % 400, 64, 48) < 0) {
            destroy_texture(&texture);
            return -1;
        }
        destroy_texture(&texture);
        if ((i % 5) == 4 && present_checked(renderer) < 0) {
            return -1;
        }
    }
    if (present_checked(renderer) < 0) {
        return -1;
    }
    printf("Phase static complete: creates=50 updates=150 draws=50 destroys=50\n");
    return 0;
}

static void write_locked_pixels(void *pixels, int pitch, const SDL_Rect *rect,
                                Uint32 format, int seed)
{
    Uint8 *row = (Uint8 *)pixels;
    int x;
    int y;

    for (y = 0; y < rect->h; ++y) {
        Uint16 *destination = (Uint16 *)row;

        for (x = 0; x < rect->w; ++x) {
            destination[x] = format_pixel(format, rect->x + x,
                                          rect->y + y, seed);
        }
        row += pitch;
    }
}

static int lock_write(SDL_Texture *texture, const SDL_Rect *rect,
                      Uint32 format, int seed)
{
    void *pixels;
    int pitch;

    if (SDL_LockTexture(texture, rect, &pixels, &pitch) < 0) {
        return fail_message("SDL_LockTexture");
    }
    ++stats.locks;
    write_locked_pixels(pixels, pitch, rect, format, seed);
    return 0;
}

static void unlock_texture(SDL_Texture *texture)
{
    SDL_UnlockTexture(texture);
    ++stats.unlocks;
}

static int phase_streaming(SDL_Renderer *renderer)
{
    int i;

    printf("Phase streaming: %d cycles\n", STREAMING_CYCLES);
    for (i = 0; i < STREAMING_CYCLES; ++i) {
        const Uint32 format = (i % 3 == 0) ? SDL_PIXELFORMAT_RGB565 :
                              (i % 3 == 1) ? SDL_PIXELFORMAT_ARGB1555 :
                                             SDL_PIXELFORMAT_ARGB4444;
        const int w = 33 + (i % 3);
        const int h = 19 + (i % 4);
        SDL_Rect full = { 0, 0, w, h };
        SDL_Rect partial;
        SDL_Rect probe = { 1, 1, 2, 2 };
        SDL_Texture *texture = create_texture(renderer, format,
                                              SDL_TEXTUREACCESS_STREAMING, w, h);
        void *nested_pixels;
        int nested_pitch;

        if (!texture) {
            return fail_message("streaming SDL_CreateTexture");
        }
        SDL_UnlockTexture(texture);
        ++stats.unlocks_without_lock;
        if (lock_write(texture, &full, format, i + 301) < 0) {
            destroy_texture(&texture);
            return -1;
        }
        unlock_texture(texture);

        if ((i % 3) == 0) {
            partial = (SDL_Rect){ w - 1, 0, 1, h };
        } else if ((i % 3) == 1) {
            partial = (SDL_Rect){ 0, h - 1, w, 1 };
        } else {
            partial = (SDL_Rect){ 2, 3, 5, 4 };
        }
        if (lock_write(texture, &partial, format, i + 401) < 0) {
            destroy_texture(&texture);
            return -1;
        }
        unlock_texture(texture);

        if (lock_write(texture, &probe, format, i + 501) < 0) {
            destroy_texture(&texture);
            return -1;
        }
        SDL_ClearError();
        if (SDL_LockTexture(texture, &probe, &nested_pixels, &nested_pitch) == 0) {
            printf("FAIL: nested streaming lock was accepted at cycle %d\n", i);
            unlock_texture(texture);
            destroy_texture(&texture);
            return -1;
        }
        if (!SDL_strstr(SDL_GetError(), "already locked")) {
            printf("FAIL: nested lock returned unexpected error: %s\n", SDL_GetError());
            unlock_texture(texture);
            destroy_texture(&texture);
            return -1;
        }
        unlock_texture(texture);

        if (lock_write(texture, &probe, format, i + 601) < 0) {
            destroy_texture(&texture);
            return -1;
        }
        unlock_texture(texture);
        if (draw_texture(renderer, texture, (i * 31) % 570,
                         (i * 23) % 420, 60, 40) < 0) {
            destroy_texture(&texture);
            return -1;
        }

        if ((i % 5) == 4) {
            SDL_Texture *replacement;

            if (lock_write(texture, &probe, format, i + 701) < 0) {
                destroy_texture(&texture);
                return -1;
            }
            ++stats.destroyed_while_locked;
            destroy_texture(&texture);
            replacement = create_texture(renderer, SDL_PIXELFORMAT_RGB565,
                                         SDL_TEXTUREACCESS_STATIC, 16, 16);
            if (!replacement ||
                update_full(replacement, SDL_PIXELFORMAT_RGB565, 16, 16,
                            i + 801) < 0 ||
                draw_texture(renderer, replacement, 8 + i * 2, 430, 32, 32) < 0) {
                destroy_texture(&replacement);
                return fail_message("post-locked-destroy replacement");
            }
            destroy_texture(&replacement);
        } else {
            destroy_texture(&texture);
        }
        if ((i % 4) == 3 && present_checked(renderer) < 0) {
            return -1;
        }
    }
    if (present_checked(renderer) < 0) {
        return -1;
    }
    printf("Phase streaming complete: cycles=20 destroyed_locked=%d "
           "unlock_without_lock=%d\n", stats.destroyed_while_locked,
           stats.unlocks_without_lock);
    return 0;
}

static int initialize_small_texture(SDL_Texture *texture, Uint32 format,
                                    int w, int h, int seed)
{
    return update_full(texture, format, w, h, seed);
}

static int phase_mixed(SDL_Window *window, int driver, SDL_Renderer **renderer)
{
    SDL_Texture *textures[MIXED_TEXTURES];
    SDL_Texture *live[5];
    int i;

    SDL_zeroa(textures);
    SDL_zeroa(live);
    printf("Phase mixed lifetime: %d simultaneous textures\n", MIXED_TEXTURES);
    for (i = 0; i < MIXED_TEXTURES; ++i) {
        const Uint32 format = (i % 3 == 0) ? SDL_PIXELFORMAT_RGB565 :
                              (i % 3 == 1) ? SDL_PIXELFORMAT_ARGB1555 :
                                             SDL_PIXELFORMAT_ARGB4444;

        textures[i] = create_texture(*renderer, format,
                                     SDL_TEXTUREACCESS_STATIC,
                                     16 + i, 12 + (i % 5));
        if (!textures[i] ||
            initialize_small_texture(textures[i], format, 16 + i,
                                     12 + (i % 5), i + 901) < 0 ||
            draw_texture(*renderer, textures[i], 10 + i * 45,
                         30 + (i % 3) * 55, 40, 40) < 0) {
            return fail_message("mixed texture setup");
        }
    }

    for (i = 0; i < 3; ++i) {
        destroy_texture(&textures[i]);
    }
    for (i = MIXED_TEXTURES - 1; i >= MIXED_TEXTURES - 3; --i) {
        destroy_texture(&textures[i]);
    }
    destroy_texture(&textures[5]);
    destroy_texture(&textures[6]);

    for (i = 0; i < MIXED_TEXTURES; ++i) {
        if (!textures[i]) {
            textures[i] = create_texture(*renderer, SDL_PIXELFORMAT_RGB565,
                                         SDL_TEXTUREACCESS_STATIC, 24, 24);
            if (!textures[i] ||
                update_full(textures[i], SDL_PIXELFORMAT_RGB565, 24, 24,
                            i + 1001) < 0 ||
                draw_texture(*renderer, textures[i], 20 + i * 40,
                             220, 36, 36) < 0) {
                return fail_message("fragmentation replacement");
            }
        }
    }
    if (present_checked(*renderer) < 0) {
        return -1;
    }
    for (i = 0; i < MIXED_TEXTURES; ++i) {
        destroy_texture(&textures[i]);
    }

    textures[0] = create_texture(*renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STATIC, 32, 32);
    if (!textures[0] ||
        update_full(textures[0], SDL_PIXELFORMAT_RGB565, 32, 32, 1101) < 0 ||
        draw_texture(*renderer, textures[0], 280, 190, 80, 80) < 0) {
        return fail_message("queued-destroy texture");
    }
    destroy_texture(&textures[0]);
    if (present_checked(*renderer) < 0) {
        return -1;
    }

    for (i = 0; i < 5; ++i) {
        live[i] = create_texture(*renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STATIC, 20 + i, 20 + i);
        if (!live[i] ||
            update_full(live[i], SDL_PIXELFORMAT_RGB565, 20 + i, 20 + i,
                        i + 1201) < 0 ||
            draw_texture(*renderer, live[i], 40 + i * 80, 330, 64, 64) < 0) {
            return fail_message("renderer-live texture");
        }
    }
    REQUIRE(SDL_RenderFlush(*renderer));
    destroy_renderer(renderer, 5);
    SDL_zeroa(live);
    *renderer = create_renderer(window, driver);
    if (!*renderer) {
        return fail_message("mixed renderer recreation");
    }
    textures[0] = create_texture(*renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!textures[0] ||
        update_full(textures[0], SDL_PIXELFORMAT_RGB565, 16, 16, 1301) < 0 ||
        draw_texture(*renderer, textures[0], 290, 210, 60, 60) < 0 ||
        present_checked(*renderer) < 0) {
        return fail_message("mixed recreated renderer texture");
    }
    destroy_texture(&textures[0]);
    printf("Phase mixed complete: renderer recreation succeeded\n");
    return 0;
}

#ifdef PVR_STRESS_EXPECT_FAILURE_INJECTION
static int expect_injected_failure(SDL_Renderer *renderer, const char *stage,
                                   const char *error_fragment, int seed)
{
    SDL_Texture *texture;

    SDL_SetHint(PRIVATE_ALLOC_COUNTDOWN_HINT, "1");
    SDL_SetHint(PRIVATE_ALLOC_STAGE_HINT, stage);
    SDL_ClearError();
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (texture) {
        SDL_DestroyTexture(texture);
        printf("FAIL: injected %s allocation was accepted\n", stage);
        return -1;
    }
    ++stats.expected_create_failures;
    if (!SDL_strstr(SDL_GetError(), error_fragment)) {
        printf("FAIL: injected %s returned unexpected error: %s\n",
               stage, SDL_GetError());
        return -1;
    }
    printf("Injected failure %s: %s\n", stage, SDL_GetError());

    SDL_SetHint(PRIVATE_ALLOC_STAGE_HINT, "none");
    texture = create_texture(renderer, SDL_PIXELFORMAT_RGB565,
                             SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!texture || update_full(texture, SDL_PIXELFORMAT_RGB565, 16, 16,
                                seed) < 0 ||
        draw_texture(renderer, texture, 20 + seed * 10, 20, 48, 48) < 0 ||
        present_checked(renderer) < 0) {
        destroy_texture(&texture);
        return fail_message("post-injection recovery texture");
    }
    destroy_texture(&texture);
    return 0;
}
#endif

static int phase_failure_injection(SDL_Renderer *renderer)
{
#ifdef PVR_STRESS_EXPECT_FAILURE_INJECTION
    printf("Phase failure injection: driverdata, staging, VRAM\n");
    if (expect_injected_failure(renderer, "driverdata", "driver-data", 1) < 0 ||
        expect_injected_failure(renderer, "staging", "staging", 2) < 0 ||
        expect_injected_failure(renderer, "vram", "VRAM", 3) < 0) {
        return -1;
    }
    SDL_SetHint(PRIVATE_ALLOC_STAGE_HINT, "none");
    printf("Phase failure injection complete: failures=3 recoveries=3\n");
#else
    (void)renderer;
    printf("Phase failure injection skipped in ordinary non-test build\n");
#endif
    return 0;
}

static int create_known_good(SDL_Renderer *renderer, SDL_Texture **texture,
                             Uint32 format, int seed)
{
    *texture = create_texture(renderer, format, SDL_TEXTUREACCESS_STATIC, 32, 32);
    if (!*texture) {
        return fail_message("known-good SDL_CreateTexture");
    }
    return update_full(*texture, format, 32, 32, seed);
}

static int phase_exhaustion(SDL_Window *window, int driver,
                            SDL_Renderer **renderer)
{
    SDL_Texture *known[3];
    SDL_Texture *exhaustion[EXHAUST_CAP];
    SDL_Texture *recovered[EXHAUST_CAP];
    int initial_count = 0;
    int recovered_count = 0;
    int freed_count = 0;
    int i;

    SDL_zeroa(known);
    SDL_zeroa(exhaustion);
    SDL_zeroa(recovered);
    printf("Phase exhaustion: %dx%d RGB565, %lu bytes per allocation\n",
           EXHAUST_W, EXHAUST_H, (unsigned long)EXHAUST_BYTES);
    if (create_known_good(*renderer, &known[0], SDL_PIXELFORMAT_RGB565, 1401) < 0 ||
        create_known_good(*renderer, &known[1], SDL_PIXELFORMAT_ARGB1555, 1402) < 0 ||
        create_known_good(*renderer, &known[2], SDL_PIXELFORMAT_ARGB4444, 1403) < 0) {
        return -1;
    }

    for (i = 0; i < EXHAUST_CAP; ++i) {
        SDL_ClearError();
        exhaustion[i] = SDL_CreateTexture(*renderer, SDL_PIXELFORMAT_RGB565,
                                          SDL_TEXTUREACCESS_STATIC,
                                          EXHAUST_W, EXHAUST_H);
        if (!exhaustion[i]) {
            break;
        }
        ++stats.texture_creates;
        ++stats.exhaustion_allocations;
        ++initial_count;
    }
    if (i == EXHAUST_CAP) {
        printf("FAIL: VRAM exhaustion safety cap %d was reached\n", EXHAUST_CAP);
        return -1;
    }
    ++stats.expected_create_failures;
    if (!SDL_strstr(SDL_GetError(), "VRAM")) {
        printf("FAIL: exhaustion was not a VRAM failure: %s\n", SDL_GetError());
        return -1;
    }
    printf("Initial exhaustion: count=%d bytes=%lu error=%s\n",
           initial_count, (unsigned long)((size_t)initial_count * EXHAUST_BYTES),
           SDL_GetError());
    if (initial_count < 2) {
        printf("FAIL: exhaustion allocated too few textures\n");
        return -1;
    }
    for (i = 0; i < 3; ++i) {
        if (draw_texture(*renderer, known[i], 40 + i * 190, 70, 160, 120) < 0) {
            return -1;
        }
    }
    if (present_checked(*renderer) < 0) {
        return -1;
    }

    for (i = 0; i < initial_count; i += 2) {
        destroy_texture(&exhaustion[i]);
        ++freed_count;
        ++stats.exhaustion_freed;
    }
    for (i = 0; i < EXHAUST_CAP; ++i) {
        SDL_ClearError();
        recovered[i] = SDL_CreateTexture(*renderer, SDL_PIXELFORMAT_RGB565,
                                         SDL_TEXTUREACCESS_STATIC,
                                         EXHAUST_W, EXHAUST_H);
        if (!recovered[i]) {
            break;
        }
        ++stats.texture_creates;
        ++stats.recovered_allocations;
        ++recovered_count;
    }
    if (i == EXHAUST_CAP) {
        printf("FAIL: recovery safety cap %d was reached\n", EXHAUST_CAP);
        return -1;
    }
    ++stats.expected_create_failures;
    if (!SDL_strstr(SDL_GetError(), "VRAM")) {
        printf("FAIL: recovery ended with a non-VRAM failure: %s\n", SDL_GetError());
        return -1;
    }
    if (recovered_count == 0) {
        printf("FAIL: no allocation recovered after freeing %d textures\n",
               freed_count);
        return -1;
    }
    printf("Recovery: freed=%d recovered=%d error=%s\n",
           freed_count, recovered_count, SDL_GetError());
    for (i = 0; i < 3; ++i) {
        if (draw_texture(*renderer, known[i], 40 + i * 190, 220, 160, 120) < 0) {
            return -1;
        }
    }
    if (present_checked(*renderer) < 0) {
        return -1;
    }

    for (i = 0; i < initial_count; ++i) {
        destroy_texture(&exhaustion[i]);
    }
    for (i = 0; i < recovered_count; ++i) {
        destroy_texture(&recovered[i]);
    }
    for (i = 0; i < 3; ++i) {
        destroy_texture(&known[i]);
    }
    known[0] = create_texture(*renderer, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STATIC, 32, 32);
    if (!known[0] || update_full(known[0], SDL_PIXELFORMAT_RGB565, 32, 32,
                                 1501) < 0 ||
        draw_texture(*renderer, known[0], 240, 180, 160, 120) < 0 ||
        present_checked(*renderer) < 0) {
        return fail_message("post-exhaustion normal texture");
    }
    destroy_texture(&known[0]);
    printf("Post-exhaustion normal texture succeeded\n");

    destroy_renderer(renderer, 0);
    *renderer = create_renderer(window, driver);
    if (!*renderer) {
        return fail_message("post-exhaustion renderer recreation");
    }
    known[0] = create_texture(*renderer, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STATIC, 32, 32);
    if (!known[0] || update_full(known[0], SDL_PIXELFORMAT_RGB565, 32, 32,
                                 1502) < 0 ||
        draw_texture(*renderer, known[0], 240, 180, 160, 120) < 0 ||
        present_checked(*renderer) < 0) {
        return fail_message("post-exhaustion recreated renderer texture");
    }
    destroy_texture(&known[0]);
    printf("Phase exhaustion complete: initial=%d freed=%d recovered=%d "
           "renderer_recreated=yes\n", initial_count, freed_count,
           recovered_count);
    return 0;
}

static int create_final_texture(SDL_Renderer *renderer, SDL_Texture **texture,
                                Uint32 format, int seed)
{
    *texture = create_texture(renderer, format, SDL_TEXTUREACCESS_STATIC, 32, 32);
    if (!*texture) {
        return fail_message("final SDL_CreateTexture");
    }
    return update_full(*texture, format, 32, 32, seed);
}

static int phase_final(SDL_Renderer *renderer)
{
    SDL_Texture *textures[3];
    SDL_Rect panel;
    int i;

    SDL_zeroa(textures);
    if (create_final_texture(renderer, &textures[0], SDL_PIXELFORMAT_RGB565,
                             1601) < 0 ||
        create_final_texture(renderer, &textures[1], SDL_PIXELFORMAT_ARGB1555,
                             1602) < 0 ||
        create_final_texture(renderer, &textures[2], SDL_PIXELFORMAT_ARGB4444,
                             1603) < 0) {
        return -1;
    }
    REQUIRE(SDL_SetRenderDrawColor(renderer, 5, 9, 18, 255));
    REQUIRE(SDL_RenderClear(renderer));
    for (i = 0; i < 3; ++i) {
        const Uint8 border_r = (i == 0) ? 255 : 30;
        const Uint8 border_g = (i == 1) ? 255 : 30;
        const Uint8 border_b = (i == 2) ? 255 : 30;

        panel = (SDL_Rect){ 35 + i * 205, 70, 170, 330 };
        REQUIRE(SDL_SetRenderDrawColor(renderer, border_r, border_g, border_b, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &panel));
        if (draw_texture(renderer, textures[i], panel.x + 8, panel.y + 8,
                         panel.w - 16, panel.h - 16) < 0) {
            return -1;
        }
    }
    if (present_checked(renderer) < 0) {
        return -1;
    }

#ifdef PVR_TEXTURE_STRESS_HARDWARE_SCREENSHOT
    /* Allow the three-buffer PVR presentation to become the visible page. */
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
    if (vid_screen_shot("/pc/pvr_texture_stress-hardware.ppm") < 0) {
        printf("FAIL: could not capture /pc/pvr_texture_stress-hardware.ppm\n");
        return -1;
    }
    printf("Captured /pc/pvr_texture_stress-hardware.ppm\n");
#else
    printf("Holding final RGB565/ARGB1555/ARGB4444 frame for %d ms\n",
           FINAL_HOLD_MS);
    SDL_Delay(FINAL_HOLD_MS);
#endif
    for (i = 0; i < 3; ++i) {
        destroy_texture(&textures[i]);
    }
    return 0;
}

static int validate_accounting(void)
{
    if (stats.texture_creates != stats.texture_destroys) {
        printf("FAIL: texture accounting is unbalanced: creates=%d destroys=%d\n",
               stats.texture_creates, stats.texture_destroys);
        return -1;
    }
    if (stats.locks != stats.unlocks + stats.destroyed_while_locked) {
        printf("FAIL: lock accounting is unbalanced: locks=%d unlocks=%d "
               "destroyed_locked=%d\n", stats.locks, stats.unlocks,
               stats.destroyed_while_locked);
        return -1;
    }
    if (stats.renderer_creates != stats.renderer_destroys + 1) {
        printf("FAIL: renderer accounting is unexpected: creates=%d destroys=%d\n",
               stats.renderer_creates, stats.renderer_destroys);
        return -1;
    }
    return 0;
}

static void print_stats(void)
{
    printf("STATS textures create=%d expected_fail=%d destroy=%d updates=%d "
           "draws=%d\n", stats.texture_creates,
           stats.expected_create_failures, stats.texture_destroys,
           stats.updates, stats.draws);
    printf("STATS locks=%d unlocks=%d destroyed_locked=%d "
           "unlock_without_lock=%d\n", stats.locks, stats.unlocks,
           stats.destroyed_while_locked, stats.unlocks_without_lock);
    printf("STATS exhaustion=%d freed=%d recovered=%d bytes_each=%lu\n",
           stats.exhaustion_allocations, stats.exhaustion_freed,
           stats.recovered_allocations, (unsigned long)EXHAUST_BYTES);
    printf("STATS renderers create=%d destroy=%d active=1\n",
           stats.renderer_creates, stats.renderer_destroys);
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    int driver;
    int result = 1;

    (void)argc;
    (void)argv;
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
                      (cont_btn_callback_t)arch_exit);
    SDL_zero(stats);
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    driver = find_pvr_driver();
    if (driver < 0) {
        printf("FAIL: Dreamcast PVR renderer is unavailable\n");
        goto done;
    }
    window = SDL_CreateWindow("PVR texture stress", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, OUTPUT_W, OUTPUT_H,
                              SDL_WINDOW_SHOWN);
    if (!window) {
        printf("FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        goto done;
    }
    renderer = create_renderer(window, driver);
    if (!renderer) {
        printf("FAIL: SDL_CreateRenderer: %s\n", SDL_GetError());
        goto done;
    }

    if (phase_static(renderer) < 0 || phase_streaming(renderer) < 0 ||
        phase_mixed(window, driver, &renderer) < 0 ||
        phase_failure_injection(renderer) < 0 ||
        phase_exhaustion(window, driver, &renderer) < 0 ||
        phase_final(renderer) < 0 || validate_accounting() < 0) {
        goto done;
    }
    print_stats();
    printf("PASS (API/runtime/visual): pvr_texture_stress completed\n");
    result = 0;

done:
    if (renderer) {
        destroy_renderer(&renderer, 0);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
