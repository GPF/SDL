#include <kos.h>
#include <SDL.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define HOLD_MS 12000
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int checks;
static int failures;

typedef struct DtCase
{
    const char *path;
    Uint32 format;
    const char *name;
} DtCase;

static const DtCase cases[] = {
    { "/rd/rgb565.dt", SDL_PIXELFORMAT_RGB565, "RGB565 twiddled" },
    { "/rd/argb1555.dt", SDL_PIXELFORMAT_ARGB1555, "ARGB1555 twiddled" },
    { "/rd/argb4444.dt", SDL_PIXELFORMAT_ARGB4444, "ARGB4444 twiddled" },
    { "/rd/vq_full.dt", SDL_PIXELFORMAT_ARGB1555, "ARGB1555 VQ full" },
    { "/rd/vq_small.dt", SDL_PIXELFORMAT_ARGB1555, "ARGB1555 VQ small" },
    { "/rd/vq_mip.dt", SDL_PIXELFORMAT_ARGB4444, "ARGB4444 VQ mip" }
};

static void check_result(SDL_bool condition, const char *message)
{
    ++checks;
    if (condition) {
        printf("CHECK PASS: %s\n", message);
    } else {
        ++failures;
        printf("CHECK FAIL: %s (SDL: %s)\n", message, SDL_GetError());
    }
}

static int find_renderer(const char *name)
{
    SDL_RendererInfo info;
    int i;

    for (i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 && info.name &&
            SDL_strcmp(info.name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static SDL_Texture *load_dt(SDL_Renderer *renderer, const char *path)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");

    if (!rw) {
        return NULL;
    }
    return SDL_LoadDreamcastTexture_RW(renderer, rw, 1);
}

static int read_file(const char *path, Uint8 **bytes, size_t *size)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    Sint64 length;

    if (!rw) {
        return -1;
    }
    length = SDL_RWsize(rw);
    if (length <= 0 || (Uint64)length > (Uint64)SDL_SIZE_MAX) {
        SDL_RWclose(rw);
        return SDL_SetError("Invalid test asset size");
    }
    *bytes = (Uint8 *)SDL_malloc((size_t)length);
    if (!*bytes) {
        SDL_RWclose(rw);
        return SDL_OutOfMemory();
    }
    if (SDL_RWread(rw, *bytes, 1, (size_t)length) != (size_t)length) {
        SDL_free(*bytes);
        *bytes = NULL;
        SDL_RWclose(rw);
        return SDL_SetError("Could not read test asset");
    }
    SDL_RWclose(rw);
    *size = (size_t)length;
    return 0;
}

static Uint32 read_le32(const Uint8 *bytes)
{
    return (Uint32)bytes[0] | ((Uint32)bytes[1] << 8) |
           ((Uint32)bytes[2] << 16) | ((Uint32)bytes[3] << 24);
}

static void write_le32(Uint8 *bytes, Uint32 value)
{
    bytes[0] = (Uint8)value;
    bytes[1] = (Uint8)(value >> 8);
    bytes[2] = (Uint8)(value >> 16);
    bytes[3] = (Uint8)(value >> 24);
}

static void expect_memory_rejection(SDL_Renderer *renderer, const Uint8 *bytes,
                                    size_t size, const char *message)
{
    SDL_RWops *rw;
    SDL_Texture *texture;

    SDL_ClearError();
    rw = SDL_RWFromConstMem(bytes, (int)size);
    if (!rw) {
        check_result(SDL_FALSE, message);
        return;
    }
    texture = SDL_LoadDreamcastTexture_RW(renderer, rw, 1);
    check_result(texture == NULL && SDL_GetError()[0] != '\0', message);
    if (texture) {
        SDL_DestroyTexture(texture);
    }
}

static void run_rejection_checks(SDL_Renderer *renderer)
{
    Uint8 *original = NULL;
    Uint8 *mutated = NULL;
    size_t size = 0;
    Uint32 value;

    if (read_file(cases[1].path, &original, &size) < 0) {
        check_result(SDL_FALSE, "read malformed-test source asset");
        return;
    }
    mutated = (Uint8 *)SDL_malloc(size);
    if (!mutated) {
        SDL_free(original);
        check_result(SDL_FALSE, "allocate malformed-test buffer");
        return;
    }

    SDL_memcpy(mutated, original, size);
    mutated[0] = 'X';
    expect_memory_rejection(renderer, mutated, size, "reject bad DcTx magic");

    SDL_memcpy(mutated, original, size);
    mutated[8] = 1;
    expect_memory_rejection(renderer, mutated, size, "reject unsupported DcTx version");

    SDL_memcpy(mutated, original, size);
    value = read_le32(mutated + 16);
    value = (value & ~(7u << 27)) | (3u << 27);
    write_le32(mutated + 16, value);
    expect_memory_rejection(renderer, mutated, size, "reject unsupported YUV .dt format");

    SDL_memcpy(mutated, original, size);
    value = read_le32(mutated + 16) | (1u << 25);
    write_le32(mutated + 16, value);
    expect_memory_rejection(renderer, mutated, size, "reject strided .dt layout");

    SDL_memcpy(mutated, original, size);
    value = read_le32(mutated + 4) - 32u;
    write_le32(mutated + 4, value);
    expect_memory_rejection(renderer, mutated, size, "reject mismatched .dt payload size");

    expect_memory_rejection(renderer, original, size - 1,
                            "reject truncated .dt payload");

    SDL_free(mutated);
    SDL_free(original);
}

static void run_recovery_checks(SDL_Renderer *renderer, SDL_bool native)
{
    SDL_Texture *warmup;
    size_t baseline;
    int i;

    /* Prime the allocator before recording the exact-cycle baseline. KOS's
       first small allocation may extend its internal arena; that retained
       arena is allocator state, not a live texture allocation. */
    warmup = load_dt(renderer, cases[4].path);
    if (!warmup) {
        check_result(SDL_FALSE, "warm up repeated .dt load");
        return;
    }
    SDL_DestroyTexture(warmup);
    SDL_RenderPresent(renderer);
    baseline = native ? (size_t)pvr_mem_available() : 0;
    if (native) {
        printf("VRAM recovery baseline=%lu\n", (unsigned long)baseline);
    }

    for (i = 0; i < 20; ++i) {
        SDL_Texture *texture = load_dt(renderer, cases[4].path);

        if (!texture) {
            check_result(SDL_FALSE, "repeated .dt load");
            return;
        }
        SDL_DestroyTexture(texture);
        SDL_RenderPresent(renderer);
        if (native && (size_t)pvr_mem_available() != baseline) {
            printf("VRAM recovery mismatch iteration=%d baseline=%lu actual=%lu\n",
                   i, (unsigned long)baseline,
                   (unsigned long)pvr_mem_available());
            check_result(SDL_FALSE, "native .dt VRAM baseline recovery");
            return;
        }
    }
    check_result(SDL_TRUE, native ? "20 native loads restore exact VRAM baseline"
                                  : "20 OpenGL loads and destroys complete");
}

static int draw_scene(SDL_Renderer *renderer, SDL_Texture **textures)
{
    static const SDL_Rect panels[] = {
        { 24, 48, 128, 128 }, { 184, 48, 128, 128 },
        { 344, 48, 128, 128 }, { 24, 232, 128, 128 },
        { 184, 232, 128, 128 }, { 344, 232, 128, 128 }
    };
    static const SDL_Color markers[] = {
        { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 128, 255, 255 },
        { 255, 128, 0, 255 }, { 255, 0, 255, 255 }, { 255, 255, 0, 255 }
    };
    SDL_Rect rect;
    int i;
    int frame;

    for (i = 0; i < ARRAY_LEN(cases); ++i) {
        if (SDL_SetTextureBlendMode(textures[i],
                                    i == 0 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND) < 0) {
            return -1;
        }
    }
    if (SDL_SetTextureColorMod(textures[1], 160, 255, 160) < 0 ||
        SDL_SetTextureAlphaMod(textures[2], 210) < 0 ||
        SDL_SetTextureScaleMode(textures[2], SDL_ScaleModeLinear) < 0 ||
        SDL_SetTextureScaleMode(textures[5], SDL_ScaleModeLinear) < 0) {
        return -1;
    }

    for (frame = 0; frame < 3; ++frame) {
        if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) < 0 ||
            SDL_SetRenderDrawColor(renderer, 10, 10, 20, 255) < 0 ||
            SDL_RenderClear(renderer) < 0) {
            return -1;
        }
        for (i = 0; i < ARRAY_LEN(cases); ++i) {
            rect = panels[i];
            rect.x -= 4;
            rect.y -= 4;
            rect.w += 8;
            rect.h += 8;
            if (SDL_SetRenderDrawColor(renderer, 32, 32, 48, 255) < 0 ||
                SDL_RenderFillRect(renderer, &rect) < 0) {
                return -1;
            }
            rect.x = panels[i].x;
            rect.y = panels[i].y - 12;
            rect.w = panels[i].w;
            rect.h = 8;
            if (SDL_SetRenderDrawColor(renderer, markers[i].r, markers[i].g,
                                       markers[i].b, 255) < 0 ||
                SDL_RenderFillRect(renderer, &rect) < 0) {
                return -1;
            }
        }

        if (SDL_RenderCopy(renderer, textures[0], NULL, &panels[0]) < 0 ||
            SDL_RenderCopy(renderer, textures[1], NULL, &panels[1]) < 0 ||
            SDL_RenderCopy(renderer, textures[2], NULL, &panels[2]) < 0 ||
            SDL_RenderCopyEx(renderer, textures[3], NULL, &panels[3], 0.0,
                             NULL, SDL_FLIP_HORIZONTAL) < 0 ||
            SDL_RenderCopyEx(renderer, textures[4], NULL, &panels[4], 90.0,
                             NULL, SDL_FLIP_NONE) < 0) {
            return -1;
        }

        rect = panels[5];
        if (SDL_RenderCopy(renderer, textures[5], NULL, &rect) < 0) {
            return -1;
        }

        rect.x = 0;
        rect.y = OUTPUT_H - 24;
        rect.w = OUTPUT_W;
        rect.h = 24;
        if (SDL_SetRenderDrawColor(renderer, 0, 200, 0, 255) < 0 ||
            SDL_RenderFillRect(renderer, &rect) < 0) {
            return -1;
        }
        if (SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) < 0) {
            return -1;
        }
        rect.w = 8;
        rect.h = 8;
        rect.x = 0;
        rect.y = 0;
        if (SDL_RenderFillRect(renderer, &rect) < 0) return -1;
        rect.x = OUTPUT_W - 8;
        if (SDL_RenderFillRect(renderer, &rect) < 0) return -1;
        rect.y = OUTPUT_H - 8;
        if (SDL_RenderFillRect(renderer, &rect) < 0) return -1;
        rect.x = 0;
        if (SDL_RenderFillRect(renderer, &rect) < 0) return -1;

        SDL_RenderPresent(renderer);
    }
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *textures[ARRAY_LEN(cases)] = { NULL };
    SDL_RendererInfo info;
    SDL_Rect update_rect = { 0, 0, 1, 1 };
    Uint16 update_pixel = 0;
    const char *renderer_name;
    const char *capture_path;
    Uint32 window_flags;
    SDL_bool native;
    int driver;
    int i;

    (void)argc;
    (void)argv;
#if defined(PVR_DT_TEST_NATIVE)
    native = SDL_TRUE;
    renderer_name = "Dreamcast PVR";
    capture_path = "/pc/pvr_dt_texture-phase6_1-native.ppm";
    window_flags = 0;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
#else
    native = SDL_FALSE;
    renderer_name = "opengl";
    capture_path = "/pc/pvr_dt_texture-phase6_1-opengl.ppm";
    window_flags = SDL_WINDOW_OPENGL;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
#endif

    printf("PVR .DT TEXTURE PHASE 6.1\n");
    printf("  renderer: %s\n", renderer_name);
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    driver = find_renderer(renderer_name);
    if (driver < 0) {
        printf("FAIL: renderer not found: %s\n", renderer_name);
        ++failures;
        goto done;
    }
    window = SDL_CreateWindow("PVR .dt Phase 6.1", 0, 0,
                              OUTPUT_W, OUTPUT_H, window_flags);
    if (!window) {
        printf("FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        ++failures;
        goto done;
    }
    renderer = SDL_CreateRenderer(window, driver, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("FAIL: SDL_CreateRenderer: %s\n", SDL_GetError());
        ++failures;
        goto done;
    }
    if (SDL_GetRendererInfo(renderer, &info) < 0) {
        ++failures;
        goto done;
    }
    check_result(info.name && SDL_strcmp(info.name, renderer_name) == 0,
                 "selected requested renderer");

    run_rejection_checks(renderer);
    run_recovery_checks(renderer, native);

    for (i = 0; i < ARRAY_LEN(cases); ++i) {
        Uint32 format = SDL_PIXELFORMAT_UNKNOWN;
        int access = -1;
        int w = 0;
        int h = 0;

        textures[i] = load_dt(renderer, cases[i].path);
        if (!textures[i]) {
            printf("FAIL: %s: %s\n", cases[i].name, SDL_GetError());
            ++failures;
            goto done;
        }
        if (SDL_QueryTexture(textures[i], &format, &access, &w, &h) < 0) {
            ++failures;
            goto done;
        }
        check_result(format == cases[i].format &&
                     access == SDL_TEXTUREACCESS_STATIC && w == 64 && h == 64,
                     cases[i].name);
    }

    SDL_ClearError();
    check_result(SDL_UpdateTexture(textures[0], &update_rect, &update_pixel,
                                   sizeof(update_pixel)) < 0 &&
                 SDL_strstr(SDL_GetError(), "immutable") != NULL,
                 "loader-created .dt textures are immutable");

    if (draw_scene(renderer, textures) < 0) {
        printf("FAIL: draw scene: %s\n", SDL_GetError());
        ++failures;
        goto done;
    }

#ifdef PVR_DT_HARDWARE_SCREENSHOT
    if (vid_screen_shot(capture_path) < 0) {
        printf("CAPTURE FAIL: %s\n", capture_path);
        ++failures;
    } else {
        printf("CAPTURE OK: %s\n", capture_path);
    }
#else
    (void)capture_path;
#endif

done:
    printf("SUMMARY checks=%d failures=%d PASS=%s\n",
           checks, failures, failures ? "NO" : "YES");
    if (renderer && failures == 0) {
        printf("Holding final frame for %d ms...\n", HOLD_MS);
        SDL_Delay(HOLD_MS);
    }
    for (i = 0; i < ARRAY_LEN(cases); ++i) {
        SDL_DestroyTexture(textures[i]);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Returning normally to dc-load-ip\n");
    return failures ? 1 : 0;
}
