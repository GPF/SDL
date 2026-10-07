#include <SDL3/SDL.h>
#include <kos/fs.h>
#include "../pvr_phase0_screenshot.h"

#define TEX_SIZE 256
#define PANEL 128
#define FILE_MAX 131104

typedef struct DtCase
{
    const char *path;
    SDL_PixelFormat format;
    SDL_BlendMode blend;
    const char *name;
} DtCase;

/* Direct 16-bit .dt formats only. RGB565 has no texture alpha, so it must
 * render through NONE; the two alpha formats must render through BLEND. */
static const DtCase cases[] = {
    { "/rd/rgb565.dt",   SDL_PIXELFORMAT_RGB565,   SDL_BLENDMODE_NONE,  "RGB565"   },
    { "/rd/argb1555.dt", SDL_PIXELFORMAT_ARGB1555, SDL_BLENDMODE_BLEND, "ARGB1555" },
    { "/rd/argb4444.dt", SDL_PIXELFORMAT_ARGB4444, SDL_BLENDMODE_BLEND, "ARGB4444" }
};

static const float direct_row_y = 16.0f;
static const float target_row_y = 176.0f;

static int panel_x(int index)
{
    return 16 + index * 224;
}

static SDL_Texture *load_dt(SDL_Renderer *renderer, const char *path)
{
    static unsigned char file[FILE_MAX];
    SDL_IOStream *stream;
    int handle;
    size_t got;

    handle = fs_open(path, O_RDONLY);
    if (handle < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Could not open %s", path);
        return NULL;
    }
    got = fs_read(handle, file, sizeof(file));
    fs_close(handle);
    if (got != sizeof(file)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s read %u of %u bytes",
                     path, (unsigned)got, (unsigned)sizeof(file));
        return NULL;
    }

    stream = SDL_IOFromConstMem(file, got);
    if (!stream) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Could not create IO stream for %s", path);
        return NULL;
    }
    return SDL_LoadDreamcastTexture_IO(renderer, stream, true);
}

/* A direct .dt whose declared payload does not match its dimensions must still
 * be rejected. This keeps the gate honest without changing the parser. */
static bool malformed_rejected(SDL_Renderer *renderer)
{
    unsigned char bad[64] = { 0 };
    SDL_IOStream *stream;
    SDL_Texture *texture;

    bad[0] = 'D';
    bad[1] = 'c';
    bad[2] = 'T';
    bad[3] = 'x';
    bad[4] = 32;      /* header_size */
    bad[5] = 64;      /* chunk_size: too small for 256x256 RGB565 */
    bad[12] = 8;      /* width 256 */
    bad[13] = 8;      /* height 256 */
    bad[16] = 1;      /* RGB565 */

    stream = SDL_IOFromConstMem(bad, sizeof(bad));
    if (!stream) {
        return false;
    }
    texture = SDL_LoadDreamcastTexture_IO(renderer, stream, true);
    if (texture) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "MALFORMED-REJECTION FAIL: accepted a short payload");
        SDL_DestroyTexture(texture);
        return false;
    }
    SDL_Log("MALFORMED-REJECTION PASS: %s", SDL_GetError());
    return true;
}

static bool draw_panel(SDL_Renderer *renderer, SDL_Texture *texture, int index, float y)
{
    const SDL_FRect dst = { (float)panel_x(index), y, (float)PANEL, (float)PANEL };

    return SDL_RenderTexture(renderer, texture, NULL, &dst);
}

int main(void)
{
    static SDL_Texture *texture[3];
    static SDL_Texture *target[3];
    SDL_Window *window;
    SDL_Renderer *renderer;
    int failures = 0;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    /* No render driver or video-mode hint is set here: SDL_DC_DREAMCAST_PVR_VIDEO
     * is the default Dreamcast video mode, so this gate exercises the default
     * selection path. */

    window = SDL_CreateWindow("SDL3 Dreamcast direct 16-bit .dt gate", 640, 480, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (SDL_strcmp(SDL_GetRendererName(renderer), "Dreamcast PVR") != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "renderer=%s (expected Dreamcast PVR)",
                     SDL_GetRendererName(renderer));
        SDL_Quit();
        return 1;
    }
    SDL_Log("renderer=%s", SDL_GetRendererName(renderer));

    for (int i = 0; i < 3; ++i) {
        texture[i] = load_dt(renderer, cases[i].path);
        if (!texture[i]) {
            SDL_Log("%s LOAD FAIL", cases[i].name);
            failures++;
            continue;
        }
        if (texture[i]->format != cases[i].format ||
            texture[i]->w != TEX_SIZE || texture[i]->h != TEX_SIZE) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s metadata mismatch: format=%s size=%dx%d",
                         cases[i].name, SDL_GetPixelFormatName(texture[i]->format),
                         texture[i]->w, texture[i]->h);
            failures++;
        }
        if (!SDL_SetTextureBlendMode(texture[i], cases[i].blend)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s blend mode rejected: %s",
                         cases[i].name, SDL_GetError());
            failures++;
        }
        SDL_Log("%s LOAD PASS format=%s size=%dx%d blend=%s",
                cases[i].name, SDL_GetPixelFormatName(texture[i]->format),
                texture[i]->w, texture[i]->h,
                cases[i].blend == SDL_BLENDMODE_NONE ? "NONE" : "BLEND");
    }

    if (!malformed_rejected(renderer)) {
        failures++;
    }

    for (int i = 0; i < 3; ++i) {
        if (!texture[i]) {
            target[i] = NULL;
            continue;
        }
        target[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                      SDL_TEXTUREACCESS_TARGET, PANEL, PANEL);
        if (!target[i]) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s target create FAIL: %s",
                         cases[i].name, SDL_GetError());
            failures++;
        }
    }

    /* Sample each .dt through the RGB565 render-target path. A target scene is
     * committed by the next target switch, not by present. */
    for (int i = 0; i < 3; ++i) {
        const SDL_FRect dst = { 0.0f, 0.0f, (float)PANEL, (float)PANEL };

        if (!target[i]) {
            continue;
        }
        if (!SDL_SetRenderTarget(renderer, target[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s target switch FAIL: %s",
                         cases[i].name, SDL_GetError());
            failures++;
            continue;
        }
        if (!SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) ||
            !SDL_RenderClear(renderer)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s target clear FAIL: %s",
                         cases[i].name, SDL_GetError());
            failures++;
            continue;
        }
        if (!SDL_RenderTexture(renderer, texture[i], NULL, &dst)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s target draw FAIL: %s",
                         cases[i].name, SDL_GetError());
            failures++;
            continue;
        }
        /* API success only; target pixels are checked from the capture. */
        SDL_Log("%s TARGET SUBMIT OK (pixels unverified)", cases[i].name);
    }

    if (!SDL_SetRenderTarget(renderer, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "target release FAIL: %s", SDL_GetError());
        failures++;
    }

    /* Top row: direct .dt to the screen over blue. Bottom row: the same .dt
     * sampled back through its RGB565 target over black. */
    if (!SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255) ||
        !SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) ||
        !SDL_RenderClear(renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "screen clear FAIL: %s", SDL_GetError());
        failures++;
    }

    for (int i = 0; i < 3; ++i) {
        if (texture[i]) {
            if (!draw_panel(renderer, texture[i], i, direct_row_y)) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s direct draw FAIL: %s",
                             cases[i].name, SDL_GetError());
                failures++;
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (target[i]) {
            if (!draw_panel(renderer, target[i], i, target_row_y)) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s target draw FAIL: %s",
                             cases[i].name, SDL_GetError());
                failures++;
            }
        }
    }

    if (!SDL_RenderPresent(renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "present FAIL: %s", SDL_GetError());
        failures++;
    }

#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (!pvr_phase0_capture("/pc/pvr_dt_direct.ppm")) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "capture FAIL: %s", SDL_GetError());
        failures++;
    }
#endif

    for (int i = 0; i < 3; ++i) {
        if (texture[i]) {
            SDL_DestroyTexture(texture[i]);
        }
        if (target[i]) {
            SDL_DestroyTexture(target[i]);
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    SDL_Log("DIRECT 16-BIT .DT GATE failures=%d", failures);
    return failures == 0 ? 0 : 1;
}
