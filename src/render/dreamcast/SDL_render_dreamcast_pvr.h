/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Private declarations shared by the native Dreamcast PVR renderer subsystems.
*/

#ifndef SDL_render_dreamcast_pvr_h_
#define SDL_render_dreamcast_pvr_h_

#include "../SDL_sysrender.h"
#include "SDL_render_dreamcast_pvr_data.h"

/* Centralized renderer identity. SDL_CreateRenderer matches this name against
 * SDL_HINT_RENDER_DRIVER, so the Dreamcast video layer uses it to select the
 * native PVR renderer as the default driver. Keep the literal in one place. */
#define DC_PVR_RENDERER_NAME "Dreamcast PVR"

/* ------------------------------------------------------------------ *
 * SDL3 adapter-boundary conversion helpers.
 *
 * These are the only two places where SDL3 renderer semantics are turned
 * into PVR hardware state. Low-level PVR code below this boundary works on
 * packed bytes and framebuffer pixels, not on SDL3 types.
 * ------------------------------------------------------------------ */

/* Float color policy (applies SDL3 color_scale to RGB only, never alpha):
 *   value <= 0.0f          -> 0
 *   value >= 1.0f          -> 255
 *   otherwise              -> nearest 8-bit value (round half up)
 * Out-of-range values clamp; they never wrap. HDR values are not preserved.
 * Non-finite input resolves to 0 rather than wrapping.
 */
Uint32 DC_PVR_PackColor(const SDL_FColor *color, float color_scale);

/* Coordinate contract derived from SDL3 core (src/render/SDL_render.c):
 *
 *   view->pixel_viewport  = floor(logical * current_scale + logical_offset)
 *                           -> framebuffer pixel space
 *   view->pixel_clip_rect = ceil(clip * current_scale), NOT offset by the
 *                           viewport origin or logical_offset
 *                           -> viewport-relative pixel space
 *
 * Queue callbacks receive logical coordinates. Geometry is passed unscaled and
 * receives current_scale as scale_x/scale_y, so scaling happens at queue time.
 * Fill rects are already scaled by core before QueueFillRects, and points only
 * reach QueueDrawPoints when current_scale is exactly 1.0.
 *
 * So the single conversion is:
 *   framebuffer pixel = pixel_viewport origin + (queued viewport-relative pixel)
 *
 * Every emission path uses this helper; no per-primitive ad hoc math.
 */
void DC_PVR_TranslateToFramebuffer(const DC_PVR_RenderData *data, float *x, float *y);

/* Effective drawing bounds: pixel_viewport intersected with the viewport-relative
 * clip rect when clipping is enabled. Clear ignores both, per SDL3 docs. */

bool DC_PVR_Submit(const void *data, size_t size);
bool DC_PVR_EmitHeader(SDL_Renderer *renderer, SDL_Texture *texture,
                       SDL_BlendMode blend, SDL_ScaleMode scale_mode, int list_type);

bool DC_PVR_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                            const SDL_FPoint *points, int count);
bool DC_PVR_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                           const SDL_FPoint *points, int count);
bool DC_PVR_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                           const SDL_FRect *rects, int count);
bool DC_PVR_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                          SDL_Texture *texture, const float *xy, int xy_stride,
                          const SDL_FColor *color, int color_stride,
                          const float *uv, int uv_stride, int num_vertices,
                          const void *indices, int num_indices, int size_indices,
                          float scale_x, float scale_y);
bool DC_PVR_EmitClear(SDL_Renderer *renderer, const SDL_RenderCommand *cmd);
bool DC_PVR_RunGeometryCommand(SDL_Renderer *renderer,
                               const SDL_RenderCommand *cmd,
                               const void *vertices);

bool DC_PVR_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                          SDL_PropertiesID create_props);
SDL_Texture *DC_PVR_LoadDreamcastTexture_IO(SDL_Renderer *renderer,
                                           SDL_IOStream *src, bool closeio);
#if defined(SDL_VIDEO_RENDER_OGL)
SDL_Texture *SDL_GL_LoadDreamcastTexture_IO(SDL_Renderer *renderer,
                                           SDL_IOStream *src, bool closeio);
#endif
bool DC_PVR_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                          const SDL_Rect *rect, const void *pixels, int pitch);
bool DC_PVR_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                        const SDL_Rect *rect, void **pixels, int *pitch);
void DC_PVR_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture);
void DC_PVR_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture);
void DC_PVR_FreeTextureData(DC_PVR_TextureData *data);

/* Texture VRAM lifetime (SDL_render_dreamcast_pvr.c). */
void DC_PVR_RetireTexture(SDL_Renderer *renderer, DC_PVR_TextureData *texture);
bool DC_PVR_WaitTextureIdle(SDL_Renderer *renderer, DC_PVR_TextureData *texture);
bool DC_PVR_ReclaimForAllocation(SDL_Renderer *renderer);

#endif /* SDL_render_dreamcast_pvr_h_ */
