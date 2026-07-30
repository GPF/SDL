/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Native Dreamcast PVR renderer, Phase 0 feasibility implementation.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_DREAMCAST_PVR

#include "SDL_render_dreamcast_pvr.h"

#if defined(DC_PVR_TEXTURE_FAILURE_INJECTION)
#include "SDL_hints.h"

#define DC_PVR_TEST_ALLOC_STAGE_HINT "SDL_DC_PVR_TEST_ALLOC_STAGE"
#define DC_PVR_TEST_ALLOC_COUNTDOWN_HINT "SDL_DC_PVR_TEST_ALLOC_COUNTDOWN"

typedef enum DC_PVR_TestAllocStage
{
    DC_PVR_TEST_ALLOC_NONE,
    DC_PVR_TEST_ALLOC_DRIVERDATA,
    DC_PVR_TEST_ALLOC_STAGING,
    DC_PVR_TEST_ALLOC_VRAM
} DC_PVR_TestAllocStage;

static DC_PVR_TestAllocStage DC_PVR_test_alloc_stage;
static int DC_PVR_test_alloc_countdown = -1;
static int DC_PVR_test_alloc_remaining;

static DC_PVR_TestAllocStage DC_PVR_TestParseAllocStage(const char *value)
{
    if (value && SDL_strcmp(value, "driverdata") == 0) {
        return DC_PVR_TEST_ALLOC_DRIVERDATA;
    }
    if (value && SDL_strcmp(value, "staging") == 0) {
        return DC_PVR_TEST_ALLOC_STAGING;
    }
    if (value && SDL_strcmp(value, "vram") == 0) {
        return DC_PVR_TEST_ALLOC_VRAM;
    }
    return DC_PVR_TEST_ALLOC_NONE;
}

static SDL_bool DC_PVR_TestShouldFailAllocation(DC_PVR_TestAllocStage stage)
{
    const char *stage_hint = SDL_GetHint(DC_PVR_TEST_ALLOC_STAGE_HINT);
    const char *countdown_hint = SDL_GetHint(DC_PVR_TEST_ALLOC_COUNTDOWN_HINT);
    const DC_PVR_TestAllocStage selected = DC_PVR_TestParseAllocStage(stage_hint);
    const int countdown = countdown_hint ? SDL_atoi(countdown_hint) : 0;

    if (selected != DC_PVR_test_alloc_stage ||
        countdown != DC_PVR_test_alloc_countdown) {
        DC_PVR_test_alloc_stage = selected;
        DC_PVR_test_alloc_countdown = countdown;
        DC_PVR_test_alloc_remaining = countdown > 0 ? countdown : 0;
    }
    if (selected != stage || DC_PVR_test_alloc_remaining <= 0) {
        return SDL_FALSE;
    }
    --DC_PVR_test_alloc_remaining;
    return DC_PVR_test_alloc_remaining == 0 ? SDL_TRUE : SDL_FALSE;
}
#endif

static int DC_PVR_NextPowerOfTwo(int value)
{
    int result = 8;

    while (result < value) {
        result <<= 1;
    }
    return result;
}

static int DC_PVR_PrepareVRAMReplacement(DC_PVR_TextureData *data,
                                         pvr_ptr_t *replacement,
                                         DC_PVR_DeferredVRAM **deferred)
{
    DC_PVR_DeferredVRAM *entry;
    pvr_ptr_t vram;

    entry = (DC_PVR_DeferredVRAM *)SDL_malloc(sizeof(*entry));
    if (!entry) {
        return SDL_OutOfMemory();
    }
    vram = pvr_mem_malloc(data->allocation_size);
    if (!vram) {
        SDL_free(entry);
        return SDL_SetError("Could not allocate replacement Dreamcast PVR texture VRAM");
    }
    entry->next = NULL;
    entry->vram = data->vram;
    *replacement = vram;
    *deferred = entry;
    return 0;
}

static void DC_PVR_CommitVRAMReplacement(DC_PVR_RenderData *renderdata,
                                         DC_PVR_TextureData *data,
                                         pvr_ptr_t replacement,
                                         DC_PVR_DeferredVRAM *deferred)
{
    data->vram = replacement;
    data->sample_vram = replacement;
    data->referenced_in_scene = SDL_FALSE;
    deferred->next = renderdata->deferred_vram;
    renderdata->deferred_vram = deferred;
    renderdata->header_valid = SDL_FALSE;
    renderdata->header_texture = NULL;
}

int DC_PVR_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_TextureData *data;
    int pvr_format;
#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
    pvr_ptr_t vram_next;
#else
    (void)renderer;
#endif

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
#if !defined(DC_PVR_RENDER_TARGETS_ENABLED)
        return SDL_SetError("Dreamcast PVR does not support render targets");
#else
        if (!renderer->SetRenderTarget) {
            return SDL_SetError("Dreamcast PVR render targets are not enabled");
        }
        if (texture->format != SDL_PIXELFORMAT_RGB565) {
            return SDL_SetError("Dreamcast PVR target textures require RGB565 format");
        }
        pvr_format = PVR_TXRFMT_RGB565;
#endif
    } else {
        if (texture->access != SDL_TEXTUREACCESS_STATIC &&
            texture->access != SDL_TEXTUREACCESS_STREAMING) {
            return SDL_SetError("Dreamcast PVR texture access mode is not supported");
        }
        switch (texture->format) {
        case SDL_PIXELFORMAT_RGB565:
            pvr_format = PVR_TXRFMT_RGB565;
            break;
        case SDL_PIXELFORMAT_ARGB1555:
            pvr_format = PVR_TXRFMT_ARGB1555;
            break;
        case SDL_PIXELFORMAT_ARGB4444:
            pvr_format = PVR_TXRFMT_ARGB4444;
            break;
        default:
            return SDL_SetError("Dreamcast PVR texture format is not supported");
        }
    }

#if defined(DC_PVR_TEXTURE_FAILURE_INJECTION)
    if (DC_PVR_TestShouldFailAllocation(DC_PVR_TEST_ALLOC_DRIVERDATA)) {
        return SDL_SetError("Injected Dreamcast PVR texture driver-data allocation failure");
    }
#endif
    data = (DC_PVR_TextureData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }

    data->sdl_format = texture->format;
    data->pvr_format = pvr_format | PVR_TXRFMT_NONTWIDDLED;
    data->access = texture->access;
    data->logical_w = texture->w;
    data->logical_h = texture->h;
    data->backing_w = DC_PVR_NextPowerOfTwo(texture->w);
    data->backing_h = DC_PVR_NextPowerOfTwo(texture->h);
    data->cpu_pitch = data->backing_w * 2;
    data->vram_pitch = data->cpu_pitch;
    data->allocation_size = (size_t)data->vram_pitch * data->backing_h;
    data->filter = PVR_FILTER_NEAREST;
    data->dirty = SDL_TRUE;
#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
    data->rtt_render_w = texture->w;
    data->rtt_render_h = texture->h;
    data->rtt_stride_px = data->backing_w;
    data->current_valid = SDL_FALSE;
#endif
    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        data->pixels = NULL;
#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
        vram_next = pvr_mem_malloc(data->allocation_size);
        if (!vram_next) {
            if (data->pixels) {
                SDL_free(data->pixels);
            }
            SDL_free(data);
            return SDL_SetError("Could not allocate target VRAM (next)");
        }
        data->vram_next = vram_next;
#endif
    } else {
#if defined(DC_PVR_TEXTURE_FAILURE_INJECTION)
        if (DC_PVR_TestShouldFailAllocation(DC_PVR_TEST_ALLOC_STAGING)) {
            SDL_free(data);
            return SDL_SetError("Injected Dreamcast PVR texture staging allocation failure");
        }
#endif
        data->pixels = SDL_calloc(1, data->allocation_size);
        if (!data->pixels) {
            SDL_free(data);
            return SDL_SetError("Could not allocate Dreamcast PVR texture staging memory");
        }
    }
#if defined(DC_PVR_TEXTURE_FAILURE_INJECTION)
    if (DC_PVR_TestShouldFailAllocation(DC_PVR_TEST_ALLOC_VRAM)) {
#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
        if (data->vram_next) {
            pvr_mem_free(data->vram_next);
            data->vram_next = NULL;
        }
#endif
        if (data->pixels) {
            SDL_free(data->pixels);
        }
        SDL_free(data);
        return SDL_SetError("Injected Dreamcast PVR texture VRAM allocation failure");
    }
#endif
    data->vram = pvr_mem_malloc(data->allocation_size);
    if (!data->vram) {
#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
        if (data->vram_next) {
            pvr_mem_free(data->vram_next);
            data->vram_next = NULL;
        }
#endif
        if (data->pixels) {
            SDL_free(data->pixels);
        }
        SDL_free(data);
        return SDL_SetError("Could not allocate Dreamcast PVR texture VRAM");
    }
    data->sample_vram = data->vram;

    texture->driverdata = data;
    texture->pitch = data->cpu_pitch;
    return 0;
}

int DC_PVR_CreateDreamcastTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                                  const SDL_DreamcastTextureData *dt)
{
    DC_PVR_TextureData *data;
    pvr_ptr_t vram;

    (void)renderer;
    if (!texture || !dt || !dt->payload || dt->payload_size == 0) {
        return SDL_SetError("Dreamcast .dt texture has invalid native PVR data");
    }
    if (texture->access != SDL_TEXTUREACCESS_STATIC ||
        texture->format != dt->sdl_format || texture->w != (int)dt->width ||
        texture->h != (int)dt->height) {
        return SDL_SetError("Dreamcast .dt texture has inconsistent SDL state");
    }

#if defined(DC_PVR_TEXTURE_FAILURE_INJECTION)
    if (DC_PVR_TestShouldFailAllocation(DC_PVR_TEST_ALLOC_DRIVERDATA)) {
        return SDL_SetError("Injected Dreamcast PVR texture driver-data allocation failure");
    }
#endif
    data = (DC_PVR_TextureData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }

#if defined(DC_PVR_TEXTURE_FAILURE_INJECTION)
    if (DC_PVR_TestShouldFailAllocation(DC_PVR_TEST_ALLOC_VRAM)) {
        SDL_free(data);
        return SDL_SetError("Injected Dreamcast PVR texture VRAM allocation failure");
    }
#endif
    vram = pvr_mem_malloc(dt->payload_size);
    if (!vram) {
        SDL_free(data);
        return SDL_SetError("Could not allocate Dreamcast .dt texture VRAM");
    }

    data->vram = vram;
    data->sample_vram = vram;
    if (dt->compressed) {
        data->sample_vram = (pvr_ptr_t)((Uint8 *)vram - 2048u +
                                       (size_t)dt->codebook_size * 8u);
    }
    data->allocation_size = dt->payload_size;
    data->sdl_format = dt->sdl_format;
    data->pvr_format = (int)(dt->pvr_type & 0xfc000000u);
    data->logical_w = dt->width;
    data->logical_h = dt->height;
    data->backing_w = dt->pvr_width;
    data->backing_h = dt->pvr_height;
    data->access = SDL_TEXTUREACCESS_STATIC;
    data->filter = PVR_FILTER_NEAREST;
    data->uploaded = SDL_TRUE;
    data->preformatted = SDL_TRUE;

    pvr_txr_load(dt->payload, data->vram, data->allocation_size);
    texture->driverdata = data;
    texture->pitch = 0;

    SDL_Log("Dreamcast PVR: loaded .dt texture %ux%u compressed=%d twiddled=%d mipmapped=%d codebook=%u payload=%lu",
            (unsigned)dt->width, (unsigned)dt->height, (int)dt->compressed,
            (int)dt->twiddled, (int)dt->mipmapped,
            (unsigned)dt->codebook_size,
            (unsigned long)dt->payload_size);
    return 0;
}

int DC_PVR_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                                const SDL_Rect *rect, const void *pixels, int pitch)
{
    DC_PVR_RenderData *renderdata = (DC_PVR_RenderData *)renderer->driverdata;
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->driverdata;
    DC_PVR_DeferredVRAM *deferred = NULL;
    pvr_ptr_t upload_vram = data ? data->vram : NULL;
    const Uint8 *source = (const Uint8 *)pixels;
    Uint8 *destination;
    size_t row_bytes;
    int row;

#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
    if (data && data->access == SDL_TEXTUREACCESS_TARGET) {
        return SDL_SetError("Dreamcast PVR target textures cannot be CPU-updated");
    }
#endif
    if (data && data->preformatted) {
        return SDL_SetError("Dreamcast .dt textures are immutable");
    }
    if (!data || !data->pixels || !data->vram) {
        return SDL_SetError("Dreamcast PVR texture update has invalid backend state");
    }
    if (!pixels || !rect) {
        return SDL_InvalidParamError("texture update");
    }
    if (data->locked) {
        return SDL_SetError("Dreamcast PVR texture cannot be updated while locked");
    }
    if (rect->x < 0 || rect->y < 0 || rect->w <= 0 || rect->h <= 0 ||
        rect->x > data->logical_w || rect->y > data->logical_h ||
        rect->w > data->logical_w - rect->x ||
        rect->h > data->logical_h - rect->y) {
        return SDL_SetError("Dreamcast PVR texture update is outside the logical texture");
    }

    row_bytes = (size_t)rect->w * 2;
    if (pitch < (int)row_bytes) {
        return SDL_SetError("Dreamcast PVR texture update pitch is too small");
    }

    /* RunCommandQueue may already have submitted headers that reference the
     * current allocation. Preserve that immutable backing until presentation
     * completes, while later draws use a full-backing replacement. */
    if (data->referenced_in_scene &&
        DC_PVR_PrepareVRAMReplacement(data, &upload_vram, &deferred) < 0) {
        return -1;
    }

    destination = (Uint8 *)data->pixels + (size_t)rect->y * data->cpu_pitch + (size_t)rect->x * 2;
    for (row = 0; row < rect->h; ++row) {
        SDL_memcpy(destination, source, row_bytes);
        destination += data->cpu_pitch;
        source += pitch;
    }

    data->dirty = SDL_TRUE;
    /* Upload the complete POT backing store synchronously. */
    pvr_txr_load(data->pixels, upload_vram, data->allocation_size);
    if (deferred) {
        DC_PVR_CommitVRAMReplacement(renderdata, data, upload_vram, deferred);
    }
    data->uploaded = SDL_TRUE;
    data->dirty = SDL_FALSE;
    return 0;
}

int DC_PVR_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                       const SDL_Rect *rect, void **pixels, int *pitch)
{
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->driverdata;

    (void)renderer;
    if (!data || !data->pixels || !data->vram) {
        return SDL_SetError("Dreamcast PVR texture lock has invalid backend state");
    }
    if (!rect || !pixels || !pitch) {
        return SDL_InvalidParamError("texture lock");
    }
    if (data->access != SDL_TEXTUREACCESS_STREAMING) {
        return SDL_SetError("Dreamcast PVR texture lock requires streaming access");
    }
    if (data->locked) {
        return SDL_SetError("Dreamcast PVR texture is already locked");
    }
    if (rect->x < 0 || rect->y < 0 || rect->w <= 0 || rect->h <= 0 ||
        rect->x > data->logical_w || rect->y > data->logical_h ||
        rect->w > data->logical_w - rect->x ||
        rect->h > data->logical_h - rect->y) {
        return SDL_SetError("Dreamcast PVR texture lock is outside the logical texture");
    }

    if (data->referenced_in_scene &&
        DC_PVR_PrepareVRAMReplacement(data, &data->lock_replacement_vram,
                                      &data->lock_deferred_vram) < 0) {
        return -1;
    }

    data->locked = SDL_TRUE;
    data->locked_rect = *rect;
    *pixels = (Uint8 *)data->pixels + (size_t)rect->y * data->cpu_pitch +
              (size_t)rect->x * 2;
    *pitch = data->cpu_pitch;
    return 0;
}

void DC_PVR_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_RenderData *renderdata = (DC_PVR_RenderData *)renderer->driverdata;
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->driverdata;
    pvr_ptr_t upload_vram;

    if (!data || !data->locked) {
        return;
    }

    data->dirty = SDL_TRUE;
    if (data->referenced_in_scene && !data->lock_replacement_vram) {
        if (DC_PVR_PrepareVRAMReplacement(data,
                                          &data->lock_replacement_vram,
                                          &data->lock_deferred_vram) < 0) {
            data->locked = SDL_FALSE;
            SDL_zero(data->locked_rect);
            return;
        }
    }
    upload_vram = data->lock_replacement_vram ? data->lock_replacement_vram
                                               : data->vram;
    pvr_txr_load(data->pixels, upload_vram, data->allocation_size);
    if (data->lock_deferred_vram) {
        DC_PVR_CommitVRAMReplacement(renderdata, data, upload_vram,
                                     data->lock_deferred_vram);
        data->lock_replacement_vram = NULL;
        data->lock_deferred_vram = NULL;
    }
    data->uploaded = SDL_TRUE;
    data->dirty = SDL_FALSE;
    data->locked = SDL_FALSE;
    SDL_zero(data->locked_rect);
}

void DC_PVR_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->driverdata;

    (void)renderer;
    /* data is guaranteed non-NULL here: driverdata is set in CreateTexture
     * and NULLed in DestroyTexture, which no-ops this callback. The prior
     * SDL_SetError branch on !data was unreachable and has been removed. */
    if (scaleMode == SDL_ScaleModeNearest) {
        data->filter = PVR_FILTER_NEAREST;
    } else if (scaleMode == SDL_ScaleModeLinear) {
        data->filter = PVR_FILTER_BILINEAR;
    } else {
        SDL_SetError("Dreamcast PVR supports only nearest and linear texture filtering");
    }
}

void DC_PVR_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_RenderData *renderdata = (DC_PVR_RenderData *)renderer->driverdata;
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->driverdata;

    if (!data) {
        return;
    }
    if (data->lock_replacement_vram) {
        pvr_mem_free(data->lock_replacement_vram);
        data->lock_replacement_vram = NULL;
    }
    SDL_free(data->lock_deferred_vram);
    data->lock_deferred_vram = NULL;
    data->locked = SDL_FALSE;
    SDL_zero(data->locked_rect);
#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
    if (data->vram_next) {
        pvr_mem_free(data->vram_next);
        data->vram_next = NULL;
    }
#endif
    if (renderdata->header_texture == texture) {
        renderdata->header_valid = SDL_FALSE;
        renderdata->header_texture = NULL;
    }
    if (renderdata->scene_active) {
        data->next_deferred = renderdata->deferred_textures;
        renderdata->deferred_textures = data;
    } else {
        DC_PVR_FreeTextureData(data, SDL_TRUE);
    }
    texture->driverdata = NULL;
}


void DC_PVR_FreeTextureData(DC_PVR_TextureData *data, SDL_bool free_vram)
{
    if (!data) {
        return;
    }
    if (free_vram && data->vram) {
        pvr_mem_free(data->vram);
    }
    if (free_vram && data->lock_replacement_vram) {
        pvr_mem_free(data->lock_replacement_vram);
    }
    SDL_free(data->lock_deferred_vram);
    SDL_free(data->pixels);
    SDL_free(data);
}

#endif /* SDL_VIDEO_RENDER_DREAMCAST_PVR */
