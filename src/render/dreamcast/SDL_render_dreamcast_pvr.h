/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Private declarations shared by the native Dreamcast PVR renderer subsystems.
*/

#ifndef SDL_render_dreamcast_pvr_h_
#define SDL_render_dreamcast_pvr_h_

#include "../SDL_sysrender.h"
#include "SDL_render_dreamcast_pvr_data.h"

/* Centralized renderer identity. Used by SDL_render.c to route PVR-specific
 * behavior (blend-mode override, scale-mode filter gate) to the native PVR
 * renderer without widening a global SDL semantic change. If the advertised
 * renderer name changes, update this constant and every #if check that uses
 * it. Do not hard-code the literal in multiple translation units. */
#define DC_PVR_RENDERER_NAME "Dreamcast PVR"

int DC_PVR_Submit(const void *data, size_t size);
int DC_PVR_EmitHeader(SDL_Renderer *renderer, SDL_Texture *texture,
                      SDL_BlendMode blend);

int DC_PVR_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                           const SDL_FPoint *points, int count);
int DC_PVR_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                          const SDL_FRect *rects, int count);
int DC_PVR_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                         SDL_Texture *texture, const float *xy, int xy_stride,
                         const SDL_Color *color, int color_stride,
                         const float *uv, int uv_stride, int num_vertices,
                         const void *indices, int num_indices, int size_indices,
                         float scale_x, float scale_y);
int DC_PVR_EmitFullTargetClear(SDL_Renderer *renderer,
                               const SDL_RenderCommand *cmd);
int DC_PVR_RunGeometryCommand(SDL_Renderer *renderer,
                              const SDL_RenderCommand *cmd,
                              const void *vertices);

int DC_PVR_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture);
int DC_PVR_CreateDreamcastTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                                  const SDL_DreamcastTextureData *dt);
int DC_PVR_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                         const SDL_Rect *rect, const void *pixels, int pitch);
int DC_PVR_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                       const SDL_Rect *rect, void **pixels, int *pitch);
void DC_PVR_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture);
void DC_PVR_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture,
                                SDL_ScaleMode scaleMode);
void DC_PVR_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture);
void DC_PVR_FreeTextureData(DC_PVR_TextureData *data, SDL_bool free_vram);

#endif /* SDL_render_dreamcast_pvr_h_ */
