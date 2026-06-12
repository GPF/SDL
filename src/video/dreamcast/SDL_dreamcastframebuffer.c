/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_DREAMCAST
#include <kos.h> 
#include "../SDL_sysvideo.h"
#include "SDL_dreamcastframebuffer_c.h"
#include <SDL3/SDL_hints.h>
#include "video/SDL_sysvideo.h"
#include "../../SDL_properties_c.h"
#include "../../SDL_utils_c.h"
#include "../../events/SDL_mouse_c.h"

#define DREAMCAST_SURFACE "SDL.internal.window.surface"

int sdl_dc_pvr_inited =0;
static int sdl_dc_textured=0;
static int sdl_dc_width=0;
static int sdl_dc_height=0;
static int sdl_dc_bpp=0;
static int sdl_dc_wtex=0;
static int sdl_dc_htex=0;
static int sdl_dc_pvr_wtex=0;
static int sdl_dc_pvr_htex=0;
static int sdl_dc_tex_bytes=0;
static int sdl_dc_strided=0;
static uint32_t sdl_dc_txr_format=0;
static pvr_ptr_t sdl_dc_memtex;
static unsigned short *sdl_dc_buftex;
static void *sdl_dc_memfreed;
static void *sdl_dc_dblfreed=NULL;
static void *sdl_dc_dblmem=NULL;
static unsigned sdl_dc_dblsize=0;
static float sdl_dc_u1=0.3f;
static float sdl_dc_u2=0.3f;
static float sdl_dc_v1=0.9f;
static float sdl_dc_v2=0.6f;
extern unsigned int __sdl_dc_mouse_shift;
SDL_Surface *cursorSurface = NULL;

static bool sdl_dc_is_textured_mode(const char *video_mode_hint)
{
    return video_mode_hint &&
           (SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0 ||
            SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_STRIDED_VIDEO") == 0);
}

static bool sdl_dc_is_strided_textured_mode(const char *video_mode_hint)
{
    return video_mode_hint &&
           SDL_strcmp(video_mode_hint, "SDL_DC_TEXTURED_STRIDED_VIDEO") == 0;
}
/* XPM */
static const char *arrow[] = {
    /* width height num_colors chars_per_pixel */
    "    32    32        3            1",
    /* colors */
    "X c #000000",
    ". c #ffffff",
    "  c None",
    /* pixels */
    "X                               ",
    "XX                              ",
    "X.X                             ",
    "X..X                            ",
    "X...X                           ",
    "X....X                          ",
    "X.....X                         ",
    "X......X                        ",
    "X.......X                       ",
    "X........X                      ",
    "X.....XXXXX                     ",
    "X..X..X                         ",
    "X.X X..X                        ",
    "XX  X..X                        ",
    "X    X..X                       ",
    "     X..X                       ",
    "      X..X                      ",
    "      X..X                      ",
    "       XX                       ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "0,0"
};

static SDL_Surface *init_system_cursor(const char *image[]) {
    SDL_Surface *cursor_surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!cursor_surface) {
        SDL_Log("Failed to create cursor surface: %s", SDL_GetError());
        return NULL;
    }

    const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
    if (!details) {
        SDL_Log("Failed to get pixel format details: %s", SDL_GetError());
        SDL_DestroySurface(cursor_surface);
        return NULL;
    }

    Uint32 *pixels = (Uint32 *)cursor_surface->pixels;

    for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < 32; ++col) {
            char c = image[4 + row][col];
            Uint32 color = 0;

            switch (c) {
                case 'X':
                    color = SDL_MapRGBA(details, NULL, 0, 0, 0, 255);  // black
                    break;
                case '.':
                    color = SDL_MapRGBA(details, NULL, 255, 255, 255, 255);  // white
                    break;
                default:
                    color = SDL_MapRGBA(details, NULL, 0, 0, 0, 0);  // transparent
                    break;
            }

            pixels[row * 32 + col] = color;
        }
    }

    return cursor_surface;
}

static void sdl_dc_blit_textured(void)
{
#define DX1 0.0f
#define DY1 0.0f
#define DZ1 1.0f
#define DWI 640.0f
#define DHE 480.0f

    pvr_poly_hdr_t *hdr;
    pvr_vertex_t *vert;
    pvr_poly_cxt_t cxt;
    pvr_dr_state_t dr_state;

    if (SDL_GetHintBoolean(SDL_HINT_RENDER_VSYNC, true)) {
            pvr_wait_ready();
    }
            // SDL_Log("sdl_dc_blit_textured");
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);

    if (sdl_dc_buftex)
    {
        dcache_flush_range((unsigned)sdl_dc_buftex, sdl_dc_tex_bytes);
        while (!pvr_dma_ready());
        pvr_txr_load_dma(sdl_dc_buftex, sdl_dc_memtex, sdl_dc_tex_bytes, -1, NULL, 0);
        // pvr_txr_load(sdl_dc_buftex, sdl_dc_memtex, sdl_dc_tex_bytes);
    }

    pvr_dr_init(&dr_state);
    if (sdl_dc_strided) {
        pvr_txr_set_stride(sdl_dc_wtex);
    }
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, sdl_dc_txr_format, sdl_dc_pvr_wtex, sdl_dc_pvr_htex, sdl_dc_memtex, PVR_FILTER_NEAREST);

    hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
    pvr_poly_compile(hdr, &cxt);
    pvr_dr_commit(hdr);

    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = DX1; vert->y = DY1; vert->z = DZ1; vert->u = sdl_dc_u1; vert->v = sdl_dc_v1;
    pvr_dr_commit(vert);

    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = DX1+DWI; vert->y = DY1; vert->z = DZ1; vert->u = sdl_dc_u2; vert->v = sdl_dc_v1;
    pvr_dr_commit(vert);

    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = DX1; vert->y = DY1+DHE; vert->z = DZ1; vert->u = sdl_dc_u1; vert->v = sdl_dc_v2;
    pvr_dr_commit(vert);

    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX_EOL;
    vert->x = DX1+DWI; vert->y = DY1+DHE; vert->z = DZ1; vert->u = sdl_dc_u2; vert->v = sdl_dc_v2;
    pvr_dr_commit(vert);

    // pvr_dr_finish();
    pvr_list_finish();
    pvr_scene_finish();

#undef DX1
#undef DY1
#undef DZ1
#undef DWI
#undef DHE
}

static unsigned short *sdl_dc_buf[2] = { NULL, NULL };
static int sdl_dc_buf_index = 0;
bool SDL_DREAMCAST_CreateWindowFramebuffer(SDL_VideoDevice *device, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    SDL_Surface *surface = NULL;
    int w, h;
    Uint32 surface_format = *format;
    int target_bpp;
    size_t surface_size = 0;
    size_t expected_pitch = 0;
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    const char *double_buffer_hint = SDL_GetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER);

    SDL_GetWindowSizeInPixels(window, &w, &h);
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    if (!mode) {
        SDL_SetError("Failed to get current display mode");
        return false;
    }

    if (surface_format != SDL_PIXELFORMAT_RGB565 &&
        surface_format != SDL_PIXELFORMAT_ARGB1555 &&
        surface_format != SDL_PIXELFORMAT_XRGB8888 &&
        surface_format != SDL_PIXELFORMAT_ARGB8888) {
        surface_format = SDL_PIXELFORMAT_ARGB1555;
    }

    // if (video_mode_hint && strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
    //     target_bpp = 16;
    //     surface_format = SDL_PIXELFORMAT_RGB565;

    //     const char *w_hint = SDL_GetHint(SDL_HINT_DC_SCREEN_WIDTH_TEXTURED);
    //     const char *h_hint = SDL_GetHint(SDL_HINT_DC_SCREEN_HEIGHT_TEXTURED);
    //     int logical_w = w_hint ? SDL_atoi(w_hint) : 640;
    //     int logical_h = h_hint ? SDL_atoi(h_hint) : 480;

    //     sdl_dc_width = logical_w;
    //     sdl_dc_height = logical_h;

    //     sdl_dc_wtex = 1 << (32 - __builtin_clz(logical_w - 1));
    //     sdl_dc_htex = 1 << (32 - __builtin_clz(logical_h - 1));
    //     __sdl_dc_mouse_shift = 640.0f / (float)sdl_dc_width;

    //     // Calculate pitch and size
    //     if (!SDL_CalculateSurfaceSize(surface_format, sdl_dc_wtex, sdl_dc_htex, &surface_size, &expected_pitch, false)) {
    //         return SDL_SetError("Surface size calc failed");
    //     }

    //     surface = SDL_CreateSurface(sdl_dc_wtex, sdl_dc_htex, surface_format);
    //     if (!surface) {
    //         return SDL_SetError("Failed to create surface: %s", SDL_GetError());
    //     }
    //     surface->pitch = expected_pitch;
    //     SDL_SetWindowSize(window, sdl_dc_wtex, sdl_dc_htex);

    // } 
    // else 
    {
        target_bpp = SDL_BITSPERPIXEL(surface_format);
        if (target_bpp == 24) target_bpp = 32;

        if (!SDL_CalculateSurfaceSize(surface_format, w, h, &surface_size, &expected_pitch, false)) {
            return SDL_SetError("Surface size calc failed");
        }

        surface = SDL_CreateSurface(w, h, surface_format);
        if (!surface) {
            return SDL_SetError("Failed to create surface: %s", SDL_GetError());
        }
        surface->pitch = expected_pitch;
    }

    if (video_mode_hint && strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
        if (!sdl_dc_pvr_inited) {
            pvr_dma_init();
            sdl_dc_pvr_inited = 1;
        }

        sdl_dc_dblfreed = calloc(128 + surface_size, 1);
        sdl_dc_dblmem = (unsigned short *)(((((uintptr_t)sdl_dc_dblfreed) + 64) / 64) * 64);
        surface->pixels = (void *)sdl_dc_dblmem;
        __sdl_dc_mouse_shift = 1.0f;

    } else if (video_mode_hint && strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0) {
        if (!sdl_dc_pvr_inited) {
            sdl_dc_pvr_inited = 1;
        }

        __sdl_dc_mouse_shift = 640.0f / (float)w;

        if (double_buffer_hint && SDL_GetHintBoolean(SDL_HINT_VIDEO_DOUBLE_BUFFER, true)) {
            sdl_dc_dblfreed = calloc(2 * surface_size + 128, 1);
            sdl_dc_buf[0] = (unsigned short *)((((uintptr_t)sdl_dc_dblfreed) + 64) & ~63);
            surface->pixels = (void *)sdl_dc_buf[0];
            sdl_dc_buf_index = 0;
        } else {
            surface->pixels = (void *)vram_l;
        }
    }

    // if (sdl_dc_pvr_inited != 1 && video_mode_hint && strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
    //     pvr_init_defaults();
    //     pvr_dma_init();
    //     sdl_dc_pvr_inited = 1;
    //     sdl_dc_textured = 1;
    //     sdl_dc_bpp = SDL_BITSPERPIXEL(surface_format);

    //     sdl_dc_memtex = pvr_mem_malloc(sdl_dc_wtex * sdl_dc_htex * 2);
    //     sdl_dc_u1 = 0.0f;
    //     sdl_dc_v1 = 0.0f;
    //     sdl_dc_u2 = (float)sdl_dc_width / (float)sdl_dc_wtex;
    //     sdl_dc_v2 = (float)sdl_dc_height / (float)sdl_dc_htex;

    //     if (double_buffer_hint && SDL_GetHintBoolean(SDL_HINT_VIDEO_DOUBLE_BUFFER, true)) {
    //         sdl_dc_memfreed = calloc(64 + surface_size, 1);
    //         sdl_dc_buftex = (unsigned short *)((((uintptr_t)sdl_dc_memfreed + 32) / 32) * 32);
    //         surface->pixels = (void *)sdl_dc_buftex;
    //     } else {
    //         sdl_dc_buftex = NULL;
    //         surface->pixels = sdl_dc_memtex;
    //     }
    // }

    *format = surface_format;
    *pixels = surface->pixels;
    *pitch  = surface->pitch;

    SDL_SetSurfaceProperty(SDL_GetWindowProperties(window), DREAMCAST_SURFACE, surface);
    SDL_Log("SDL_DREAMCAST_CreateWindowFramebuffer: Done. Size %d x %d, pitch %d", surface->w, surface->h, *pitch);
    return true;
}


bool SDL_DREAMCAST_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_Surface *surface = (SDL_Surface *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), DREAMCAST_SURFACE, NULL);
    if (!surface) return SDL_SetError("Couldn't find framebuffer surface for window");

    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    bool double_buffer = SDL_GetHintBoolean(SDL_HINT_VIDEO_DOUBLE_BUFFER, true);
    bool vsync_enabled = SDL_GetHintBoolean(SDL_HINT_RENDER_VSYNC, true);

    int w = surface->w;
    int h = surface->h;
    int pitch = surface->pitch;

    // if (video_mode_hint && strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") == 0) {
    //     if (numrects == 1) {
    //         sdl_dc_blit_textured();
    //     }
    // } else 
    if (strcmp(video_mode_hint, "SDL_DC_DMA_VIDEO") == 0) {
        if (numrects == 1) {
            dcache_flush_range((uintptr_t)sdl_dc_dblmem, sdl_dc_dblsize);
            while (!pvr_dma_ready());
            pvr_dma_transfer((void *)sdl_dc_dblmem, (uintptr_t)vram_l, sdl_dc_dblsize, PVR_DMA_VRAM32, -1, NULL, NULL);
        }
    } else if (strcmp(video_mode_hint, "SDL_DC_DIRECT_VIDEO") == 0) {
        if (double_buffer) {
            sq_cpy(vram_l, sdl_dc_buf[sdl_dc_buf_index], h * pitch);
            // vid_waitvbl();
            // sdl_dc_buf_index ^= 1;
            surface->pixels = sdl_dc_buf[sdl_dc_buf_index];
        } else {
            sq_cpy(vram_l, surface->pixels, h * pitch);
            // vid_waitvbl();
        }
    }

    // if (video_mode_hint && strcmp(video_mode_hint, "SDL_DC_TEXTURED_VIDEO") != 0 && vsync_enabled) {
    //     vid_waitvbl();
    // }

    return true;
}


void SDL_DREAMCAST_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_Surface *surface = (SDL_Surface *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), DREAMCAST_SURFACE, NULL);
    if (surface) {
        SDL_DestroySurface(surface);
        SDL_ClearProperty(SDL_GetWindowProperties(window), DREAMCAST_SURFACE);
    }

    // Only free DMA-related allocations
    if (sdl_dc_dblfreed) {
        free(sdl_dc_dblfreed);
        sdl_dc_dblfreed = NULL;
        sdl_dc_dblmem = NULL;
        sdl_dc_dblsize = 0;
    }

    // DO NOT free sdl_dc_memfreed or sdl_dc_memtex here,
    // those are managed by DREAMCAST_DestroyWindowTexture()

    // Reset DMA/DIRECT related state
    sdl_dc_buf[0] = NULL;
    sdl_dc_buf[1] = NULL;
    sdl_dc_buf_index = 0;
}


bool DREAMCAST_CreateWindowTexture(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    const char *double_buffer_hint = SDL_GetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER);
    const char *video_mode_hint = SDL_GetHint(SDL_HINT_DC_VIDEO_MODE);
    int logical_w = SDL_atoi(SDL_GetHint(SDL_HINT_DC_SCREEN_WIDTH_TEXTURED) ?: "640");
    int logical_h = SDL_atoi(SDL_GetHint(SDL_HINT_DC_SCREEN_HEIGHT_TEXTURED) ?: "480");
    int w, h;
    const bool strided = sdl_dc_is_strided_textured_mode(video_mode_hint) && ((logical_w % 32) == 0);
    const int padded_w = 1 << (32 - __builtin_clz(logical_w - 1));
    const int padded_h = 1 << (32 - __builtin_clz(logical_h - 1));
    sdl_dc_width = logical_w;
    sdl_dc_height = logical_h;
    sdl_dc_wtex = strided ? logical_w : padded_w;
    sdl_dc_htex = strided ? logical_h : padded_h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    *format = SDL_PIXELFORMAT_RGB565;
    sdl_dc_bpp = SDL_BITSPERPIXEL(*format);
    size_t tex_size = sdl_dc_wtex * sdl_dc_htex * (sdl_dc_bpp >> 3);

    pvr_init_defaults();
    pvr_dma_init();
    sdl_dc_pvr_inited = 1;
    sdl_dc_textured = 1;
    sdl_dc_strided = strided ? 1 : 0;
    sdl_dc_pvr_wtex = padded_w;
    sdl_dc_pvr_htex = padded_h;
    sdl_dc_tex_bytes = tex_size;
    sdl_dc_txr_format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
    if (sdl_dc_strided) {
        sdl_dc_txr_format |= PVR_TXRFMT_X32_STRIDE;
    }

    sdl_dc_memtex = pvr_mem_malloc(tex_size);
    sdl_dc_u1 = 0.0f;
    sdl_dc_v1 = 0.0f;
    sdl_dc_u2 = (float)sdl_dc_width / (float)sdl_dc_pvr_wtex;
    sdl_dc_v2 = (float)sdl_dc_height / (float)sdl_dc_pvr_htex;

    if (SDL_GetHintBoolean(SDL_HINT_VIDEO_DOUBLE_BUFFER, true)) {
        sdl_dc_memfreed = calloc(64 + tex_size, 1);
        sdl_dc_buftex = (unsigned short *)((((uintptr_t)sdl_dc_memfreed + 32) / 32) * 32);
        *pixels = (void *)sdl_dc_buftex;
    } else {
        sdl_dc_buftex = NULL;
        *pixels = (void *)sdl_dc_memtex;
    }

    *pitch = sdl_dc_wtex * (sdl_dc_bpp >> 3);
    // *pitch = 1280;
    __sdl_dc_mouse_shift = 640.0f / (float)sdl_dc_width;

    SDL_Log("DREAMCAST_CreateWindowTexture: Logical %dx%d, tex size %dx%d%s, pitch %d",
            sdl_dc_width, sdl_dc_height, sdl_dc_wtex, sdl_dc_htex,
            sdl_dc_strided ? " (strided)" : "", *pitch);
    // SDL_SetWindowSize(window, sdl_dc_wtex, sdl_dc_htex);

    return true;
}


bool DREAMCAST_UpdateWindowTexture(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    // SDL_Log("DREAMCAST_UpdateWindowTexture: numrects=%d", numrects);
    if (numrects == 1) {
        sdl_dc_blit_textured();
    }
    return true;
}


void DREAMCAST_DestroyWindowTexture(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_Log("DREAMCAST_DestroyWindowTexture: freeing VRAM and buffers");
    if (sdl_dc_memfreed) {
        SDL_free(sdl_dc_memfreed);
        sdl_dc_memfreed = NULL;
    }
    if (sdl_dc_memtex) {
        pvr_mem_free(sdl_dc_memtex);
        sdl_dc_memtex = NULL;
    }
    sdl_dc_pvr_wtex = 0;
    sdl_dc_pvr_htex = 0;
    sdl_dc_tex_bytes = 0;
    sdl_dc_strided = 0;
    sdl_dc_txr_format = 0;
}


#endif /* SDL_VIDEO_DRIVER_DREAMCAST */

/* vi: set ts=4 sw=4 expandtab: */
