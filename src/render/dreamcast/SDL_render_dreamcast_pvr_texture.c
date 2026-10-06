/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Native Dreamcast PVR texture allocation and update paths. RGB565 render
  targets use paired VRAM preservation; palettes, strided layouts, and DMA
  upload remain deferred.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_DREAMCAST_PVR

#include "SDL_render_dreamcast_pvr.h"

#include <errno.h>
#include <dc/pvr/pvr_txr.h>

#if SDL_VIDEO_RENDER_DREAMCAST_PVR
static pvr_ptr_t DC_PVR_AllocTextureVRAM(SDL_Renderer *renderer, size_t size)
{
#ifdef SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS
    SDL_PropertiesID props = SDL_GetRendererProperties(renderer);
    Sint64 count = SDL_GetNumberProperty(props, DC_PVR_TEST_VRAM_ALLOCATION_COUNT, 0) + 1;
    Sint64 fail_at = SDL_GetNumberProperty(props, DC_PVR_TEST_FAIL_VRAM_ALLOCATION, 0);
    SDL_SetNumberProperty(props, DC_PVR_TEST_VRAM_ALLOCATION_COUNT, count);
    if (fail_at > 0 && count == fail_at) {
        SDL_ClearProperty(props, DC_PVR_TEST_FAIL_VRAM_ALLOCATION);
        return NULL;
    }
#else
    (void)renderer;
#endif
    return pvr_mem_malloc(size);
}
#endif

bool DC_PVR_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                          SDL_PropertiesID create_props)
{
    DC_PVR_TextureData *data;
    int pvr_format;

    (void)create_props;

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        if (texture->format != SDL_PIXELFORMAT_RGB565) {
            return SDL_SetError("Dreamcast PVR target textures require RGB565 format");
        }
        pvr_format = PVR_TXRFMT_RGB565;
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
        data->vram_next =
DC_PVR_AllocTextureVRAM(renderer, data->allocation_size);
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

    data->vram =
DC_PVR_AllocTextureVRAM(renderer, data->allocation_size);
    if (!data->vram) {
        if (data->vram_next) {
            pvr_mem_free(data->vram_next);
        }
        free(data->pixels);
        SDL_free(data);
        return SDL_SetError("Could not allocate Dreamcast PVR texture VRAM");
    }

    texture->internal = data;
    data->sample_vram = data->vram;
    texture->pitch = data->cpu_pitch;
    return true;
}

typedef struct DC_PVR_DtHeader
{
    char fourcc[4];
    Uint32 chunk_size;
    Uint8 version, header_size, codebook_size, colors_used;
    Uint16 width_pixels, height_pixels;
    Uint32 pvr_type, pad1, pad2, pad3;
} DC_PVR_DtHeader;

SDL_COMPILE_TIME_ASSERT(dc_pvr_dt_header_size, sizeof(DC_PVR_DtHeader) == 32);

SDL_Texture *DC_PVR_LoadDreamcastTexture_IO(SDL_Renderer *renderer,
                                           SDL_IOStream *src, bool closeio)
{
    DC_PVR_DtHeader header;
    SDL_Texture *texture = NULL;
    DC_PVR_TextureData *data = NULL;
    Uint32 chunk_size, header_size, pvr_type, width, height, pixel_format;
    Uint32 backing_w, backing_h;
    size_t raw_size, expected_size, payload_size;
    Uint8 *payload = NULL;
    bool vq, mipmapped, twiddled;
    bool success = false;

    if (!renderer || !src) {
        SDL_InvalidParamError(!renderer ? "renderer" : "src");
        goto done;
    }
    if (SDL_ReadIO(src, &header, sizeof(header)) != sizeof(header)) {
        SDL_SetError("Could not read Dreamcast .dt header");
        goto done;
    }
    if (SDL_memcmp(header.fourcc, "DcTx", 4) != 0 || header.version != 0) {
        SDL_SetError("Invalid or unsupported Dreamcast .dt header");
        goto done;
    }
    chunk_size = SDL_Swap32LE(header.chunk_size);
    pvr_type = SDL_Swap32LE(header.pvr_type);
    width = SDL_Swap16LE(header.width_pixels);
    height = SDL_Swap16LE(header.height_pixels);
    header_size = ((Uint32)header.header_size + 1u) * 32u;
    backing_w = 1u << (((pvr_type >> 3) & 7u) + 3u);
    backing_h = 1u << ((pvr_type & 7u) + 3u);
    pixel_format = (pvr_type >> 27) & 7u;
    vq = (pvr_type & (1u << 30)) != 0;
    mipmapped = (pvr_type & (1u << 31)) != 0;
    twiddled = (pvr_type & (1u << 26)) == 0;

    if (header_size < 32u || chunk_size <= header_size || (chunk_size & 31u)) {
        SDL_SetError("Invalid Dreamcast .dt texture size");
        goto done;
    }
    if (width < 8u || height < 8u || width > 1024u || height > 1024u ||
        width != backing_w || height != backing_h) {
        SDL_SetError("Invalid Dreamcast .dt texture dimensions");
        goto done;
    }
    if ((pvr_type & ((1u << 25) | (1u << 11))) != 0) {
        SDL_SetError("Strided and partial Dreamcast .dt textures are unsupported");
        goto done;
    }
    if (pixel_format > 2u) {
        SDL_SetError("Unsupported Dreamcast .dt pixel format %u", (unsigned int)pixel_format);
        goto done;
    }
    if ((vq || mipmapped) && !twiddled) {
        SDL_SetError("Dreamcast VQ and mipmapped .dt textures must be twiddled");
        goto done;
    }
    if (mipmapped && width != height) {
        SDL_SetError("Dreamcast mipmapped .dt textures must be square");
        goto done;
    }
    raw_size = (size_t)width * height * 2u;
    if (mipmapped) {
        raw_size = raw_size * 4u / 3u + 6u;
    }
    if (vq) {
        raw_size = (raw_size + 7u) / 8u + ((size_t)header.codebook_size + 1u) * 8u;
    }
    expected_size = (raw_size + 31u) & ~(size_t)31u;
    payload_size = (size_t)chunk_size - header_size;
    if (payload_size != expected_size) {
        SDL_SetError("Dreamcast .dt payload size does not match its header");
        goto done;
    }
    if (header_size > 32u && SDL_SeekIO(src, header_size, SDL_IO_SEEK_SET) < 0) {
        SDL_SetError("Could not seek to Dreamcast .dt payload");
        goto done;
    }
    payload = (Uint8 *)SDL_malloc(payload_size);
    if (!payload) {
        SDL_OutOfMemory();
        goto done;
    }
    if (SDL_ReadIO(src, payload, payload_size) != payload_size) {
        SDL_SetError("Could not read Dreamcast .dt payload");
        goto done;
    }

    {
        SDL_PixelFormat format = pixel_format == 0 ? SDL_PIXELFORMAT_ARGB1555 :
                                 pixel_format == 1 ? SDL_PIXELFORMAT_RGB565 :
                                                    SDL_PIXELFORMAT_ARGB4444;
        texture = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STATIC,
                                    (int)width, (int)height);
    }
    if (!texture) {
        goto done;
    }
    data = (DC_PVR_TextureData *)texture->internal;
    if (!data || texture->access != SDL_TEXTUREACCESS_STATIC) {
        SDL_SetError("Dreamcast .dt texture has invalid PVR state");
        goto done;
    }

    /* Replace the ordinary linear backing with the validated native payload. */
    if (data->vram) {
        pvr_mem_free(data->vram);
        data->vram = NULL;
    }
    free(data->pixels);
    data->pixels = NULL;
    data->vram = pvr_mem_malloc(payload_size);
    if (!data->vram) {
        SDL_SetError("Could not allocate Dreamcast .dt texture VRAM");
        goto done;
    }
    data->sample_vram = data->vram;
    if (vq) {
        data->sample_vram = (pvr_ptr_t)((Uint8 *)data->vram - 2048u +
                                        ((size_t)header.codebook_size + 1u) * 8u);
    }
    data->allocation_size = payload_size;
    data->pvr_format = (int)(pvr_type & 0xfc000000u);
    data->backing_w = (int)backing_w;
    data->backing_h = (int)backing_h;
    data->cpu_pitch = 0;
    data->uploaded = true;
    pvr_txr_load(payload, data->vram, payload_size);
    texture->pitch = 0;
    success = true;

done:
    if (closeio && src) {
        SDL_CloseIO(src);
    }
    SDL_free(payload);
    if (!success) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
        return NULL;
    }
    return texture;
}

SDL_Texture *SDL_LoadDreamcastTexture_IO(SDL_Renderer *renderer,
                                        SDL_IOStream *src, bool closeio)
{
    if (renderer && renderer->name &&
        SDL_strcmp(renderer->name, DC_PVR_RENDERER_NAME) == 0) {
        return DC_PVR_LoadDreamcastTexture_IO(renderer, src, closeio);
    }
#if defined(SDL_VIDEO_RENDER_OGL)
    return SDL_GL_LoadDreamcastTexture_IO(renderer, src, closeio);
#else
    if (!renderer) {
        SDL_InvalidParamError("renderer");
    } else if (!src) {
        SDL_InvalidParamError("src");
    } else {
        SDL_SetError("Dreamcast .dt textures require the native PVR renderer");
    }
    if (closeio && src) {
        SDL_CloseIO(src);
    }
    return NULL;
#endif
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
