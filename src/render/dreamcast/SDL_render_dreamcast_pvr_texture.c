/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Native Dreamcast PVR renderer, Phase 0: static/streaming 16-bit textures.
  Render targets, .dt loading, palettes, strided layouts and DMA upload are
  later phases.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_DREAMCAST_PVR

#include "SDL_render_dreamcast_pvr.h"

#include <errno.h>
#include <dc/pvr/pvr_txr.h>

bool DC_PVR_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                          SDL_PropertiesID create_props)
{
    DC_PVR_TextureData *data;
    int pvr_format;

    (void)create_props;

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
#if SDL_VIDEO_RENDER_DREAMCAST_PVR_TARGETS
        if (!renderer->SetRenderTarget) {
            return SDL_SetError("Dreamcast PVR render targets are not enabled");
        }
        if (texture->format != SDL_PIXELFORMAT_RGB565) {
            return SDL_SetError("Dreamcast PVR target textures require RGB565 format");
        }
        pvr_format = PVR_TXRFMT_RGB565;
#else
        return SDL_SetError("Dreamcast PVR render targets are not enabled");
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

    data = (DC_PVR_TextureData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }

    data->sdl_format = texture->format;
    data->pvr_format = pvr_format | PVR_TXRFMT_NONTWIDDLED;
    data->access = texture->access;
    data->logical_w = texture->w;
    data->logical_h = texture->h;
    data->backing_w = SDL_powerof2(texture->w);
    data->backing_h = SDL_powerof2(texture->h);
    data->cpu_pitch = data->backing_w * 2;
    data->allocation_size = (size_t)data->cpu_pitch * data->backing_h;
    data->filter = renderer->scale_mode == SDL_SCALEMODE_LINEAR ? PVR_FILTER_BILINEAR
                                                                 : PVR_FILTER_NEAREST;
    data->uploaded = false;

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        data->vram_next = pvr_mem_malloc(data->allocation_size);
        if (!data->vram_next) {
            SDL_free(data);
            return SDL_SetError("Could not allocate Dreamcast PVR target preservation VRAM");
        }
    } else {
        /* aligned_alloc keeps the staging store 32-byte aligned for the
         * SQ/DMA path used by later phases. */
        data->pixels = aligned_alloc(32, data->allocation_size);
        if (!data->pixels) {
            SDL_free(data);
            return SDL_SetError("Could not allocate Dreamcast PVR texture staging memory");
        }
        SDL_memset(data->pixels, 0, data->allocation_size);
    }

    data->vram = pvr_mem_malloc(data->allocation_size);
    if (!data->vram) {
        if (data->vram_next) {
            pvr_mem_free(data->vram_next);
        }
        free(data->pixels);
        SDL_free(data);
        return SDL_SetError("Could not allocate Dreamcast PVR texture VRAM");
    }

    texture->internal = data;
    texture->pitch = data->cpu_pitch;
    return true;
}

bool DC_PVR_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                          const SDL_Rect *rect, const void *pixels, int pitch)
{
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->internal;
    const Uint8 *source = (const Uint8 *)pixels;
    Uint8 *destination;
    size_t row_bytes;

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        return SDL_SetError("Dreamcast PVR render targets cannot be updated from the CPU");
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

    /* A texture already referenced by an open scene has its VRAM backing
     * sampled by headers emitted for this frame. Re-uploading it mid-scene
     * would change pixels the PVR may still read, so Phase 0 rejects it
     * instead of silently approximating. */
    if (data->referenced_in_scene) {
        return SDL_SetError("Dreamcast PVR texture cannot be updated while it is referenced by an open scene");
    }

    /* Only the caller's rectangle rows are read; POT padding stays zero. */
    destination = (Uint8 *)data->pixels + (size_t)rect->y * data->cpu_pitch + (size_t)rect->x * 2;
    for (int row = 0; row < rect->h; ++row) {
        SDL_memcpy(destination, source, row_bytes);
        destination += data->cpu_pitch;
        source += pitch;
    }

    pvr_txr_load(data->pixels, data->vram, data->allocation_size);
    data->uploaded = true;
    return true;
}

bool DC_PVR_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                        const SDL_Rect *rect, void **pixels, int *pitch)
{
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->internal;

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
    if (data->referenced_in_scene) {
        return SDL_SetError("Dreamcast PVR texture cannot be locked while it is referenced by an open scene");
    }
    if (rect->x < 0 || rect->y < 0 || rect->w <= 0 || rect->h <= 0 ||
        rect->x > data->logical_w || rect->y > data->logical_h ||
        rect->w > data->logical_w - rect->x ||
        rect->h > data->logical_h - rect->y) {
        return SDL_SetError("Dreamcast PVR texture lock is outside the logical texture");
    }

    *pixels = (Uint8 *)data->pixels + (size_t)rect->y * data->cpu_pitch + (size_t)rect->x * 2;
    *pitch = data->cpu_pitch;
    data->locked = true;
    data->locked_rect = *rect;
    return true;
}

void DC_PVR_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->internal;

    if (!data || !data->locked) {
        return;
    }
    pvr_txr_load(data->pixels, data->vram, data->allocation_size);
    data->uploaded = true;
    data->locked = false;
    SDL_zero(data->locked_rect);
}

void DC_PVR_FreeTextureData(DC_PVR_TextureData *data)
{
    if (!data) {
        return;
    }
    if (data->vram) {
        pvr_mem_free(data->vram);
    }
    if (data->vram_next) {
        pvr_mem_free(data->vram_next);
    }
    if (data->pixels) {
        free(data->pixels);
    }
    SDL_free(data);
}

void DC_PVR_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_RenderData *renderdata = (DC_PVR_RenderData *)renderer->internal;
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->internal;

    if (!data) {
        return;
    }
    if (renderdata->header_texture == texture) {
        renderdata->header_valid = false;
        renderdata->header_texture = NULL;
    }
    if (renderdata->scene_active) {
        /* Emitted headers may still reference this VRAM until the scene
         * finishes; defer the free to presentation. */
        data->next_deferred = renderdata->deferred_textures;
        renderdata->deferred_textures = data;
    } else {
        DC_PVR_FreeTextureData(data);
    }
    texture->internal = NULL;
}

#endif /* SDL_VIDEO_RENDER_DREAMCAST_PVR */
