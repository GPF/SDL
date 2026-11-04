/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Phase 0 private data for the native Dreamcast PVR renderer.
*/

#ifndef SDL_render_dreamcast_pvr_data_h_
#define SDL_render_dreamcast_pvr_data_h_

#include <dc/pvr.h>



typedef enum DC_PVR_OrderedFrameState
{
    DC_PVR_TARGETS_ALLOWED = 0,
    DC_PVR_SCREEN_PASS_STARTED
} DC_PVR_OrderedFrameState;

typedef struct DC_PVR_Vertex
{
    float x;
    float y;
    float u;
    float v;
    Uint32 color;
} DC_PVR_Vertex;

typedef enum DC_PVR_TextureLayout
{
    DC_PVR_TEXTURE_LAYOUT_POT = 0,
    DC_PVR_TEXTURE_LAYOUT_STRIDED
} DC_PVR_TextureLayout;

typedef struct DC_PVR_TextureData
{
    struct DC_PVR_TextureData *next_deferred; /* retire list link */
    Uint64 retire_gen;            /* last scene that could reference this VRAM */
    Uint64 last_used_gen;         /* latest scene that sampled this texture */
    void *pixels;
    pvr_ptr_t vram;
    pvr_ptr_t sample_vram;
    pvr_ptr_t lock_replacement_vram;
    struct DC_PVR_DeferredVRAM *lock_deferred_vram;
    size_t allocation_size;
    Uint32 sdl_format;
    int pvr_format;
    int logical_w;
    int logical_h;
    int backing_w;
    int backing_h;
    int cpu_pitch;
    int vram_pitch;
    int stride_px;
    DC_PVR_TextureLayout layout;
    SDL_TextureAccess access;
    pvr_filter_mode_t filter;
    SDL_bool uploaded;
    SDL_bool dirty;
    SDL_bool referenced_in_scene;
    SDL_bool locked;
    SDL_bool preformatted;
    SDL_Rect locked_rect;
    int rtt_render_w;
    int rtt_render_h;
    int rtt_stride_px;
    pvr_ptr_t vram_next;
    SDL_bool current_valid;
} DC_PVR_TextureData;

typedef struct DC_PVR_DeferredVRAM
{
    struct DC_PVR_DeferredVRAM *next;
    pvr_ptr_t vram;
    Uint64 retire_gen;            /* last scene that could reference this VRAM */
} DC_PVR_DeferredVRAM;

typedef struct DC_PVR_RenderData
{
    SDL_bool pvr_owned;
    SDL_bool scene_active;
    SDL_bool list_active;
    SDL_bool header_valid;
    SDL_bool vsync;
    SDL_bool pvr_failed;
    int output_w;
    int output_h;
    int view_x;
    int view_y;
    int view_w;
    int view_h;
    SDL_bool viewport_set;
    SDL_bool has_viewport;
    SDL_Rect clip_rect;
    SDL_bool has_cliprect;
    SDL_Texture *header_texture;
    SDL_BlendMode header_blend;
    pvr_txr_shading_mode_t header_texture_env;
    pvr_filter_mode_t header_filter;
    int header_stride_px;
    int header_list_type;
    int active_list_type;
    int preferred_stride_px;
    int frame_stride_px;
    Uint32 stride_generation;
    /* Texture VRAM retirement. Scenes are numbered when they begin; a scene
     * "enters" the Tile Accelerator at its first pvr_list_begin(), which is
     * where KallistiOS waits for the previous render to have started. Retired
     * textures and replacement VRAM are tagged with the last scene that could
     * reference them and freed once that scene's render is proven complete
     * without waiting. See DC_PVR_CompletedScenes(). */
    DC_PVR_TextureData *retired_textures;
    DC_PVR_DeferredVRAM *retired_vram;
    Uint64 scenes_begun;
    Uint64 scenes_entered;
    Uint64 scenes_completed; /* highest scene proven complete; monotonic */
    SDL_bool scene_entered;      /* the open scene has entered the TA */
    SDL_Texture *current_target;
    SDL_Texture *scene_target;
    int scene_kind;
#if SDL_DC_PVR_OPAQUE_PREFIX_ROUTING
    SDL_bool opaque_prefix_open;
#endif
    SDL_Texture *prepared_target;
    DC_PVR_OrderedFrameState ordered_frame_state;
    SDL_bool ordered_targets_enabled;
    SDL_bool target_transition_prepared;
} DC_PVR_RenderData;

#endif /* SDL_render_dreamcast_pvr_data_h_ */
