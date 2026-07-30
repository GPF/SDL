/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Native Dreamcast PVR renderer, Phase 0 feasibility implementation.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_DREAMCAST_PVR

#include "SDL_render_dreamcast_pvr.h"

#include "SDL_hints.h"
#include "SDL_video.h"

#include <dc/video.h>

#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
typedef enum DC_PVR_SceneKind
{
    DC_PVR_SCENE_NONE = 0,
    DC_PVR_SCENE_LOGICAL_SCREEN,
    DC_PVR_SCENE_TARGET,
    DC_PVR_SCENE_PHYSICAL
} DC_PVR_SceneKind;

static int DC_PVR_NextPowerOfTwo(int value)
{
    int result = 8;

    while (result < value) {
        result <<= 1;
    }
    return result;
}
#endif

static int DC_PVR_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window,
                                 Uint32 flags);

SDL_RenderDriver DC_RenderDriver = {
    DC_PVR_CreateRenderer,
    {
        "Dreamcast PVR",
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
        3,
        {
            SDL_PIXELFORMAT_RGB565,
            SDL_PIXELFORMAT_ARGB1555,
            SDL_PIXELFORMAT_ARGB4444
        },
        1024,
        1024
    }
};

int DC_PVR_Submit(const void *data, size_t size)
{
    if (pvr_prim(data, size) < 0) {
        return SDL_SetError("Dreamcast PVR rejected a render primitive");
    }
    return 0;
}

static void DC_PVR_ReleaseDeferredTextures(DC_PVR_RenderData *data, SDL_bool free_vram)
{
    DC_PVR_TextureData *texture = data->deferred_textures;

    data->deferred_textures = NULL;
    while (texture) {
        DC_PVR_TextureData *next = texture->next_deferred;

        DC_PVR_FreeTextureData(texture, free_vram);
        texture = next;
    }
}

static void DC_PVR_ReleaseDeferredVRAM(DC_PVR_RenderData *data, SDL_bool free_vram)
{
    DC_PVR_DeferredVRAM *entry = data->deferred_vram;

    data->deferred_vram = NULL;
    while (entry) {
        DC_PVR_DeferredVRAM *next = entry->next;

        if (free_vram && entry->vram) {
            pvr_mem_free(entry->vram);
        }
        SDL_free(entry);
        entry = next;
    }
}

static void DC_PVR_ClearTextureSceneReferences(SDL_Renderer *renderer)
{
    SDL_Texture *texture;

    for (texture = renderer->textures; texture; texture = texture->next) {
        DC_PVR_TextureData *texturedata =
            (DC_PVR_TextureData *)texture->driverdata;

        if (texturedata) {
            texturedata->referenced_in_scene = SDL_FALSE;
        }
    }
}

#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
static int DC_PVR_FinishActiveScene(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (!data->scene_active) {
        return 0;
    }
    if (data->list_active) {
        if (pvr_list_finish() < 0) {
            data->pvr_failed = SDL_TRUE;
            return SDL_SetError("Could not finish the Dreamcast PVR translucent list");
        }
        data->list_active = SDL_FALSE;
    }
    if (pvr_scene_finish() < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("Could not finish the Dreamcast PVR scene");
    }
    data->scene_active = SDL_FALSE;

    /* Central commit: after successful finish, swap current/next for both
     * target and logical-screen destinations, then mark the new current
     * valid. A failed scene must not replace current state. */
    switch (data->scene_kind) {
    case DC_PVR_SCENE_TARGET:
        if (data->scene_target) {
            DC_PVR_TextureData *texturedata =
                (DC_PVR_TextureData *)data->scene_target->driverdata;

            if (texturedata) {
                pvr_ptr_t temp = texturedata->vram;

                texturedata->vram = texturedata->vram_next;
                texturedata->vram_next = temp;
                texturedata->sample_vram = texturedata->vram;
                texturedata->current_valid = SDL_TRUE;
            }
            data->scene_target = NULL;
        }
        break;
    case DC_PVR_SCENE_LOGICAL_SCREEN:
        {
            pvr_ptr_t temp = data->screen_vram_current;

            data->screen_vram_current = data->screen_vram_next;
            data->screen_vram_next = temp;
        }
        data->screen_current_valid = SDL_TRUE;
        break;
    case DC_PVR_SCENE_PHYSICAL:
    case DC_PVR_SCENE_NONE:
        break;
    }

    data->header_valid = SDL_FALSE;
    return 0;
}
#endif

#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
static int DC_PVR_EmitPreservationSeed(SDL_Renderer *renderer,
                                       pvr_ptr_t src_vram,
                                       int src_logical_w,
                                       int src_logical_h,
                                       int src_backing_w,
                                       int src_backing_h,
                                       int dest_render_w,
                                       int dest_render_h)
{
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    DC_PVR_Vertex vertices[4];
    float u_max;
    float v_max;
    int i;

    /* Edge-safe UV endpoint validated by physical microprobe. */
    u_max = (src_logical_w <= 1) ? 0.0f :
            (float)(src_logical_w - 1) / (float)src_backing_w;
    v_max = (src_logical_h <= 1) ? 0.0f :
            (float)(src_logical_h - 1) / (float)src_backing_h;

    pvr_poly_cxt_txr(&context, PVR_LIST_TR_POLY,
                     PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                     src_backing_w, src_backing_h, src_vram,
                     PVR_FILTER_NEAREST);
    context.txr.env = PVR_TXRENV_MODULATEALPHA;
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.shading = true;
    context.gen.alpha = true;
    context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    context.depth.write = PVR_DEPTHWRITE_DISABLE;
    /* RGB565 has no sampled alpha; keep vertex alpha available. */
    context.txr.alpha = SDL_TRUE;
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ZERO;

    pvr_poly_compile(&header, &context);
    if (DC_PVR_Submit(&header, sizeof(header)) < 0) {
        return -1;
    }

    SDL_zeroa(vertices);
    for (i = 0; i < 4; ++i) {
        vertices[i].color = 0xffffffff;
    }
    vertices[0].x = 0.0f;
    vertices[0].y = 0.0f;
    vertices[0].u = 0.0f;
    vertices[0].v = 0.0f;
    vertices[1].x = (float)dest_render_w;
    vertices[1].y = 0.0f;
    vertices[1].u = u_max;
    vertices[1].v = 0.0f;
    vertices[2].x = 0.0f;
    vertices[2].y = (float)dest_render_h;
    vertices[2].u = 0.0f;
    vertices[2].v = v_max;
    vertices[3].x = (float)dest_render_w;
    vertices[3].y = (float)dest_render_h;
    vertices[3].u = u_max;
    vertices[3].v = v_max;

    for (i = 0; i < 4; ++i) {
        pvr_vertex_t vertex;

        SDL_zero(vertex);
        vertex.flags = (i == 3) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        vertex.x = vertices[i].x;
        vertex.y = vertices[i].y;
        vertex.z = 1.0f;
        vertex.u = vertices[i].u;
        vertex.v = vertices[i].v;
        vertex.argb = vertices[i].color;
        if (DC_PVR_Submit(&vertex, sizeof(vertex)) < 0) {
            return -1;
        }
    }
    return 0;
}

static int DC_PVR_BeginSceneForCurrentTarget(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (data->pvr_failed) {
        return SDL_SetError("Dreamcast PVR renderer is unavailable after a scene failure");
    }
    if (!data->scene_active) {
        if (data->current_target) {
            DC_PVR_TextureData *texturedata =
                (DC_PVR_TextureData *)data->current_target->driverdata;
            pvr_ptr_t next_vram;

            if (!texturedata) {
                data->pvr_failed = SDL_TRUE;
                return SDL_SetError("Dreamcast PVR target texture has no driver data");
            }
            next_vram = texturedata->vram_next;
            if (!next_vram) {
                data->pvr_failed = SDL_TRUE;
                return SDL_SetError("Dreamcast PVR target has no next allocation");
            }
            if (pvr_scene_begin_rtt(next_vram, texturedata->rtt_render_w,
                                    texturedata->rtt_render_h,
                                    texturedata->rtt_stride_px) < 0) {
                data->pvr_failed = SDL_TRUE;
                return SDL_SetError("Could not begin the Dreamcast PVR render target scene");
            }
            data->scene_target = data->current_target;
            data->scene_kind = DC_PVR_SCENE_TARGET;
        } else {
            pvr_ptr_t next_vram = data->screen_vram_next;

            if (!next_vram) {
                data->pvr_failed = SDL_TRUE;
                return SDL_SetError("Logical screen has no next allocation");
            }
            if (pvr_scene_begin_rtt(next_vram, data->output_w,
                                    data->output_h,
                                    data->screen_stride_px) < 0) {
                data->pvr_failed = SDL_TRUE;
                return SDL_SetError("Could not begin the Dreamcast PVR logical screen scene");
            }
            data->scene_kind = DC_PVR_SCENE_LOGICAL_SCREEN;
        }
        data->scene_active = SDL_TRUE;
        data->header_valid = SDL_FALSE;
    }
    if (!data->list_active) {
        if (pvr_list_begin(PVR_LIST_TR_POLY) < 0) {
            data->pvr_failed = SDL_TRUE;
            return SDL_SetError("Could not begin the Dreamcast PVR translucent list");
        }
        data->list_active = SDL_TRUE;

        /* Emit preservation seed only after list begin. Skip if current is
         * undefined or if the destination is already the current allocation. */
        if (data->scene_kind == DC_PVR_SCENE_TARGET && data->scene_target) {
            DC_PVR_TextureData *texturedata =
                (DC_PVR_TextureData *)data->scene_target->driverdata;

            if (texturedata && texturedata->current_valid &&
                texturedata->vram != texturedata->vram_next) {
                if (DC_PVR_EmitPreservationSeed(
                    renderer,
                    texturedata->vram,
                    texturedata->logical_w,
                    texturedata->logical_h,
                    texturedata->backing_w,
                    texturedata->backing_h,
                    texturedata->rtt_render_w,
                    texturedata->rtt_render_h) < 0) {
                    data->pvr_failed = SDL_TRUE;
                    return -1;
                }
            }
        } else if (data->scene_kind == DC_PVR_SCENE_LOGICAL_SCREEN) {
            if (data->screen_current_valid &&
                data->screen_vram_current != data->screen_vram_next) {
                if (DC_PVR_EmitPreservationSeed(
                    renderer,
                    data->screen_vram_current,
                    data->output_w,
                    data->output_h,
                    data->screen_backing_w,
                    data->screen_backing_h,
                    data->output_w,
                    data->output_h) < 0) {
                    data->pvr_failed = SDL_TRUE;
                    return -1;
                }
            }
        }
    }
    return 0;
}
#endif

#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
static int DC_PVR_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (texture == data->current_target) {
        return 0;
    }
    if (texture) {
        DC_PVR_TextureData *texturedata = (DC_PVR_TextureData *)texture->driverdata;

        if (!texturedata) {
            return SDL_SetError("Dreamcast PVR target texture has no driver data");
        }
        if (texturedata->access != SDL_TEXTUREACCESS_TARGET) {
            return SDL_SetError("Texture is not a render target");
        }
        if (texturedata->sdl_format != SDL_PIXELFORMAT_RGB565) {
            return SDL_SetError("Dreamcast PVR target textures require RGB565 format");
        }
        if (texturedata->rtt_render_w <= 0 || texturedata->rtt_render_h <= 0) {
            return SDL_SetError("Dreamcast PVR target texture has invalid dimensions");
        }
        if (texturedata->rtt_stride_px < texturedata->rtt_render_w) {
            return SDL_SetError("Dreamcast PVR target stride is smaller than render width");
        }
        if (texturedata->rtt_stride_px % 4 != 0) {
            return SDL_SetError("Dreamcast PVR target stride must be a multiple of 4 pixels");
        }
        if (!texturedata->vram) {
            return SDL_SetError("Dreamcast PVR target texture has no VRAM allocation");
        }
        if (!texturedata->vram_next) {
            return SDL_SetError("Dreamcast PVR target texture has no next allocation");
        }
    }
    if (DC_PVR_FinishActiveScene(renderer) < 0) {
        return -1;
    }
    data->current_target = texture;
    data->header_valid = SDL_FALSE;
    return 0;
}
#endif

static int DC_PVR_BeginFrame(SDL_Renderer *renderer)
{
#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
    return DC_PVR_BeginSceneForCurrentTarget(renderer);
#else
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (!data->scene_active) {
        pvr_scene_begin();
        data->scene_active = SDL_TRUE;
        data->header_valid = SDL_FALSE;
    }
    if (!data->list_active) {
        if (pvr_list_begin(PVR_LIST_TR_POLY) < 0) {
            data->pvr_failed = SDL_TRUE;
            return SDL_SetError("Could not begin the Dreamcast PVR translucent list");
        }
        data->list_active = SDL_TRUE;
    }
    return 0;
#endif
}

int DC_PVR_EmitHeader(SDL_Renderer *renderer, SDL_Texture *texture, SDL_BlendMode blend)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;
    const pvr_txr_shading_mode_t texture_env = texture ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_REPLACE;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;

    if (texture) {
        DC_PVR_TextureData *texturedata = (DC_PVR_TextureData *)texture->driverdata;

        if (!texturedata) {
            return SDL_SetError("Dreamcast PVR texture has no driver data");
        }
        if (!texturedata->uploaded) {
#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
            if (texturedata->access == SDL_TEXTUREACCESS_TARGET) {
                if (!texturedata->current_valid) {
                    return SDL_SetError("Dreamcast PVR target texture has no valid contents");
                }
            } else
#endif
            {
                return SDL_SetError("Dreamcast PVR texture has not been uploaded");
            }
        }
        if (data->header_valid && data->header_texture == texture &&
            data->header_blend == blend &&
            data->header_texture_env == texture_env &&
            data->header_filter == texturedata->filter) {
            return 0;
        }
        pvr_poly_cxt_txr(&context, PVR_LIST_TR_POLY,
                         texturedata->pvr_format,
                         texturedata->backing_w, texturedata->backing_h,
                         texturedata->sample_vram, texturedata->filter);
        /* SDL's packed vertex color modulates all four sampled channels. */
        context.txr.env = texture_env;
    } else {
        pvr_poly_cxt_col(&context, PVR_LIST_TR_POLY);
    }

    context.gen.culling = PVR_CULLING_NONE;
    context.gen.shading = true;
    context.gen.alpha = true;
    context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    context.depth.write = PVR_DEPTHWRITE_DISABLE;
    if (texture) {
        /* KOS inverts this flag. RGB565 has no sampled alpha; disabling it
         * leaves vertex alpha available for SDL modulation and blending. */
        context.txr.alpha = (texture->format == SDL_PIXELFORMAT_RGB565);
    } else if (data->header_valid && data->header_texture == NULL &&
               data->header_blend == blend &&
               data->header_texture_env == texture_env) {
        return 0;
    }
    if (blend == SDL_BLENDMODE_NONE) {
        context.blend.src = PVR_BLEND_ONE;
        context.blend.dst = PVR_BLEND_ZERO;
    } else if (blend == SDL_BLENDMODE_BLEND) {
        context.blend.src = PVR_BLEND_SRCALPHA;
        context.blend.dst = PVR_BLEND_INVSRCALPHA;
    } else if (blend == SDL_BLENDMODE_ADD) {
        /* SDL_ADD: dst = src*srcA + dst. PVR: dst = src*SRCALPHA + dst*ONE.
         * Both factors are shared across color/alpha by the PVR hardware. For
         * the current RGB565-only destination (screen and planned initial RTT),
         * alpha is irrelevant, so SRCALPHA / ONE reproduces the SDL RGB
         * equation exactly. */
        context.blend.src = PVR_BLEND_SRCALPHA;
        context.blend.dst = PVR_BLEND_ONE;
    } else if (blend == SDL_BLENDMODE_MUL) {
        /* SDL_MUL: dst = src*dstC + dst*(1-srcA). PVR: dst = src*DSTCOLOR +
         * dst*INVSRCALPHA. Same-factor sharing matches both color and alpha
         * for SDL's dstA = dst passthrough because srcA*dstA + dstA*(1-srcA)
         * = dstA. Validated here for the current RGB565-only destination.
         * Alpha-bearing PVR targets remain unvalidated for this mapping. */
        context.blend.src = PVR_BLEND_DESTCOLOR;
        context.blend.dst = PVR_BLEND_INVSRCALPHA;
    } else {
        return SDL_SetError("Unsupported Dreamcast PVR blend mode");
    }

    pvr_poly_compile(&header, &context);
    if (DC_PVR_Submit(&header, sizeof(header)) < 0) {
        return -1;
    }

    data->header_texture = texture;
    data->header_blend = blend;
    data->header_texture_env = texture_env;
    data->header_filter = texture ? ((DC_PVR_TextureData *)texture->driverdata)->filter
                                  : PVR_FILTER_NEAREST;
    data->header_valid = SDL_TRUE;
    if (texture) {
        ((DC_PVR_TextureData *)texture->driverdata)->referenced_in_scene = SDL_TRUE;
    }
    return 0;
}

static SDL_bool DC_PVR_SupportsBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    (void)renderer;
    return (blendMode == SDL_BLENDMODE_NONE || blendMode == SDL_BLENDMODE_BLEND ||
            blendMode == SDL_BLENDMODE_ADD || blendMode == SDL_BLENDMODE_MUL)
               ? SDL_TRUE
               : SDL_FALSE;
}

static int DC_PVR_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    *w = data->output_w;
    *h = data->output_h;
    return 0;
}

static int DC_PVR_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return 0;
}

static int DC_PVR_QueueSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return 0;
}

static int DC_PVR_RunCommandQueue(SDL_Renderer *renderer,
                                  SDL_RenderCommand *cmd, void *vertices,
                                  size_t vertsize)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;
    (void)vertsize;

    if (DC_PVR_BeginFrame(renderer) < 0) {
        return -1;
    }

    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_CLEAR:
            if (DC_PVR_EmitFullTargetClear(renderer, cmd) < 0) {
                return -1;
            }
            break;
        case SDL_RENDERCMD_GEOMETRY:
        case SDL_RENDERCMD_DRAW_POINTS:
        case SDL_RENDERCMD_FILL_RECTS:
            if (DC_PVR_RunGeometryCommand(renderer, cmd, vertices) < 0) {
                return -1;
            }
            break;
        case SDL_RENDERCMD_SETVIEWPORT:
            data->view_x = cmd->data.viewport.rect.x;
            data->view_y = cmd->data.viewport.rect.y;
            data->view_w = cmd->data.viewport.rect.w;
            data->view_h = cmd->data.viewport.rect.h;
            data->viewport_set = SDL_TRUE;
            data->has_viewport = (data->view_w > 0 && data->view_h > 0)
                                     ? SDL_TRUE
                                     : SDL_FALSE;
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            data->clip_rect = cmd->data.cliprect.rect;
            data->has_cliprect = cmd->data.cliprect.enabled ? SDL_TRUE : SDL_FALSE;
            break;
        case SDL_RENDERCMD_NO_OP:
        case SDL_RENDERCMD_SETDRAWCOLOR:
            break;
        default:
            return SDL_SetError("Unsupported Dreamcast PVR Phase 0 render command");
        }
        cmd = cmd->next;
    }
    return 0;
}

static int DC_PVR_RenderPresent(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
    /* Finish the active destination scene if one is open. Commit/swap
     * is handled centrally in DC_PVR_FinishActiveScene. */
    if (data->scene_active && DC_PVR_FinishActiveScene(renderer) < 0) {
        return -1;
    }

    if (!data->current_target) {
        /* Screen selected: begin physical screen scene and present the
         * completed logical screen. */
        if (!data->screen_current_valid) {
            return SDL_SetError("Logical screen has no valid completed contents");
        }
        pvr_scene_begin();
        data->scene_active = SDL_TRUE;
        data->scene_kind = DC_PVR_SCENE_PHYSICAL;
        data->header_valid = SDL_FALSE;
        if (pvr_list_begin(PVR_LIST_TR_POLY) < 0) {
            data->pvr_failed = SDL_TRUE;
            return SDL_SetError("Could not begin the Dreamcast PVR translucent list");
        }
        data->list_active = SDL_TRUE;
        if (DC_PVR_EmitPreservationSeed(
            renderer,
            data->screen_vram_current,
            data->output_w,
            data->output_h,
            data->screen_backing_w,
            data->screen_backing_h,
            data->output_w,
            data->output_h) < 0) {
            data->pvr_failed = SDL_TRUE;
            return -1;
        }
        if (DC_PVR_FinishActiveScene(renderer) < 0) {
            return -1;
        }
        /* Physical presentation does not swap logical-screen allocations.
         * Keep screen_current_valid true so the next logical RTT begins with
         * a preserved seed from the physical presentation source. */
    }
#else
    if (DC_PVR_BeginFrame(renderer) < 0) {
        return -1;
    }
    if (pvr_list_finish() < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("Could not finish the Dreamcast PVR translucent list");
    }
    data->list_active = SDL_FALSE;
    if (pvr_scene_finish() < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("Could not finish the Dreamcast PVR scene");
    }
    data->scene_active = SDL_FALSE;
    data->header_valid = SDL_FALSE;
#endif
    if (pvr_wait_render_done() < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("Timed out waiting for Dreamcast PVR rendering");
    }
    DC_PVR_ReleaseDeferredTextures(data, SDL_TRUE);
    DC_PVR_ReleaseDeferredVRAM(data, SDL_TRUE);
    DC_PVR_ClearTextureSceneReferences(renderer);
    return 0;
}

static int DC_PVR_SetVSync(SDL_Renderer *renderer, int vsync)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (vsync != 1) {
        return SDL_SetError("Dreamcast PVR presentation is always synchronized to vertical blanking");
    }
    data->vsync = SDL_TRUE;
    renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
    return 0;
}

static void DC_PVR_DestroyRenderer(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;
    SDL_bool synchronized;

    if (!data) {
        return;
    }
    synchronized = data->pvr_failed ? SDL_FALSE : SDL_TRUE;
    if (data->pvr_owned && data->list_active) {
        if (pvr_list_finish() < 0) {
            synchronized = SDL_FALSE;
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR list cleanup failed");
        } else {
            data->list_active = SDL_FALSE;
        }
    }
    if (data->pvr_owned && data->scene_active) {
        if (pvr_scene_finish() < 0) {
            synchronized = SDL_FALSE;
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR scene cleanup failed");
        } else {
            data->scene_active = SDL_FALSE;
#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
            data->scene_target = NULL;
            data->scene_kind = DC_PVR_SCENE_NONE;
#endif
            if (pvr_wait_render_done() < 0) {
                synchronized = SDL_FALSE;
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR cleanup synchronization failed");
            }
        }
    }
    if (data->pvr_owned) {
        if (!synchronized) {
            if (pvr_shutdown() < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR recovery shutdown failed");
            }
            data->pvr_owned = SDL_FALSE;
#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
            /* pvr_shutdown() discarded the allocator after failed
             * synchronization, so these allocations must not be passed to
             * pvr_mem_free(). */
            data->screen_vram_next = NULL;
            data->screen_vram_current = NULL;
#endif
            DC_PVR_ReleaseDeferredTextures(data, SDL_FALSE);
            DC_PVR_ReleaseDeferredVRAM(data, SDL_FALSE);
        } else {
            DC_PVR_ReleaseDeferredTextures(data, SDL_TRUE);
            DC_PVR_ReleaseDeferredVRAM(data, SDL_TRUE);
#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
            if (data->screen_vram_next) {
                pvr_mem_free(data->screen_vram_next);
                data->screen_vram_next = NULL;
            }
            if (data->screen_vram_current) {
                pvr_mem_free(data->screen_vram_current);
                data->screen_vram_current = NULL;
            }
#endif
            /* Render completion precedes the vblank handler's page flip. Keep
             * that handler installed through one vblank before PVR shutdown. */
            vid_waitvbl();
            if (pvr_shutdown() < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR shutdown failed");
            }
            data->pvr_owned = SDL_FALSE;
        }
    } else {
        DC_PVR_ReleaseDeferredTextures(data, SDL_FALSE);
        DC_PVR_ReleaseDeferredVRAM(data, SDL_FALSE);
    }
#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
    data->current_target = NULL;
    data->scene_target = NULL;
    data->scene_kind = DC_PVR_SCENE_NONE;
    data->header_valid = SDL_FALSE;
#endif
    SDL_free(data);
    renderer->driverdata = NULL;
}

static int DC_PVR_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, Uint32 flags)
{
    static const pvr_init_params_t pvr_params = {
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

    if (flags & SDL_RENDERER_SOFTWARE) {
        return SDL_SetError("Dreamcast PVR is not a software renderer");
    }
#if !defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
    if (flags & SDL_RENDERER_TARGETTEXTURE) {
        return SDL_SetError("Dreamcast PVR Phase 0 does not support render targets");
    }
#endif
    video_mode = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    if (!video_mode || SDL_strcmp(video_mode, "SDL_DC_DIRECT_VIDEO") != 0) {
        return SDL_SetError("Dreamcast PVR requires SDL_HINT_DC_VIDEO_MODE=SDL_DC_DIRECT_VIDEO before window creation");
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

    data->pvr_owned = SDL_TRUE;
    data->vsync = SDL_TRUE;
    data->output_w = output_w;
    data->output_h = output_h;

#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
    data->screen_backing_w = DC_PVR_NextPowerOfTwo(output_w);
    data->screen_backing_h = DC_PVR_NextPowerOfTwo(output_h);
    if (data->screen_backing_w < output_w || data->screen_backing_h < output_h) {
        pvr_shutdown();
        data->pvr_owned = SDL_FALSE;
        SDL_free(data);
        return SDL_SetError("Screen dimensions exceed POT backing capacity");
    }
    data->screen_stride_px = data->screen_backing_w;
    data->screen_alloc_size = (size_t)data->screen_stride_px *
                              (size_t)data->screen_backing_h * 2;
    data->screen_current_valid = SDL_FALSE;

    data->screen_vram_current = pvr_mem_malloc(data->screen_alloc_size);
    if (!data->screen_vram_current) {
        pvr_shutdown();
        data->pvr_owned = SDL_FALSE;
        SDL_free(data);
        return SDL_SetError("Could not allocate first logical-screen VRAM surface");
    }
    data->screen_vram_next = pvr_mem_malloc(data->screen_alloc_size);
    if (!data->screen_vram_next) {
        pvr_mem_free(data->screen_vram_current);
        data->screen_vram_current = NULL;
        pvr_shutdown();
        data->pvr_owned = SDL_FALSE;
        SDL_free(data);
        return SDL_SetError("Could not allocate second logical-screen VRAM surface");
    }
#endif

    renderer->driverdata = data;
    renderer->info = DC_RenderDriver.info;
    renderer->GetOutputSize = DC_PVR_GetOutputSize;
    renderer->SupportsBlendMode = DC_PVR_SupportsBlendMode;
    renderer->CreateTexture = DC_PVR_CreateTexture;
    renderer->CreateDreamcastTexture = DC_PVR_CreateDreamcastTexture;
    renderer->UpdateTexture = DC_PVR_UpdateTexture;
    renderer->LockTexture = DC_PVR_LockTexture;
    renderer->UnlockTexture = DC_PVR_UnlockTexture;
    renderer->SetTextureScaleMode = DC_PVR_SetTextureScaleMode;
    renderer->QueueSetViewport = DC_PVR_QueueSetViewport;
    renderer->QueueSetDrawColor = DC_PVR_QueueSetDrawColor;
    renderer->QueueDrawPoints = DC_PVR_QueueDrawPoints;
    renderer->QueueFillRects = DC_PVR_QueueFillRects;
    /* The default POINTS line method queues Bresenham points and axis-aligned
     * fill rects; SDL owns its draw_last endpoint rules before this backend. */
    renderer->QueueGeometry = DC_PVR_QueueGeometry;
    renderer->RunCommandQueue = DC_PVR_RunCommandQueue;
    renderer->RenderPresent = DC_PVR_RenderPresent;
    renderer->DestroyTexture = DC_PVR_DestroyTexture;
    renderer->DestroyRenderer = DC_PVR_DestroyRenderer;
    renderer->SetVSync = DC_PVR_SetVSync;
#if defined(DC_PVR_RENDER_TARGET_PROTOTYPE)
    renderer->SetRenderTarget = DC_PVR_SetRenderTarget;
    renderer->info.flags |= SDL_RENDERER_TARGETTEXTURE;
#endif
    return 0;
}

#endif /* SDL_VIDEO_RENDER_DREAMCAST_PVR */
