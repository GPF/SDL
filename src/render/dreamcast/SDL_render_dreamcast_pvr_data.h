/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Phase 0 private data for the native Dreamcast PVR renderer.
*/

#ifndef SDL_render_dreamcast_pvr_data_h_
#define SDL_render_dreamcast_pvr_data_h_

#include <dc/pvr.h>

#ifdef SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS
#define DC_PVR_TEST_FAIL_NEXT_TARGET_SWITCH "SDL.renderer.dreamcast.pvr.test_fail_next_target_switch"
#define DC_PVR_TEST_FAIL_VRAM_ALLOCATION "SDL.renderer.dreamcast.pvr.test_fail_vram_allocation"
#define DC_PVR_TEST_VRAM_ALLOCATION_COUNT "SDL.renderer.dreamcast.pvr.test_vram_allocation_count"
#define DC_PVR_TEST_FAIL_SCREEN_ALLOCATION "SDL.renderer.dreamcast.pvr.test_fail_screen_allocation"
#endif

/* Renderer-owned compact vertex.
 *
 * x/y hold viewport-relative pixels (logical coordinates already multiplied by
 * view->current_scale). The emission path adds the pixel viewport origin, so
 * the same stored vertex is valid for any viewport change queued after it.
 * u/v hold PVR texture coordinates already scaled to the backing dimensions.
 * For fill rects, u/v temporarily hold the rectangle width and height.
 */
typedef struct DC_PVR_Vertex
{
    float x;
    float y;
    float u;
    float v;
    Uint32 color;
} DC_PVR_Vertex;

typedef struct DC_PVR_TextureData
{
    struct DC_PVR_TextureData *next_deferred;
    void *pixels;                 /* CPU staging backing store */
    pvr_ptr_t vram;               /* PVR VRAM backing store */
    pvr_ptr_t sample_vram;         /* PVR sampling address (VQ codebook offset) */
    pvr_ptr_t vram_next;          /* Preservation destination for render targets */
    size_t allocation_size;
    SDL_PixelFormat sdl_format;
    int pvr_format;
    int logical_w;
    int logical_h;
    int backing_w;
    int backing_h;
    int cpu_pitch;
    SDL_TextureAccess access;
    pvr_filter_mode_t filter;
    bool uploaded;
    bool current_valid;
    bool referenced_in_scene;
    bool locked;
    SDL_Rect locked_rect;
} DC_PVR_TextureData;

typedef enum DC_PVR_SceneKind
{
    DC_PVR_SCENE_NONE,
    DC_PVR_SCENE_LOGICAL_SCREEN,
    DC_PVR_SCENE_TARGET,
    DC_PVR_SCENE_PHYSICAL
} DC_PVR_SceneKind;

typedef struct DC_PVR_RenderData
{
    bool pvr_owned;
    bool scene_active;
    bool list_active;
    bool header_valid;
    bool pvr_failed;
    int output_w;
    int output_h;
    int render_w;
    int render_h;

#if SDL_VIDEO_RENDER_DREAMCAST_PVR
    SDL_Texture *current_target;
    SDL_Texture *scene_target;
    DC_PVR_SceneKind scene_kind;
    pvr_ptr_t screen_vram;
    pvr_ptr_t screen_vram_next;
    size_t screen_allocation_size;
    int screen_backing_w;
    int screen_backing_h;
    int screen_stride_px;
    bool screen_current_valid;
#endif

    /* Pixel-space state captured from SDL3 commands.
     * pixel_viewport is framebuffer space and already includes logical_offset.
     * pixel_clip_rect is scaled but viewport-relative. */
    SDL_Rect pixel_viewport;
    SDL_Rect pixel_clip_rect;
    bool clipping_enabled;

    /* Polygon header cache identity. */
    SDL_Texture *header_texture;
    SDL_BlendMode header_blend;
    SDL_ScaleMode header_scale_mode;
    pvr_txr_shading_mode_t header_texture_env;
    pvr_filter_mode_t header_filter;
    int header_list_type;
    int active_list_type;

    /* Textures destroyed while a scene is open cannot have their VRAM freed
     * until presentation completes. */
    DC_PVR_TextureData *deferred_textures;
} DC_PVR_RenderData;

#endif /* SDL_render_dreamcast_pvr_data_h_ */
