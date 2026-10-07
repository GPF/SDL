#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

/* POT vs NPOT target, both written after a screen scene has executed.
 * Panel 1: POT 64x64, expected blue + green rect at target-local (8,8,20,20).
 * Panel 2: NPOT 80x64, expected magenta + yellow rect at target-local (8,8,20,20). */

static bool color(SDL_Renderer *r, float red, float green, float blue)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    return SDL_SetRenderDrawColorFloat(r, red, green, blue, 1.0f);
}

static bool rect(SDL_Renderer *r, float x, float y, float w, float h)
{
    const SDL_FRect area = { x, y, w, h };
    return SDL_RenderFillRect(r, &area);
}

static bool panel(SDL_Renderer *r, SDL_Texture *t, float x, float y)
{
    float w, h;
    if (!SDL_GetTextureSize(t, &w, &h)) {
        SDL_Log("DIAG size FAIL %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    if (!SDL_RenderTexture(r, t, NULL, &(SDL_FRect){ x, y, w, h })) {
        SDL_Log("DIAG panel FAIL %s", SDL_GetError());
        return false;
    }
    return true;
}

int main(void)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *pot;
#ifndef DIAG_UNTEXTURED
    SDL_Texture *src;
#endif

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("pvr_dt_diag", 640, 480, 0);
    renderer = SDL_CreateRenderer(window, "Dreamcast PVR");
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Log("renderer=%s", SDL_GetRendererName(renderer));

    pot = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                            SDL_TEXTUREACCESS_TARGET, 64, 64);
    if (!pot) {
        SDL_Log("DIAG SETUP FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

#ifndef DIAG_UNTEXTURED
    {
        Uint16 px[16 * 16];
        for (int i = 0; i < 256; ++i) {
            px[i] = (((i % 16) / 4 + (i / 16) / 4) & 1) ? 0xF800 : 0x07E0; /* red/green checker */
        }
        src = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 16, 16);
        if (!src || !SDL_UpdateTexture(src, NULL, px, 32)) {
            SDL_Log("DIAG SRC FAIL %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        SDL_SetTextureScaleMode(src, SDL_SCALEMODE_NEAREST);
    }
#endif

    for (int frame = 0; frame < 3; ++frame) {
        color(renderer, 0.0f, 0.0f, 0.0f);
        SDL_RenderClear(renderer);
        color(renderer, 1.0f, 1.0f, 1.0f);
        rect(renderer, 32.0f, 32.0f, 64.0f, 64.0f);
        if (!SDL_RenderPresent(renderer)) {
            SDL_Log("DIAG WARMUP FAIL %s", SDL_GetError());
            break;
        }
    }

    /* Flushed screen scene (presented) before the first target scene. */
    color(renderer, 0.0f, 0.0f, 0.0f);
    SDL_RenderClear(renderer);
    color(renderer, 0.0f, 1.0f, 1.0f);
    rect(renderer, 200.0f, 200.0f, 16.0f, 16.0f);
    if (!SDL_RenderPresent(renderer)) {
        SDL_Log("DIAG FLUSH FAIL %s", SDL_GetError());
    }

    /* One target scene, committed by switching back to the screen. */
    if (!SDL_SetRenderTarget(renderer, pot)) {
        SDL_Log("DIAG SWITCH POT FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    color(renderer, 0.0f, 0.0f, 1.0f);
    SDL_RenderClear(renderer);
    /* Textured draw inside the RTT scene: expect a red/green checker at (8,8,48x48). */
#ifndef DIAG_UNTEXTURED
    if (!SDL_RenderTexture(renderer, src, NULL, &(SDL_FRect){ 8.0f, 8.0f, 48.0f, 48.0f })) {
        SDL_Log("DIAG TEXTURED DRAW FAIL %s", SDL_GetError());
    }
#else
    color(renderer, 0.0f, 1.0f, 0.0f);
    rect(renderer, 8.0f, 8.0f, 20.0f, 20.0f);
#endif
    if (!SDL_SetRenderTarget(renderer, NULL)) {
        SDL_Log("DIAG RELEASE FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Final scene: red clear + white marker identify the frame; the pot panel
     * is under test. */
    color(renderer, 1.0f, 0.0f, 0.0f);
    SDL_RenderClear(renderer);
    color(renderer, 1.0f, 1.0f, 1.0f);
    rect(renderer, 300.0f, 300.0f, 32.0f, 32.0f);
    panel(renderer, pot, 16.0f, 16.0f);
    if (!SDL_RenderPresent(renderer)) {
        SDL_Log("DIAG PRESENT FAIL %s", SDL_GetError());
    }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (!pvr_phase0_capture("/pc/pvr_dt_diag_textured.ppm")) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "capture FAIL %s", SDL_GetError());
    }
#endif

    SDL_Log("DIAG pot panel at (16,16) 64x64; white marker at (300,300); rename capture per run");
    SDL_Quit();
    return 0;
}
