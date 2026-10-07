/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Native Dreamcast PVR renderer, Phase 0: SDL3 adapter boundary, PVR ownership,
  scene/list lifetime and polygon header emission.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_DREAMCAST_PVR

#include "SDL_render_dreamcast_pvr.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>

#include <dc/video.h>

static bool DC_PVR_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window,
                                  SDL_PropertiesID props);

#if SDL_VIDEO_RENDER_DREAMCAST_PVR
static pvr_ptr_t DC_PVR_AllocScreenVRAM(size_t size, int ordinal)
{
#ifdef SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS
    const char *fail = SDL_GetHint(DC_PVR_TEST_FAIL_SCREEN_ALLOCATION);
    if (fail && SDL_atoi(fail) == ordinal) {
        return NULL;
    }
#else
    (void)ordinal;
#endif
    return pvr_mem_malloc(size);
}
#endif

SDL_RenderDriver DC_PVR_RenderDriver = {
    DC_PVR_CreateRenderer,
    DC_PVR_RENDERER_NAME
};

/* Phase 0 runs one screen scene with one translucent polygon list. Every draw,
 * including SDL_BLENDMODE_NONE, stays in that list with depth comparison ALWAYS
 * and depth writes disabled, so SDL command order is submission order. */
#define DC_PVR_PHASE0_LIST_TYPE PVR_LIST_TR_POLY

#if SDL_VIDEO_RENDER_DREAMCAST_PVR
static bool DC_PVR_EmitPreservationSeed(SDL_Renderer *renderer,
                                       pvr_ptr_t src_vram,
                                       int logical_w, int logical_h,
                                       int backing_w, int backing_h,
                                       int dest_w, int dest_h)
{
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_vertex_t vertex;
    /* The seed is a 1:1 copy, so the UVs must span the full texel edges
     * (w, not w-1) or nearest sampling duplicates a column and a row. */
    const float u_max = (float)logical_w / (float)backing_w;
    const float v_max = (float)logical_h / (float)backing_h;
    const float x[4] = { 0.0f, (float)dest_w, 0.0f, (float)dest_w };
    const float y[4] = { 0.0f, 0.0f, (float)dest_h, (float)dest_h };
    const float u[4] = { 0.0f, u_max, 0.0f, u_max };
    const float v[4] = { 0.0f, 0.0f, v_max, v_max };

    (void)renderer;
    pvr_poly_cxt_txr(&context, PVR_LIST_TR_POLY,
                     PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                     backing_w, backing_h, src_vram, PVR_FILTER_NEAREST);
    context.txr.env = PVR_TXRENV_MODULATEALPHA;
    context.txr.alpha = true;
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.shading = true;
    context.gen.alpha = true;
    context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    context.depth.write = PVR_DEPTHWRITE_DISABLE;
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ZERO;
    pvr_poly_compile(&header, &context);
    if (!DC_PVR_Submit(&header, sizeof(header))) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        SDL_zero(vertex);
        vertex.flags = (i == 3) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        vertex.x = x[i];
        vertex.y = y[i];
        vertex.z = 1.0f;
        vertex.u = u[i];
        vertex.v = v[i];
        vertex.argb = 0xffffffffu;
        if (!DC_PVR_Submit(&vertex, sizeof(vertex))) {
            return false;
        }
    }
    return true;
}

static bool DC_PVR_ValidateRenderTarget(SDL_Texture *texture)
{
    DC_PVR_TextureData *texturedata;

    if (!texture) {
        return true;
    }
    texturedata = (DC_PVR_TextureData *)texture->internal;
    if (!texturedata || texturedata->access != SDL_TEXTUREACCESS_TARGET ||
        texturedata->sdl_format != SDL_PIXELFORMAT_RGB565 ||
        !texturedata->vram || !texturedata->vram_next ||
        texturedata->logical_w <= 0 || texturedata->logical_h <= 0 ||
        texturedata->backing_w < texturedata->logical_w ||
        texturedata->backing_h < texturedata->logical_h ||
        (texturedata->backing_w & 3) != 0) {
        return SDL_SetError("Invalid Dreamcast PVR RGB565 render target");
    }
    return true;
}

static void DC_PVR_ReleaseDeferredTextures(DC_PVR_RenderData *data);
static void DC_PVR_ClearTextureSceneReferences(SDL_Renderer *renderer);
#endif

static bool DC_PVR_BeginScene(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

    if (data->scene_active) {
        return true;
    }
#if SDL_VIDEO_RENDER_DREAMCAST_PVR
    if (data->pvr_failed) {
        return SDL_SetError("Dreamcast PVR renderer is unavailable after a scene failure");
    }
    if (data->current_target) {
        DC_PVR_TextureData *texturedata =
            (DC_PVR_TextureData *)data->current_target->internal;
        if (!DC_PVR_ValidateRenderTarget(data->current_target) ||
            pvr_scene_begin_rtt(texturedata->vram_next,
                                texturedata->logical_w,
                                texturedata->logical_h,
                                texturedata->backing_w) < 0) {
            data->pvr_failed = true;
            return SDL_SetError("Could not begin Dreamcast PVR target scene");
        }
        data->scene_kind = DC_PVR_SCENE_TARGET;
        data->scene_target = data->current_target;
    } else {
        if (pvr_scene_begin_rtt(data->screen_vram_next, data->output_w,
                                data->output_h, data->screen_stride_px) < 0) {
            data->pvr_failed = true;
            return SDL_SetError("Could not begin Dreamcast PVR logical-screen scene");
        }
        data->scene_kind = DC_PVR_SCENE_LOGICAL_SCREEN;
    }
#else
    pvr_scene_begin();
#endif
    data->scene_active = true;
    data->header_valid = false;
    return true;
}

static bool DC_PVR_BeginList(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

    if (data->list_active) {
        return true;
    }
    if (pvr_list_begin(DC_PVR_PHASE0_LIST_TYPE) < 0) {
        data->pvr_failed = true;
        return SDL_SetError("Could not begin the Dreamcast PVR list");
    }
    data->list_active = true;
    data->active_list_type = DC_PVR_PHASE0_LIST_TYPE;
    data->header_valid = false;
#if SDL_VIDEO_RENDER_DREAMCAST_PVR
    if (data->scene_kind == DC_PVR_SCENE_TARGET && data->scene_target) {
        DC_PVR_TextureData *texturedata =
            (DC_PVR_TextureData *)data->scene_target->internal;
        if (texturedata->current_valid &&
            !DC_PVR_EmitPreservationSeed(renderer, texturedata->vram,
                                         texturedata->logical_w,
                                         texturedata->logical_h,
                                         texturedata->backing_w,
                                         texturedata->backing_h,
                                         texturedata->logical_w,
                                         texturedata->logical_h)) {
            data->pvr_failed = true;
            return false;
        }
    } else if (data->scene_kind == DC_PVR_SCENE_LOGICAL_SCREEN &&
               data->screen_current_valid) {
        if (!DC_PVR_EmitPreservationSeed(renderer, data->screen_vram,
                                         data->output_w, data->output_h,
                                         data->screen_backing_w,
                                         data->screen_backing_h,
                                         data->output_w, data->output_h)) {
            data->pvr_failed = true;
            return false;
        }
    }
#endif
    return true;
}

static bool DC_PVR_FinishCurrentList(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

    if (!data->list_active) {
        return true;
    }
    if (pvr_list_finish() < 0) {
        data->pvr_failed = true;
        return SDL_SetError("Could not finish the Dreamcast PVR list");
    }
    data->list_active = false;
    data->active_list_type = PVR_LIST_NONE;
    return true;
}

static bool DC_PVR_FinishActiveScene(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

    if (!DC_PVR_FinishCurrentList(renderer)) {
        return false;
    }
    if (!data->scene_active) {
        return true;
    }
    if (pvr_scene_finish() < 0) {
        data->pvr_failed = true;
        return SDL_SetError("Could not finish the Dreamcast PVR scene");
    }
    data->scene_active = false;
#if SDL_VIDEO_RENDER_DREAMCAST_PVR
    if (pvr_wait_render_done() < 0) {
        data->pvr_failed = true;
        return SDL_SetError("Timed out waiting for Dreamcast PVR scene completion");
    }
    if (data->scene_kind == DC_PVR_SCENE_TARGET && data->scene_target) {
        DC_PVR_TextureData *texturedata =
            (DC_PVR_TextureData *)data->scene_target->internal;
        pvr_ptr_t temp = texturedata->vram;
        texturedata->vram = texturedata->vram_next;
        texturedata->vram_next = temp;
        texturedata->sample_vram = texturedata->vram;
        texturedata->current_valid = true;
    } else if (data->scene_kind == DC_PVR_SCENE_LOGICAL_SCREEN) {
        pvr_ptr_t temp = data->screen_vram;
        data->screen_vram = data->screen_vram_next;
        data->screen_vram_next = temp;
        data->screen_current_valid = true;
    }
    data->scene_target = NULL;
    data->scene_kind = DC_PVR_SCENE_NONE;
    DC_PVR_ReleaseDeferredTextures(data);
    DC_PVR_ClearTextureSceneReferences(renderer);
#endif
    data->header_valid = false;
    return true;
}

static void DC_PVR_ReleaseDeferredTextures(DC_PVR_RenderData *data)
{
    DC_PVR_TextureData *texture = data->deferred_textures;

    data->deferred_textures = NULL;
    while (texture) {
        DC_PVR_TextureData *next = texture->next_deferred;

        DC_PVR_FreeTextureData(texture);
        texture = next;
    }
}

static void DC_PVR_ClearTextureSceneReferences(SDL_Renderer *renderer)
{
    for (SDL_Texture *texture = renderer->textures; texture; texture = texture->next) {
        DC_PVR_TextureData *texturedata = (DC_PVR_TextureData *)texture->internal;

        if (texturedata) {
            texturedata->referenced_in_scene = false;
        }
    }
}

bool DC_PVR_EmitHeader(SDL_Renderer *renderer, SDL_Texture *texture,
                       SDL_BlendMode blend, SDL_ScaleMode scale_mode, int list_type)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;
    DC_PVR_TextureData *texturedata = texture ? (DC_PVR_TextureData *)texture->internal : NULL;
    pvr_txr_shading_mode_t texture_env = texture ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_REPLACE;
    pvr_filter_mode_t filter;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;

    if (texture) {
        if (!texturedata) {
            return SDL_SetError("Dreamcast PVR texture has no driver data");
        }
#if SDL_VIDEO_RENDER_DREAMCAST_PVR
        if (texturedata->access == SDL_TEXTUREACCESS_TARGET) {
            if (!texturedata->current_valid) {
                return SDL_SetError("Dreamcast PVR render target has no completed contents");
            }
        } else
#endif
        if (!texturedata->uploaded) {
            return SDL_SetError("Dreamcast PVR texture has not been uploaded");
        }
        if (scale_mode == SDL_SCALEMODE_NEAREST) {
            filter = PVR_FILTER_NEAREST;
        } else if (scale_mode == SDL_SCALEMODE_LINEAR) {
            filter = PVR_FILTER_BILINEAR;
        } else {
            return SDL_SetError("Dreamcast PVR supports only nearest and linear texture filtering");
        }
        if (data->header_valid &&
            data->header_list_type == list_type &&
            data->header_texture == texture &&
            data->header_blend == blend &&
            data->header_scale_mode == scale_mode &&
            data->header_texture_env == texture_env &&
            data->header_filter == filter) {
            return true;
        }
        pvr_poly_cxt_txr(&context, list_type, texturedata->pvr_format,
                         texturedata->backing_w, texturedata->backing_h,
                         texturedata->sample_vram ? texturedata->sample_vram : texturedata->vram,
                         filter);
    } else {
        filter = PVR_FILTER_NEAREST;
        if (data->header_valid &&
            data->header_list_type == list_type &&
            data->header_texture == NULL &&
            data->header_blend == blend) {
            return true;
        }
        pvr_poly_cxt_col(&context, list_type);
    }

    context.gen.culling = PVR_CULLING_NONE;
    context.gen.shading = true;
    context.gen.alpha = true;
    context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    context.depth.write = PVR_DEPTHWRITE_DISABLE;
    if (texture) {
        /* KOS inverts this flag. RGB565 has no sampled alpha; disabling it
         * leaves vertex alpha available for SDL modulation and blending. */
        context.txr.alpha = (texturedata->sdl_format == SDL_PIXELFORMAT_RGB565);
    }

    /* Phase 0 supports the two modes the first smoke tests need. SDL3 core
     * advertises ADD/MOD/MUL as universally supported, so the honest gate is
     * here rather than in SupportsBlendMode. */
    if (blend == SDL_BLENDMODE_NONE) {
        context.blend.src = PVR_BLEND_ONE;
        context.blend.dst = PVR_BLEND_ZERO;
    } else if (blend == SDL_BLENDMODE_BLEND) {
        context.blend.src = PVR_BLEND_SRCALPHA;
        context.blend.dst = PVR_BLEND_INVSRCALPHA;
    } else {
        return SDL_SetError("Unsupported Dreamcast PVR blend mode in Phase 0");
    }

    pvr_poly_compile(&header, &context);
    if (!DC_PVR_Submit(&header, sizeof(header))) {
        return false;
    }

    data->header_texture = texture;
    data->header_blend = blend;
    data->header_scale_mode = scale_mode;
    data->header_texture_env = texture_env;
    data->header_filter = filter;
    data->header_list_type = list_type;
    data->header_valid = true;
    if (texture) {
        texturedata->referenced_in_scene = true;
    }
    return true;
}

static bool DC_PVR_SupportsBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    (void)renderer;
    return (blendMode == SDL_BLENDMODE_NONE || blendMode == SDL_BLENDMODE_BLEND);
}

static bool DC_PVR_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

    if (!w || !h) {
        return SDL_InvalidParamError("size");
    }
    *w = data->output_w;
    *h = data->output_h;
    return true;
}

static bool DC_PVR_QueueNoOp(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return true;
}

static void DC_PVR_InvalidateCachedState(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

    data->header_valid = false;
}

static bool DC_PVR_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                   void *vertices, size_t vertsize)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

    (void)vertsize;

    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
            /* cmd->data.viewport.rect is view->pixel_viewport: already scaled
             * and already includes view->logical_offset. */
            data->pixel_viewport = cmd->data.viewport.rect;
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            /* cmd->data.cliprect.rect is view->pixel_clip_rect: scaled but
             * viewport-relative. */
            data->pixel_clip_rect = cmd->data.cliprect.rect;
            data->clipping_enabled = cmd->data.cliprect.enabled;
            break;
        case SDL_RENDERCMD_CLEAR:
            if (!DC_PVR_BeginScene(renderer) || !DC_PVR_BeginList(renderer)) {
                return false;
            }
            if (!DC_PVR_EmitClear(renderer, cmd)) {
                return false;
            }
            break;
        case SDL_RENDERCMD_GEOMETRY:
        case SDL_RENDERCMD_DRAW_POINTS:
        case SDL_RENDERCMD_FILL_RECTS:
            if (cmd->data.draw.count == 0) {
                break;
            }
            if (!DC_PVR_BeginScene(renderer) || !DC_PVR_BeginList(renderer)) {
                return false;
            }
            if (!DC_PVR_RunGeometryCommand(renderer, cmd, vertices)) {
                return false;
            }
            break;
        case SDL_RENDERCMD_SETDRAWCOLOR:
        case SDL_RENDERCMD_NO_OP:
            break;
        default:
            return SDL_SetError("Unsupported Dreamcast PVR Phase 0 render command");
        }
        cmd = cmd->next;
    }
    return true;
}

static bool DC_PVR_RenderPresent(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

#if SDL_VIDEO_RENDER_DREAMCAST_PVR
    if (data->current_target) {
        return SDL_SetError("SDL3 does not present while a Dreamcast PVR target is selected");
    }
    if (!DC_PVR_FinishActiveScene(renderer)) {
        return false;
    }
    if (data->screen_current_valid) {
        pvr_scene_begin();
        data->scene_active = true;
        data->scene_kind = DC_PVR_SCENE_PHYSICAL;
        data->header_valid = false;
        if (pvr_list_begin(DC_PVR_PHASE0_LIST_TYPE) < 0) {
            data->pvr_failed = true;
            return SDL_SetError("Could not begin Dreamcast PVR presentation list");
        }
        data->list_active = true;
        data->active_list_type = DC_PVR_PHASE0_LIST_TYPE;
        if (!DC_PVR_EmitPreservationSeed(renderer, data->screen_vram,
                                         data->output_w, data->output_h,
                                         data->screen_backing_w,
                                         data->screen_backing_h,
                                         data->output_w, data->output_h) ||
            !DC_PVR_FinishActiveScene(renderer)) {
            data->pvr_failed = true;
            return false;
        }
    } else {
        /* An untouched SDL backbuffer has undefined contents. Preserve the
         * existing empty-present behavior without sampling uninitialized VRAM. */
        pvr_scene_begin();
        data->scene_active = true;
        data->scene_kind = DC_PVR_SCENE_PHYSICAL;
        if (!DC_PVR_FinishActiveScene(renderer)) {
            return false;
        }
    }
    data->screen_current_valid = false;
    DC_PVR_ReleaseDeferredTextures(data);
    DC_PVR_ClearTextureSceneReferences(renderer);
    return true;
#else
    /* An empty frame still needs a scene and list. */
    if (!DC_PVR_BeginScene(renderer) || !DC_PVR_BeginList(renderer)) {
        return false;
    }
    if (!DC_PVR_FinishActiveScene(renderer)) {
        return false;
    }
    if (pvr_wait_render_done() < 0) {
        data->pvr_failed = true;
        return SDL_SetError("Timed out waiting for Dreamcast PVR rendering");
    }
    DC_PVR_ReleaseDeferredTextures(data);
    DC_PVR_ClearTextureSceneReferences(renderer);
    return true;
#endif
}

#if SDL_VIDEO_RENDER_DREAMCAST_PVR
static bool DC_PVR_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;
    SDL_PropertiesID props = SDL_GetRendererProperties(renderer);

    if (data->pvr_failed) {
        return SDL_SetError("Dreamcast PVR renderer is unavailable after a scene failure");
    }
#ifdef SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS
    if (SDL_GetBooleanProperty(props, DC_PVR_TEST_FAIL_NEXT_TARGET_SWITCH, false)) {
        SDL_ClearProperty(props, DC_PVR_TEST_FAIL_NEXT_TARGET_SWITCH);
        return SDL_SetError("Injected Dreamcast PVR target-switch failure");
    }
#else
    (void)props;
#endif
    if (!DC_PVR_ValidateRenderTarget(texture)) {
        return false;
    }
    if (!DC_PVR_FinishActiveScene(renderer)) {
        return false;
    }

#ifdef DC_PVR_TRACE_TARGETS
    /* Diagnostic only: compile with -DDC_PVR_TRACE_TARGETS. No behavior change. */
    {
        SDL_Texture *prev = data->current_target;
        DC_PVR_TextureData *pd = prev ? (DC_PVR_TextureData *)prev->internal : NULL;
        DC_PVR_TextureData *nd = texture ? (DC_PVR_TextureData *)texture->internal : NULL;
        SDL_Log("PVRTRACE switch prev=%p(vram=%p next=%p sample=%p valid=%d) -> new=%p(vram=%p next=%p sample=%p valid=%d) screen(vram=%p next=%p valid=%d)",
                (void *)prev, pd ? (void *)pd->vram : NULL, pd ? (void *)pd->vram_next : NULL,
                pd ? (void *)pd->sample_vram : NULL, pd ? pd->current_valid : -1,
                (void *)texture, nd ? (void *)nd->vram : NULL, nd ? (void *)nd->vram_next : NULL,
                nd ? (void *)nd->sample_vram : NULL, nd ? nd->current_valid : -1,
                (void *)data->screen_vram, (void *)data->screen_vram_next,
                data->screen_current_valid);
    }
#endif
    data->current_target = texture;
    data->render_w = texture ? texture->w : data->output_w;
    data->render_h = texture ? texture->h : data->output_h;
    data->header_valid = false;
    return true;
}
#endif

static bool DC_PVR_SetVSync(SDL_Renderer *renderer, int vsync)
{
    (void)renderer;

    if (vsync == 0) {
        /* SDL3 uses this as its no-simulated-vsync default. The Dreamcast
         * video layer still changes the displayed page on vertical blank. */
        return true;
    }
    if (vsync == 1) {
        return true;
    }
    return SDL_Unsupported();
}

static void DC_PVR_DestroyRenderer(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;
    bool synchronized;

    if (!data) {
        return;
    }
    synchronized = !data->pvr_failed;

    if (data->pvr_owned && data->list_active) {
        if (pvr_list_finish() < 0) {
            synchronized = false;
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR list cleanup failed");
        } else {
            data->list_active = false;
        }
    }
    if (data->pvr_owned && data->scene_active) {
        if (pvr_scene_finish() < 0) {
            synchronized = false;
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR scene cleanup failed");
        } else {
            data->scene_active = false;
            if (pvr_wait_render_done() < 0) {
                synchronized = false;
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR cleanup synchronization failed");
            }
        }
    }
    /* Deferred VRAM must be released after render completion and before
     * pvr_shutdown(), which invalidates the PVR memory allocator. */
    DC_PVR_ReleaseDeferredTextures(data);

#if SDL_VIDEO_RENDER_DREAMCAST_PVR
    if (data->screen_vram) {
        pvr_mem_free(data->screen_vram);
        data->screen_vram = NULL;
    }
    if (data->screen_vram_next) {
        pvr_mem_free(data->screen_vram_next);
        data->screen_vram_next = NULL;
    }
#endif

    if (data->pvr_owned) {
        if (!synchronized) {
            if (pvr_shutdown() < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR recovery shutdown failed");
            }
        } else {
            /* Render completion precedes the vblank handler's page flip. Keep
             * that handler installed through one vblank before PVR shutdown. */
            vid_waitvbl();
            if (pvr_shutdown() < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR shutdown failed");
            }
        }
        data->pvr_owned = false;
    }
    SDL_free(data);
    renderer->internal = NULL;
}

static bool DC_PVR_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window,
                                  SDL_PropertiesID props)
{
    /* Phase 0 contract: enable only PVR_LIST_TR_POLY, direct submission, no
     * DMA (KOS receives dma_enabled = 0), translucent autosort disabled so
     * SDL painter order is preserved. */
    pvr_init_params_t pvr_params = {
        { PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
        512 * 1024,
        0, /* Direct submission; no DMA. */
        0, /* No FSAA. */
        1, /* Disable translucent autosort to preserve SDL command order. */
        3,
        0
    };
    DC_PVR_RenderData *data;
    const char *video_mode;
    int output_w;
    int output_h;

    (void)props;

    video_mode = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    if (!video_mode || SDL_strcmp(video_mode, "SDL_DC_DREAMCAST_PVR_VIDEO") != 0) {
        return SDL_SetError("Dreamcast PVR requires SDL_HINT_DC_VIDEO_MODE=SDL_DC_DREAMCAST_PVR_VIDEO before window creation");
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_OPENGL) {
        return SDL_SetError("Dreamcast PVR cannot use an OpenGL window");
    }
    if (SDL_GL_GetCurrentContext() != NULL) {
        return SDL_SetError("Dreamcast PVR cannot be created while an OpenGL context is current");
    }
    SDL_GetWindowSizeInPixels(window, &output_w, &output_h);
    if (output_w <= 0 || output_h <= 0 || (output_w & 31) != 0) {
        return SDL_SetError("Dreamcast PVR requires a positive output size with width divisible by 32");
    }

    data = (DC_PVR_RenderData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }
    if (pvr_init(&pvr_params) < 0) {
        SDL_free(data);
        return SDL_SetError("Could not initialize Dreamcast PVR (it may already be owned)");
    }

    data->pvr_owned = true;
    data->output_w = output_w;
    data->output_h = output_h;
    data->render_w = output_w;
    data->render_h = output_h;
    data->active_list_type = DC_PVR_PHASE0_LIST_TYPE;

#if SDL_VIDEO_RENDER_DREAMCAST_PVR
    data->screen_backing_w = SDL_powerof2(output_w);
    data->screen_backing_h = SDL_powerof2(output_h);
    data->screen_stride_px = data->screen_backing_w;
    data->screen_allocation_size = (size_t)data->screen_backing_w *
                                   (size_t)data->screen_backing_h * 2;
    data->screen_vram = DC_PVR_AllocScreenVRAM(data->screen_allocation_size, 1);
    data->screen_vram_next = DC_PVR_AllocScreenVRAM(data->screen_allocation_size, 2);
    if (!data->screen_vram || !data->screen_vram_next) {
        if (data->screen_vram) {
            pvr_mem_free(data->screen_vram);
        }
        if (data->screen_vram_next) {
            pvr_mem_free(data->screen_vram_next);
        }
        pvr_shutdown();
        SDL_free(data);
        return SDL_SetError("Could not allocate Dreamcast PVR logical-screen preservation VRAM");
    }
#endif

    renderer->name = DC_PVR_RenderDriver.name;
    renderer->internal = data;
    renderer->GetOutputSize = DC_PVR_GetOutputSize;
    renderer->SupportsBlendMode = DC_PVR_SupportsBlendMode;
    renderer->CreateTexture = DC_PVR_CreateTexture;
    renderer->UpdateTexture = DC_PVR_UpdateTexture;
    renderer->LockTexture = DC_PVR_LockTexture;
    renderer->UnlockTexture = DC_PVR_UnlockTexture;
    renderer->QueueSetViewport = DC_PVR_QueueNoOp;
    renderer->QueueSetDrawColor = DC_PVR_QueueNoOp;
    renderer->QueueDrawPoints = DC_PVR_QueueDrawPoints;
    renderer->QueueDrawLines = DC_PVR_QueueDrawLines;
    renderer->QueueFillRects = DC_PVR_QueueFillRects;
    /* QueueCopy/QueueCopyEx stay unset: SDL core translates copies, rotation
     * and flips into QueueGeometry, so the geometry path is the only emitter. */
    renderer->QueueGeometry = DC_PVR_QueueGeometry;
    renderer->InvalidateCachedState = DC_PVR_InvalidateCachedState;
    renderer->RunCommandQueue = DC_PVR_RunCommandQueue;
    renderer->RenderPresent = DC_PVR_RenderPresent;
    renderer->DestroyTexture = DC_PVR_DestroyTexture;
    renderer->DestroyRenderer = DC_PVR_DestroyRenderer;
    renderer->SetVSync = DC_PVR_SetVSync;
#if SDL_VIDEO_RENDER_DREAMCAST_PVR
    renderer->SetRenderTarget = DC_PVR_SetRenderTarget;
#endif

    renderer->npot_texture_wrap_unsupported = true;
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_RGB565);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB1555);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB4444);
    SDL_SetNumberProperty(SDL_GetRendererProperties(renderer),
                          SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 1024);

    DC_PVR_InvalidateCachedState(renderer);

    return true;
}

#endif /* SDL_VIDEO_RENDER_DREAMCAST_PVR */
