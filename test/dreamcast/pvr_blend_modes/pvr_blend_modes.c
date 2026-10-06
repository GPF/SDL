#include <SDL3/SDL.h>
#include "../pvr_phase0_screenshot.h"

static const Uint8 alpha_values[] = { 0, 128, 255 };

static bool set_draw(SDL_Renderer *renderer, SDL_BlendMode mode,
                     float r, float g, float b, Uint8 alpha)
{
    return SDL_SetRenderDrawBlendMode(renderer, mode) &&
           SDL_SetRenderDrawColor(renderer, (Uint8)(r * 255.0f),
                                 (Uint8)(g * 255.0f), (Uint8)(b * 255.0f), alpha);
}

static bool draw_rect(SDL_Renderer *renderer, float x, float y, float w, float h)
{
    const SDL_FRect rect = { x, y, w, h };
    return SDL_RenderFillRect(renderer, &rect);
}

static bool draw_untextured_row(SDL_Renderer *renderer, SDL_BlendMode mode,
                                float x, float y, float step)
{
    for (int i = 0; i < 3; ++i) {
        if (!set_draw(renderer, mode, 1.0f, 0.0f, 0.0f, alpha_values[i]) ||
            !draw_rect(renderer, x + (float)i * step, y, 48.0f, 40.0f)) {
            return false;
        }
    }
    return true;
}

static bool draw_textured_row(SDL_Renderer *renderer, SDL_Texture *texture,
                             SDL_BlendMode mode, float x, float y, float step)
{
    if (!SDL_SetTextureBlendMode(texture, mode) ||
        !SDL_SetTextureColorMod(texture, 255, 128, 64)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const SDL_FRect dst = { x + (float)i * step, y, 48.0f, 40.0f };
        if (!SDL_SetTextureAlphaMod(texture, alpha_values[i]) ||
            !SDL_RenderTexture(renderer, texture, NULL, &dst)) {
            return false;
        }
    }
    return SDL_SetTextureAlphaMod(texture, 255) &&
           SDL_SetTextureColorMod(texture, 255, 255, 255);
}

static bool draw_panel(SDL_Renderer *renderer, SDL_Texture *texture,
                       float origin_x, float origin_y, float step)
{
    return SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) &&
           SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255) &&
           SDL_RenderClear(renderer) &&
           draw_untextured_row(renderer, SDL_BLENDMODE_NONE,
                               origin_x, origin_y, step) &&
           draw_untextured_row(renderer, SDL_BLENDMODE_BLEND,
                               origin_x, origin_y + 52.0f, step) &&
           draw_textured_row(renderer, texture, SDL_BLENDMODE_NONE,
                             origin_x, origin_y + 104.0f, step) &&
           draw_textured_row(renderer, texture, SDL_BLENDMODE_BLEND,
                             origin_x, origin_y + 156.0f, step);
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_Texture *target = NULL;
    Uint16 white[16 * 16];
    bool ok = false;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_blend init=FAIL %s", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("PVR blend modes", 640, 480, 0);
    renderer = window ? SDL_CreateRenderer(window, "Dreamcast PVR") : NULL;
    if (!renderer || SDL_strcmp(SDL_GetRendererName(renderer), "Dreamcast PVR") != 0) {
        SDL_Log("pvr_blend renderer=FAIL %s", SDL_GetError());
        goto done;
    }
    SDL_Log("pvr_blend renderer=%s", SDL_GetRendererName(renderer));

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 16, 16);
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_TARGET, 256, 224);
    if (!texture || !target) {
        SDL_Log("pvr_blend create=FAIL %s", SDL_GetError());
        goto done;
    }
    for (size_t i = 0; i < SDL_arraysize(white); ++i) {
        white[i] = 0xFFFF;
    }
    if (!SDL_UpdateTexture(texture, NULL, white, 16 * (int)sizeof(Uint16)) ||
        !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) {
        SDL_Log("pvr_blend texture_setup=FAIL %s", SDL_GetError());
        goto done;
    }
    if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) ||
        !SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) ||
        !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
        SDL_Log("pvr_blend mode_or_target_acceptance=FAIL %s", SDL_GetError());
        goto done;
    }
    SDL_Log("pvr_blend setters=PASS NONE,BLEND for draw and texture");

    {
        SDL_BlendMode mode = SDL_BLENDMODE_INVALID;
        if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) ||
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD) ||
            !SDL_GetRenderDrawBlendMode(renderer, &mode) || mode != SDL_BLENDMODE_NONE) {
            SDL_Log("pvr_blend draw_ADD_rejection=FAIL %s", SDL_GetError());
            goto done;
        }
        SDL_ClearError();
        mode = SDL_BLENDMODE_INVALID;
        if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) ||
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD) ||
            !SDL_GetTextureBlendMode(texture, &mode) || mode != SDL_BLENDMODE_NONE) {
            SDL_Log("pvr_blend texture_ADD_rejection=FAIL %s", SDL_GetError());
            goto done;
        }
        SDL_ClearError();
    }
    SDL_Log("pvr_blend ADD_rejection=PASS state_preserved");

    if (!SDL_SetRenderTarget(renderer, target)) {
        SDL_Log("pvr_blend target_acceptance=FAIL %s", SDL_GetError());
        goto done;
    }

    for (int frame = 0; frame < 3; ++frame) {
        if (!SDL_SetRenderTarget(renderer, NULL) ||
            !draw_panel(renderer, texture, 20.0f, 24.0f, 72.0f) ||
            !SDL_SetRenderTarget(renderer, target) ||
            !draw_panel(renderer, texture, 8.0f, 8.0f, 80.0f) ||
            !SDL_SetRenderTarget(renderer, NULL) ||
            !SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureAlphaMod(target, 255) ||
            !SDL_SetTextureColorMod(target, 255, 255, 255) ||
            !SDL_RenderTexture(renderer, target, NULL,
                               &(SDL_FRect){ 336.0f, 8.0f, 256.0f, 224.0f }) ||
            !SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_blend frame=%d FAIL %s", frame, SDL_GetError());
            goto done;
        }
    }
    SDL_Log("pvr_blend screen_target_paths=PASS frames=3");
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
    if (!pvr_phase0_capture("/pc/pvr_blend_modes.ppm")) {
        SDL_Log("pvr_blend screenshot=FAIL %s", SDL_GetError());
        goto done;
    }
#endif
    ok = true;

done:
    SDL_Log("pvr_blend result=%s", ok ? "PASS (inspect screen/target panels)" : "FAIL");
    if (target) SDL_DestroyTexture(target);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
