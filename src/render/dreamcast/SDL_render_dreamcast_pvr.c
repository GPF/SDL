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
#include <dc/pvr/pvr_txr.h>

/* Ordered render targets are always enabled in the native PVR renderer.
 * The legacy DC_PVR_RENDER_TARGET_PROTOTYPE has been retired.
 */
typedef enum DC_PVR_SceneKind
{
    DC_PVR_SCENE_NONE = 0,
    DC_PVR_SCENE_LOGICAL_SCREEN,
    DC_PVR_SCENE_TARGET,
    DC_PVR_SCENE_PHYSICAL
} DC_PVR_SceneKind;

static int DC_PVR_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window,
                                 Uint32 flags);

#define DC_PVR_RENDERER_VERSION "Dreamcast PVR"

SDL_RenderDriver DC_RenderDriver = {
    DC_PVR_CreateRenderer,
    {
        /* SDL_RenderDriver.info.name is a public selector and is matched
         * exactly by SDL_CreateRenderer's SDL_HINT_RENDER_DRIVER path.
         * Keep the stable name separate from the build diagnostic above. */
        DC_PVR_RENDERER_NAME,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
        SDL_RENDERER_TARGETTEXTURE,
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

/* ---- Texture VRAM retirement by scene generation -------------------------
 *
 * Invariant: the VRAM behind a destroyed texture stays allocated until every PVR
 * scene that could still fetch it has finished rendering. pvr_mem_free() writes
 * allocator metadata into the block and a new texture can be uploaded over it, so
 * an early free corrupts any scene still to be rendered.
 *
 * Nothing here sleeps to establish that at destroy or present time. Scenes are
 * numbered in submission order, and a destroyed texture is tagged with the latest
 * scene begun (the last one that could reference it). What KallistiOS lets us
 * conclude without waiting (pvr_scene.c):
 *
 *  1. A render starts only when no render is in progress, so renders run strictly
 *     one after another in scene order.
 *  2. A scene enters the TA at its first pvr_list_begin(). That call runs
 *     pvr_start_ta_rendering(), which waits for the previous scene's render to
 *     have started (ta_busy clear). pvr_scene_begin() alone does not wait.
 *  3. So once scene m has entered, render m-1 has started and, by (1), every
 *     render up to m-2 is complete.
 *  4. After scene m is finished, pvr_check_ready() == 0 (note: 0 means ready) says
 *     render m has started, so every render up to m-1 is complete.
 *
 * (2) rests on pvr_wait_ready(), which gives up after 100 ms without reporting it
 * to pvr_start_ta_rendering(); a render stalled that long is already a hardware or
 * system failure, and the blocking drain below checks its results. This path also
 * requires PVR vertex-buffer double buffering to stay enabled: with a single
 * vertex buffer, pvr_start_ta_rendering() waits for the previous render to
 * complete, which is the barrier this design removes.
 *
 * Reclamation therefore needs only these cheap checks and runs at scene entry,
 * scene finish and texture destruction. A blocking drain is used only when an
 * allocation fails, before a CPU write into texture VRAM that a recent scene
 * sampled, and at renderer shutdown.
 */
static void DC_PVR_NoteSceneBegun(DC_PVR_RenderData *data)
{
    data->scenes_begun++;
    data->scene_entered = SDL_FALSE;
}

static void DC_PVR_NoteSceneEntered(DC_PVR_RenderData *data)
{
    if (!data->scene_entered) {
        data->scene_entered = SDL_TRUE;
        data->scenes_entered = data->scenes_begun;
    }
}

/* Highest scene number whose render is proven complete without waiting. */
static Uint64 DC_PVR_CompletedScenes(DC_PVR_RenderData *data)
{
    const Uint64 m = data->scenes_entered; /* latest scene that has entered the TA */
    const SDL_bool m_is_open = data->scene_active && data->scene_entered &&
                               data->scenes_begun == m;
    Uint64 done = (m >= 2) ? (m - 2) : 0; /* (3) */

    if (m >= 1 && !m_is_open && pvr_check_ready() == 0) {
        done = m - 1; /* (4) */
    }
    if (done > data->scenes_completed) {
        data->scenes_completed = done;
    }
    return data->scenes_completed;
}

static void DC_PVR_ReclaimRetired(DC_PVR_RenderData *data, Uint64 completed,
                                  SDL_bool free_vram)
{
    DC_PVR_TextureData **texture_link = &data->retired_textures;
    DC_PVR_DeferredVRAM **vram_link = &data->retired_vram;

    while (*texture_link) {
        DC_PVR_TextureData *texture = *texture_link;

        if (texture->retire_gen <= completed) {
            *texture_link = texture->next_deferred;
            DC_PVR_FreeTextureData(texture, free_vram);
        } else {
            texture_link = &texture->next_deferred;
        }
    }

    while (*vram_link) {
        DC_PVR_DeferredVRAM *entry = *vram_link;

        if (entry->retire_gen <= completed) {
            *vram_link = entry->next;
            if (free_vram && entry->vram) {
                pvr_mem_free(entry->vram);
            }
            SDL_free(entry);
        } else {
            vram_link = &entry->next;
        }
    }
}

static void DC_PVR_ReclaimCompleted(DC_PVR_RenderData *data)
{
    if (data->retired_textures || data->retired_vram) {
        DC_PVR_ReclaimRetired(data, DC_PVR_CompletedScenes(data), SDL_TRUE);
    }
}

/* Blocks until every scene that has entered the TA, except one still open, has
 * finished rendering. Returns SDL_FALSE if a KallistiOS wait timed out. */
static int DC_PVR_WaitScenesComplete(DC_PVR_RenderData *data)
{
    const SDL_bool open_entered = data->scene_active && data->scene_entered;
    Uint64 done;

    if (data->scenes_entered > 0) {
        /* ta_busy belongs to the latest entered scene. If that scene is finished,
         * wait for its render to start; if it is open, render m-1 has already
         * started (2) and waiting for ta_busy would never end. */
        if (!open_entered && pvr_wait_ready() < 0) {
            return SDL_SetError("Timed out waiting for the Dreamcast PVR Tile Accelerator");
        }
        if (pvr_wait_render_done() < 0) {
            return SDL_SetError("Timed out waiting for Dreamcast PVR scene completion");
        }
    }
    done = open_entered ? (data->scenes_entered - 1) : data->scenes_entered;
    if (done > data->scenes_completed) {
        data->scenes_completed = done;
    }
    return 0;
}

void DC_PVR_RetireTexture(SDL_Renderer *renderer, DC_PVR_TextureData *texture)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    /* Exact: the last scene that actually sampled this VRAM. A destroyed texture
     * cannot be referenced by any later scene, so a never-sampled allocation is
     * reclaimable at once. */
    texture->retire_gen = texture->last_used_gen;
    texture->next_deferred = data->retired_textures;
    data->retired_textures = texture;
    DC_PVR_ReclaimCompleted(data);
}

void DC_PVR_RetireVRAM(SDL_Renderer *renderer, DC_PVR_DeferredVRAM *entry, Uint64 gen)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    entry->retire_gen = gen;
    entry->next = data->retired_vram;
    data->retired_vram = entry;
    DC_PVR_ReclaimCompleted(data);
}

/* CPU writes into texture VRAM (uploads) must not land while a scene that sampled
 * the texture can still be rendering. Free when already proven, wait otherwise. */
SDL_bool DC_PVR_TextureIsIdle(SDL_Renderer *renderer, DC_PVR_TextureData *texture)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    return (texture->last_used_gen == 0 ||
            texture->last_used_gen <= DC_PVR_CompletedScenes(data)) ? SDL_TRUE : SDL_FALSE;
}

int DC_PVR_WaitTextureIdle(SDL_Renderer *renderer, DC_PVR_TextureData *texture)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (DC_PVR_TextureIsIdle(renderer, texture)) {
        return 0;
    }
    if (DC_PVR_WaitScenesComplete(data) < 0) {
        return -1;
    }
    if (!DC_PVR_TextureIsIdle(renderer, texture)) {
        return SDL_SetError("Dreamcast PVR texture is still referenced by an open scene");
    }
    return 0;
}

/* Called when pvr_mem_malloc() fails: retired VRAM that is only waiting for a
 * render to finish becomes allocatable after a blocking drain. */
SDL_bool DC_PVR_ReclaimForAllocation(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (!data->retired_textures && !data->retired_vram) {
        return SDL_FALSE;
    }
    if (DC_PVR_WaitScenesComplete(data) < 0) {
        return SDL_FALSE;
    }
    DC_PVR_ReclaimRetired(data, data->scenes_completed, SDL_TRUE);
    return SDL_TRUE;
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

static void DC_PVR_ResetFrameStride(DC_PVR_RenderData *data)
{
    data->frame_stride_px = 0;
    ++data->stride_generation;
}

#if SDL_DC_PVR_OPAQUE_PREFIX_ROUTING
/* Opaque-prefix eligibility: a drawable is eligible for PVR_LIST_OP_POLY
 * only while the prefix is open and the draw is either a supported full-
 * target clear or untextured SDL_BLENDMODE_NONE geometry.
 *
 * A textured SDL_BLENDMODE_NONE draw is NOT eligible for the opaque
 * prefix because it must be emitted through PVR_LIST_TR_POLY to preserve
 * SDL painter order (textured NONE in OP would render before later TR
 * draws, reversing SDL order).
 *
 * Once the prefix closes it never reopens in the same frame.
 */
static SDL_bool DC_PVR_IsOpaquePrefixEligible(SDL_BlendMode blend,
                                                SDL_Texture *texture,
                                                SDL_bool opaque_prefix_open)
{
    if (!opaque_prefix_open)
        return SDL_FALSE;
    if (blend != SDL_BLENDMODE_NONE)
        return SDL_FALSE;
    if (texture != NULL)
        return SDL_FALSE;
    return SDL_TRUE;
}

/* Begin the requested PVR list, transitioning from the current list if
 * necessary.  Invalidates the polygon-header cache on any transition.
 * Called from RunCommandQueue; only one list may be active at a time. */
static int DC_PVR_BeginList(SDL_Renderer *renderer, int list_type)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (data->active_list_type == list_type)
        return 0;

    if (data->list_active) {
        if (pvr_list_finish() < 0) {
            data->pvr_failed = SDL_TRUE;
            return SDL_SetError("Could not finish the Dreamcast PVR list during opaque-prefix transition");
        }
        data->list_active = SDL_FALSE;
        data->active_list_type = PVR_LIST_NONE;
    }

    data->header_valid = SDL_FALSE;

    if (pvr_list_begin(list_type) < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("Could not begin the Dreamcast PVR list");
    }
    data->list_active = SDL_TRUE;
    data->active_list_type = list_type;
    DC_PVR_NoteSceneEntered(data);
    DC_PVR_ReclaimCompleted(data);
    return 0;
}
#endif

/* Finish whatever list is currently active.  Safe to call when no list is
 * active; safe to call from destruction with an incomplete list. */
static int DC_PVR_FinishCurrentList(SDL_Renderer *renderer)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (!data->list_active)
        return 0;
    if (pvr_list_finish() < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("Could not finish the Dreamcast PVR list");
    }
    data->list_active = SDL_FALSE;
    data->active_list_type = PVR_LIST_NONE;
    return 0;
}

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
    /* pvr_scene_finish() opens any list that was never opened, so the scene has
     * entered the TA by now. No wait here: renders run in submission order, so a
     * finished scene only has to be ordered before later scenes. VRAM that a
     * pending render may still read is released by the retire list once that
     * render is proven complete (see DC_PVR_CompletedScenes). */
    DC_PVR_NoteSceneEntered(data);

    /* Central commit: after successful finish, swap target current/next and,
     * in the full preservation prototype only, logical-screen current/next.
     * A failed scene must not replace current state. */
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
        /* Retired prototype — no logical-screen swap needed. */
        break;
    case DC_PVR_SCENE_PHYSICAL:
    case DC_PVR_SCENE_NONE:
        break;
    }

    data->header_valid = SDL_FALSE;
    DC_PVR_ReclaimCompleted(data);
    return 0;
}

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

    (void)renderer;

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
            /* No logical screen — render directly to the physical screen. */
            pvr_scene_begin();
            data->scene_kind = DC_PVR_SCENE_PHYSICAL;
        }
        data->scene_active = SDL_TRUE;
        DC_PVR_NoteSceneBegun(data);
        data->header_valid = SDL_FALSE;
    }
    if (!data->list_active) {
        if (pvr_list_begin(PVR_LIST_TR_POLY) < 0) {
            data->pvr_failed = SDL_TRUE;
            return SDL_SetError("Could not begin the Dreamcast PVR translucent list");
        }
        data->list_active = SDL_TRUE;
        data->active_list_type = PVR_LIST_TR_POLY;
        DC_PVR_NoteSceneEntered(data);

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
        }
    }
    DC_PVR_ReclaimCompleted(data);
    return 0;
}

static int DC_PVR_ValidateRenderTarget(SDL_Texture *texture)
{
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
    return 0;
}

static int DC_PVR_PrepareRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    SDL_assert(texture != data->current_target);
    SDL_assert(!data->target_transition_prepared);
    if (DC_PVR_ValidateRenderTarget(texture) < 0) {
        return -1;
    }
    if (texture && !data->current_target &&
        data->ordered_frame_state == DC_PVR_SCREEN_PASS_STARTED) {
        return SDL_SetError("Dreamcast PVR render targets must be rendered before the screen pass; present the screen before selecting a texture target");
    }
    if (DC_PVR_FinishActiveScene(renderer) < 0) {
        return -1;
    }
    data->prepared_target = texture;
    data->target_transition_prepared = SDL_TRUE;
    return 0;
}

static int DC_PVR_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    SDL_assert(data->target_transition_prepared);
    SDL_assert(data->prepared_target == texture);
    data->current_target = texture;
    data->prepared_target = NULL;
    data->target_transition_prepared = SDL_FALSE;
    data->header_valid = SDL_FALSE;
    return 0;
}

static int DC_PVR_BeginFrame(SDL_Renderer *renderer)
{
    return DC_PVR_BeginSceneForCurrentTarget(renderer);
}

int DC_PVR_EmitHeader(SDL_Renderer *renderer, SDL_Texture *texture,
                      SDL_BlendMode blend, int list_type)
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
            if (texturedata->access == SDL_TEXTUREACCESS_TARGET) {
                if (!texturedata->current_valid) {
                    return SDL_SetError("Dreamcast PVR target texture has no valid contents");
                }
            } else
            {
                return SDL_SetError("Dreamcast PVR texture has not been uploaded");
            }
        }
        if (data->header_valid &&
            data->header_list_type == list_type &&
            data->header_texture == texture &&
            data->header_blend == blend &&
            data->header_texture_env == texture_env &&
            data->header_filter == texturedata->filter &&
            data->header_stride_px == texturedata->stride_px) {
            return 0;
        }
        if (texturedata->layout == DC_PVR_TEXTURE_LAYOUT_STRIDED) {
            if (data->frame_stride_px != 0 &&
                data->frame_stride_px != texturedata->stride_px) {
                return SDL_SetError("Dreamcast PVR strided textures require one stride per frame");
            }
            if (data->frame_stride_px == 0) {
                pvr_txr_set_stride((size_t)texturedata->stride_px);
                if (pvr_txr_get_stride() != (size_t)texturedata->stride_px) {
                    return SDL_SetError("Dreamcast PVR stride register rejected the texture pitch");
                }
                data->frame_stride_px = texturedata->stride_px;
            }
        }
        pvr_poly_cxt_txr(&context, list_type,
                         texturedata->pvr_format,
                         texturedata->backing_w, texturedata->backing_h,
                         texturedata->sample_vram, texturedata->filter);
        /* SDL's packed vertex color modulates all four sampled channels. */
        context.txr.env = texture_env;
    } else {
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
    data->header_stride_px = texture ? ((DC_PVR_TextureData *)texture->driverdata)->stride_px : 0;
    data->header_list_type = list_type;
    data->header_valid = SDL_TRUE;
    if (texture) {
        DC_PVR_TextureData *texturedata = (DC_PVR_TextureData *)texture->driverdata;

        texturedata->referenced_in_scene = SDL_TRUE;
        texturedata->last_used_gen = data->scenes_begun;
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

    /* Ordered render targets are always enabled. */
    if (!data->ordered_targets_enabled && DC_PVR_BeginFrame(renderer) < 0) {
        return -1;
    }

    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_CLEAR:
            /* Ordered render targets are always enabled. */
            if (data->ordered_targets_enabled) {
                if (!data->current_target) {
                    data->ordered_frame_state = DC_PVR_SCREEN_PASS_STARTED;
                }
                if (DC_PVR_BeginFrame(renderer) < 0) {
                    return -1;
                }
            }
#if SDL_DC_PVR_OPAQUE_PREFIX_ROUTING
            if (DC_PVR_IsOpaquePrefixEligible(
                    SDL_BLENDMODE_NONE, NULL, data->opaque_prefix_open)) {
                if (DC_PVR_BeginList(renderer, PVR_LIST_OP_POLY) < 0)
                    return -1;
            } else {
                if (DC_PVR_BeginList(renderer, PVR_LIST_TR_POLY) < 0)
                    return -1;
                data->opaque_prefix_open = SDL_FALSE;
            }
#endif
            if (DC_PVR_EmitFullTargetClear(renderer, cmd) < 0) {
                return -1;
            }
            break;
        case SDL_RENDERCMD_GEOMETRY:
        case SDL_RENDERCMD_DRAW_POINTS:
        case SDL_RENDERCMD_FILL_RECTS:
            /* Ordered render targets are always enabled. */
            if (data->ordered_targets_enabled && cmd->data.draw.count == 0) {
                break;
            }
            if (data->ordered_targets_enabled && cmd->data.draw.count > 0) {
                if (!data->current_target) {
                    data->ordered_frame_state = DC_PVR_SCREEN_PASS_STARTED;
                }
                if (DC_PVR_BeginFrame(renderer) < 0) {
                    return -1;
                }
            }
            if (cmd->data.draw.count == 0)
                break;
#if SDL_DC_PVR_OPAQUE_PREFIX_ROUTING
            if (DC_PVR_IsOpaquePrefixEligible(
                    cmd->data.draw.blend,
                    cmd->data.draw.texture,
                    data->opaque_prefix_open)) {
                if (DC_PVR_BeginList(renderer, PVR_LIST_OP_POLY) < 0)
                    return -1;
            } else {
                if (DC_PVR_BeginList(renderer, PVR_LIST_TR_POLY) < 0)
                    return -1;
                data->opaque_prefix_open = SDL_FALSE;
            }
#endif
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

    /* Ordered render targets are always enabled. */
    if (data->ordered_targets_enabled) {
        if (!data->current_target && !data->scene_active &&
            DC_PVR_BeginFrame(renderer) < 0) {
            return -1;
        }
        if (data->scene_active && DC_PVR_FinishActiveScene(renderer) < 0) {
            return -1;
        }
        /* Present returns before the frame is rendered. Retired VRAM is released
         * once the scene that could have referenced it is proven complete. */
        DC_PVR_ClearTextureSceneReferences(renderer);
        DC_PVR_ReclaimCompleted(data);
        if (data->current_target) {
            SDL_SetError("Dreamcast PVR cannot present while a render target is selected; select the screen and draw it before presenting");
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", SDL_GetError());
            return -1;
        }
        data->ordered_frame_state = DC_PVR_TARGETS_ALLOWED;
        DC_PVR_ResetFrameStride(data);
#if SDL_DC_PVR_OPAQUE_PREFIX_ROUTING
        data->opaque_prefix_open = SDL_TRUE;
        data->active_list_type = -1;
#endif
        return 0;
    }
    if (DC_PVR_BeginFrame(renderer) < 0) {
        return -1;
    }
    if (DC_PVR_FinishCurrentList(renderer) < 0) {
        return -1;
    }
    if (pvr_scene_finish() < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("Could not finish the Dreamcast PVR scene");
    }
    data->scene_active = SDL_FALSE;
    data->header_valid = SDL_FALSE;
    DC_PVR_NoteSceneEntered(data);
#if SDL_DC_PVR_OPAQUE_PREFIX_ROUTING
    data->opaque_prefix_open = SDL_TRUE;
    data->active_list_type = -1;
#endif
    DC_PVR_ClearTextureSceneReferences(renderer);
    DC_PVR_ReclaimCompleted(data);
    DC_PVR_ResetFrameStride(data);
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
            data->scene_target = NULL;
            data->scene_kind = DC_PVR_SCENE_NONE;
            DC_PVR_NoteSceneEntered(data);
        }
    }
    /* Every submitted render must be complete before any VRAM is freed, whether
     * or not a scene was open: renders are no longer waited for at scene finish.
     * This is the blocking drain, and it precedes pvr_shutdown(), which
     * invalidates the PVR memory allocator. */
    if (data->pvr_owned && synchronized && !data->scene_active) {
        if (DC_PVR_WaitScenesComplete(data) < 0) {
            synchronized = SDL_FALSE;
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR cleanup synchronization failed");
        }
    }
    if (data->pvr_owned) {
        if (!synchronized) {
            if (pvr_shutdown() < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR recovery shutdown failed");
            }
            data->pvr_owned = SDL_FALSE;
            DC_PVR_ReclaimRetired(data, ~(Uint64)0, SDL_FALSE);
        } else {
            DC_PVR_ReclaimRetired(data, ~(Uint64)0, SDL_TRUE);
            /* Render completion precedes the vblank handler's page flip. Keep
             * that handler installed through one vblank before PVR shutdown. */
            vid_waitvbl();
            if (pvr_shutdown() < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Dreamcast PVR shutdown failed");
            }
            data->pvr_owned = SDL_FALSE;
        }
    } else {
        DC_PVR_ReclaimRetired(data, ~(Uint64)0, SDL_FALSE);
    }
    data->current_target = NULL;
    data->scene_target = NULL;
    data->scene_kind = DC_PVR_SCENE_NONE;
    data->header_valid = SDL_FALSE;
    SDL_free(data);
    renderer->driverdata = NULL;
}

static int DC_PVR_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, Uint32 flags)
{
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

    if (flags & SDL_RENDERER_SOFTWARE) {
        return SDL_SetError("Dreamcast PVR is not a software renderer");
    }
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
#if SDL_DC_PVR_OPAQUE_PREFIX_ROUTING
    /* Enable the opaque list for the prefix routing increment.  The
     * bin size is deliberately measured by the focused capacity test. */
    pvr_params.opb_sizes[0] = PVR_BINSIZE_16;
#endif
    if (pvr_init(&pvr_params) < 0) {
        SDL_free(data);
        return SDL_SetError("Could not initialize Dreamcast PVR (it may already be owned)");
    }
    SDL_Log("%s", DC_PVR_RENDERER_VERSION);
    data->pvr_owned = SDL_TRUE;
    data->vsync = SDL_TRUE;
    data->output_w = output_w;
    data->output_h = output_h;
    data->preferred_stride_px = output_w <= 992 ? output_w : 992;
    data->ordered_targets_enabled = SDL_TRUE;
    data->ordered_frame_state = DC_PVR_TARGETS_ALLOWED;
#if SDL_DC_PVR_OPAQUE_PREFIX_ROUTING
    data->opaque_prefix_open = SDL_TRUE;
    data->active_list_type = PVR_LIST_NONE;
#else
    data->active_list_type = PVR_LIST_TR_POLY;
#endif

    /* Ordered render targets are always enabled. The legacy DC_PVR_RENDER_TARGET_PROTOTYPE
     * has been retired. The renderer now renders directly to the physical screen.
     * Per-target VRAM is allocated only when a render target is created. */

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
    renderer->PrepareRenderTarget = DC_PVR_PrepareRenderTarget;
    renderer->SetRenderTarget = DC_PVR_SetRenderTarget;
    return 0;
}

#endif /* SDL_VIDEO_RENDER_DREAMCAST_PVR */
