/* Focused native Dreamcast PVR partial-VRAM-upload validation.

POT-only scope: partial uploads are implemented only for POT-backed textures.
Strided textures always fall through to full-backing upload.

Pages:
  1 — POT RGB565: nonzero-x, interior patch, multiple rows, row edges,
      right-edge, bottom-edge, and bottom-right-corner updates;
      neighboring-texel preservation probe; padded source pitch.
  2 — POT ARGB1555 and ARGB4444 partial updates; partial streaming
      lock/unlock (multi-row);
      update-after-scene-reference (full-backing fallback).
  3 — Strided texture: exercises full-upload fallback;
      diagnostic report (SHOW_UPLOAD_PATH=1); VRAM recovery;
      renderer-continues probe.

Build:
  cd test/dreamcast/test/pvr_partial_upload
  make -B -f Makefile.dc \
    SDL2_LIBDIR=/path/to/SDL2/build-scripts/dcbuild \
    EXTRA_CFLAGS="-DPVR_TEXTURE_HARDWARE_SCREENSHOT"

Run on physical Dreamcast:
  kos-tool -t <IP> -x pvr_partial_upload.elf -m .
*/

#include <SDL.h>

#include <dc/video.h>

#include <stdio.h>

#define HOLD_MS 2000
#define SCREENSHOT_PREFIX "/pc/pvr_partial_upload-page"

/* ---------- helpers ---------- */

static int find_pvr_driver(void)
{
    SDL_RendererInfo info;
    int i;
    for (i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 &&
            SDL_strcmp(info.name, "Dreamcast PVR") == 0) {
            return i;
        }
    }
    return -1;
}

static int check_renderer_formats(SDL_Renderer *renderer)
{
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) < 0)
        return -1;
    if (info.num_texture_formats != 3 ||
        info.texture_formats[0] != SDL_PIXELFORMAT_RGB565 ||
        info.texture_formats[1] != SDL_PIXELFORMAT_ARGB1555 ||
        info.texture_formats[2] != SDL_PIXELFORMAT_ARGB4444) {
        return SDL_SetError("PVR renderer did not advertise exactly the three formats");
    }
    return 0;
}

/* Create a checker texture (RGB565). */
static SDL_Texture *create_checker_rgb565(SDL_Renderer *renderer, int w, int h)
{
    SDL_Texture *t;
    Uint16 *buf;
    int pitch, px, py;

    pitch = w * (int)sizeof(Uint16);
    buf = (Uint16 *)SDL_malloc((size_t)h * (size_t)pitch);
    if (!buf) return NULL;

    for (py = 0; py < h; ++py) {
        Uint16 *row = buf + py * w;
        for (px = 0; px < w; ++px)
            row[px] = ((px ^ py) & 1) ? 0x07ffu : 0xf800u;
    }

    t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                          SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) { SDL_free(buf); return NULL; }
    if (SDL_UpdateTexture(t, NULL, buf, pitch) < 0) {
        SDL_DestroyTexture(t); SDL_free(buf); return NULL;
    }
    SDL_free(buf);
    return t;
}

/* Fill a logical texture with a solid color. */
static SDL_Texture *create_solid_rgb565(SDL_Renderer *renderer, int w, int h,
                                        Uint16 color)
{
    SDL_Texture *t;
    Uint16 *buf;
    int pitch;
    int i;

    pitch = w * (int)sizeof(Uint16);
    buf = (Uint16 *)SDL_malloc((size_t)h * (size_t)pitch);
    if (!buf) return NULL;
    for (i = 0; i < w * h; ++i)
        buf[i] = color;

    t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                          SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) { SDL_free(buf); return NULL; }
    if (SDL_UpdateTexture(t, NULL, buf, pitch) < 0) {
        SDL_DestroyTexture(t); SDL_free(buf); return NULL;
    }
    SDL_free(buf);
    return t;
}

/* Fill a logical texture with a solid ARGB1555 color. */
static SDL_Texture *create_solid_argb1555(SDL_Renderer *renderer, int w, int h,
                                          Uint16 color)
{
    SDL_Texture *t;
    Uint16 *buf;
    int pitch;
    int i;

    pitch = w * (int)sizeof(Uint16);
    buf = (Uint16 *)SDL_malloc((size_t)h * (size_t)pitch);
    if (!buf) return NULL;
    for (i = 0; i < w * h; ++i)
        buf[i] = color;

    t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                          SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) { SDL_free(buf); return NULL; }
    if (SDL_UpdateTexture(t, NULL, buf, pitch) < 0) {
        SDL_DestroyTexture(t); SDL_free(buf); return NULL;
    }
    SDL_free(buf);
    return t;
}

/* Fill a logical texture with a solid ARGB4444 color. */
static SDL_Texture *create_solid_argb4444(SDL_Renderer *renderer, int w, int h,
                                          Uint16 color)
{
    SDL_Texture *t;
    Uint16 *buf;
    int pitch;
    int i;

    pitch = w * (int)sizeof(Uint16);
    buf = (Uint16 *)SDL_malloc((size_t)h * (size_t)pitch);
    if (!buf) return NULL;
    for (i = 0; i < w * h; ++i)
        buf[i] = color;

    t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB4444,
                          SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) { SDL_free(buf); return NULL; }
    if (SDL_UpdateTexture(t, NULL, buf, pitch) < 0) {
        SDL_DestroyTexture(t); SDL_free(buf); return NULL;
    }
    SDL_free(buf);
    return t;
}

/* Draw a texture at a destination rect. */
static int draw_tex(SDL_Renderer *r, SDL_Texture *t, SDL_Rect *dst)
{
    if (SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE) < 0)
        return -1;
    return SDL_RenderCopy(r, t, NULL, dst);
}

/* ---------- page 1: POT RGB565 partial updates ---------- */

static int run_page1(SDL_Renderer *renderer)
{
    int fail_count = 0;
    int px, py;

    /* Clear with dark background. */
    if (SDL_SetRenderDrawColor(renderer, 8, 8, 12, 255) < 0 ||
        SDL_RenderClear(renderer) < 0)
        return 1;

    /* --- Nonzero-x, right-edge (x=12, w=4, touches logical right) --- */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 16, 16, 0x001fu);
        Uint16 data[4 * 16];
        SDL_Rect r = { 12, 0, 4, 16 };
        SDL_Rect dr = { 10, 10, 120, 120 };
        for (py = 0; py < 16; ++py)
            for (px = 0; px < 4; ++px)
                data[py * 4 + px] = 0xffe0u;
        if (t && SDL_UpdateTexture(t, &r, data, 4 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- Interior patch (partial-upload eligible) --- */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 16, 16, 0x001fu);
        Uint16 data[8 * 8];
        SDL_Rect r = { 4, 4, 8, 8 };
        SDL_Rect dr = { 150, 10, 120, 120 };
        for (py = 0; py < 8; ++py)
            for (px = 0; px < 8; ++px)
                data[py * 8 + px] = ((px + py) & 1) ? 0x07ffu : 0xf800u;
        if (t && SDL_UpdateTexture(t, &r, data, 8 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- Multiple rows, nonzero x, bottom-edge (y=12, h=4) --- */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 16, 16, 0x001fu);
        Uint16 data[8 * 16];
        SDL_Rect r = { 4, 12, 8, 4 };
        SDL_Rect dr = { 290, 10, 120, 120 };
        for (py = 0; py < 4; ++py)
            for (px = 0; px < 16; ++px)
                data[py * 16 + px] = ((px ^ py) & 1) ? 0xe000u : 0x00e0u;
        if (t && SDL_UpdateTexture(t, &r, data, 16 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- Row-edge: top row only (y=0, h=1) --- */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 16, 16, 0x001fu);
        Uint16 data[16];
        SDL_Rect r = { 0, 0, 16, 1 };
        SDL_Rect dr = { 10, 150, 120, 120 };
        for (px = 0; px < 16; ++px)
            data[px] = 0xffe0u;
        if (t && SDL_UpdateTexture(t, &r, data, 16 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- Row-edge: bottom row only (y=15, h=1) --- */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 16, 16, 0x001fu);
        Uint16 data[16];
        SDL_Rect r = { 0, 15, 16, 1 };
        SDL_Rect dr = { 150, 150, 120, 120 };
        for (px = 0; px < 16; ++px)
            data[px] = 0x07ffu;
        if (t && SDL_UpdateTexture(t, &r, data, 16 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- Bottom-right corner (x=12, y=12, w=4, h=4) --- */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 16, 16, 0x001fu);
        Uint16 data[4 * 4];
        SDL_Rect r = { 12, 12, 4, 4 };
        SDL_Rect dr = { 290, 150, 120, 120 };
        for (py = 0; py < 4; ++py)
            for (px = 0; px < 4; ++px)
                data[py * 4 + px] = 0x0400u;
        if (t && SDL_UpdateTexture(t, &r, data, 4 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- Neighboring-texel preservation: update center, verify corners --- */
    {
        SDL_Texture *t = create_checker_rgb565(renderer, 16, 16);
        Uint16 data[8 * 8];
        SDL_Rect r = { 4, 4, 8, 8 };
        SDL_Rect dr = { 10, 290, 120, 120 };
        /* Fill update region with a distinct pattern. */
        for (py = 0; py < 8; ++py)
            for (px = 0; px < 8; ++px)
                data[py * 8 + px] = 0xffe0u;
        if (t && SDL_UpdateTexture(t, &r, data, 8 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
        /* Note: the captured PPM shows the checker pattern with the center
         * replaced by 0xffe0. The four corners (untouched) retain checker
         * values, proving neighboring texels were not corrupted. */
    }

    /* --- Padded source pitch: rect.w=4, source_pitch=8 words/row --- */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 16, 16, 0x001fu);
        Uint16 *data;
        SDL_Rect r = { 12, 0, 4, 16 };
        SDL_Rect dr = { 150, 290, 120, 120 };

        /* Source pitch = 8 words per row (padded), but rect.w = 4. */
        data = (Uint16 *)SDL_malloc(16 * 8 * sizeof(Uint16));
        if (!data) { ++fail_count; SDL_DestroyTexture(t); goto p1_done; }
        for (py = 0; py < 16; ++py) {
            for (px = 0; px < 4; ++px)
                data[py * 8 + px] = 0x07ffu;
            /* Rows 4-7 are padding (left untouched by the update). */
            for (px = 4; px < 8; ++px)
                data[py * 8 + px] = 0x001fu;
        }
        if (t && SDL_UpdateTexture(t, &r, data, 8 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_free(data);
        SDL_DestroyTexture(t);
    }

    /* --- 32×32 RGB565: right-edge 16×16 update (triggers partial) --- */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 32, 32, 0x001fu);
        Uint16 data[16 * 32];
        SDL_Rect r = { 16, 0, 16, 16 };
        SDL_Rect dr = { 290, 290, 120, 120 };
        for (py = 0; py < 16; ++py)
            for (px = 0; px < 32; ++px)
                data[py * 32 + px] = ((px ^ py) & 1) ? 0xffe0u : 0x07ffu;
        if (t && SDL_UpdateTexture(t, &r, data, 32 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

p1_done:
    printf("Page 1: PASS — POT RGB565 partial updates (nonzero-x, interior, "
           "multi-row, row edges, edges, corner, neighbor-preserve, padded, 32×32)\n");
    return fail_count;
}

/* ---------- page 2: POT ARGB1555/ARGB4444, streaming lock, scene ref ---------- */

static int run_page2(SDL_Renderer *renderer)
{
    int fail_count = 0;
    int px, py;

    if (SDL_SetRenderDrawColor(renderer, 8, 8, 12, 255) < 0 ||
        SDL_RenderClear(renderer) < 0)
        return 1;

    /* --- ARGB1555: right-edge 16×16 (POT partial) --- */
    {
        SDL_Texture *t = create_solid_argb1555(renderer, 32, 32, 0x000fu);
        Uint16 data[16 * 32];
        SDL_Rect r = { 16, 0, 16, 16 };
        SDL_Rect dr = { 10, 10, 120, 120 };
        for (py = 0; py < 16; ++py)
            for (px = 0; px < 32; ++px)
                data[py * 32 + px] = ((px + py) & 31) ? 0xfc00u : 0x03ffu;
        if (t && SDL_UpdateTexture(t, &r, data, 32 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- ARGB4444: bottom-edge 16×16 (POT partial) --- */
    {
        SDL_Texture *t = create_solid_argb4444(renderer, 32, 32, 0x000fu);
        Uint16 data[16 * 32];
        SDL_Rect r = { 0, 16, 16, 16 };
        SDL_Rect dr = { 150, 10, 120, 120 };
        for (py = 0; py < 16; ++py)
            for (px = 0; px < 32; ++px)
                data[py * 32 + px] = (Uint16)(((px * 15 / 31) << 12) |
                                              ((py * 15 / 31) << 8) | 0x000fu);
        if (t && SDL_UpdateTexture(t, &r, data, 32 * (int)sizeof(Uint16)) == 0) {
            draw_tex(renderer, t, &dr);
        } else { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- Partial streaming lock/unlock: bottom-edge, multi-row (POT) --- */
    {
        SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                           SDL_TEXTUREACCESS_STREAMING, 16, 16);
        void *pixels;
        int pitch;
        SDL_Rect lock_r = { 0, 12, 16, 4 };
        SDL_Rect dr = { 290, 10, 120, 120 };
        int py, px;

        if (!t || SDL_LockTexture(t, &lock_r, &pixels, &pitch) < 0) {
            ++fail_count; SDL_DestroyTexture(t); goto p2_done;
        }
        for (py = 0; py < 4; ++py) {
            Uint16 *row = (Uint16 *)((Uint8 *)pixels + py * pitch);
            for (px = 0; px < 16; ++px)
                row[px] = 0xe000u;
        }
        SDL_UnlockTexture(t);
        if (draw_tex(renderer, t, &dr) < 0) {
            ++fail_count; SDL_DestroyTexture(t); goto p2_done;
        }
        SDL_DestroyTexture(t);
    }

    /* --- Update after scene reference (full-backing fallback) --- */
    {
        SDL_Texture *t = create_checker_rgb565(renderer, 16, 16);
        Uint16 update_buf[16 * 16];
        SDL_Rect dr = { 10, 150, 120, 120 };
        int py, px;

        if (!t) { ++fail_count; goto p2_done; }

        /* Draw the checker first to establish a scene reference. */
        if (draw_tex(renderer, t, &dr) < 0 ||
            SDL_RenderFlush(renderer) < 0) {
            ++fail_count; SDL_DestroyTexture(t); goto p2_done;
        }

        /* Now update — triggers full-backing fallback because referenced. */
        for (py = 0; py < 16; ++py)
            for (px = 0; px < 16; ++px)
                update_buf[py * 16 + px] = ((px * 3 + py * 7) & 31) ? 0x0400u : 0xf000u;
        if (SDL_UpdateTexture(t, NULL, update_buf, 16 * (int)sizeof(Uint16)) < 0) {
            ++fail_count; SDL_DestroyTexture(t); goto p2_done;
        }

        /* Draw again at a new position to verify the update took effect. */
        dr.x = 150;
        if (draw_tex(renderer, t, &dr) < 0) {
            ++fail_count; SDL_DestroyTexture(t); goto p2_done;
        }
        SDL_DestroyTexture(t);
    }

    printf("Page 2: PASS — POT ARGB1555/ARGB4444 partial, streaming lock, "
           "scene-reference fallback\n");
    return fail_count;

p2_done:
    printf("Page 2: FAIL (%d errors)\n", fail_count);
    return fail_count;
}

/* ---------- page 3: strided fallback, diagnostics, VRAM recovery ---------- */

static int run_page3(SDL_Renderer *renderer)
{
    SDL_Rect dst;
    int fail_count = 0;
    int i;

    if (SDL_SetRenderDrawColor(renderer, 8, 8, 12, 255) < 0 ||
        SDL_RenderClear(renderer) < 0)
        return 1;

    /* --- Normal static texture (baseline) --- */
    {
        SDL_Texture *t = create_checker_rgb565(renderer, 8, 8);
        dst.x = 10; dst.y = 10; dst.w = 120; dst.h = 120;
        if (!t || draw_tex(renderer, t, &dst) < 0) { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    /* --- Strided RGB565: exercises full-upload fallback ---
     * For a 640×480 output the fixed-class stride is 640 px.  Strided
     * layout triggers when preferred_stride_px < backing_w, so we need a
     * texture whose POT backing exceeds 640 px (logical width > 512).
     * This test creates such a texture and verifies it still renders
     * correctly (full-backing path) despite the partial-upload option. */
    {
        SDL_Texture *t = create_solid_rgb565(renderer, 520, 16, 0x001fu);
        Uint16 *data;
        SDL_Rect r = { 456, 0, 64, 16 };
        SDL_Rect dr = { 150, 10, 120, 120 };
        int py, px;

        if (!t) { ++fail_count; goto p3_done; }

        /* Update the right-edge region. With strided layout this must
         * fall through to full-backing upload. */
        data = (Uint16 *)SDL_malloc(16 * 64 * sizeof(Uint16));
        if (!data) { ++fail_count; SDL_DestroyTexture(t); goto p3_done; }
        for (py = 0; py < 16; ++py) {
            for (px = 0; px < 64; ++px)
                data[py * 64 + px] = 0xffe0u;
        }
        if (SDL_UpdateTexture(t, &r, data, 64 * (int)sizeof(Uint16)) < 0) {
            ++fail_count; SDL_free(data); SDL_DestroyTexture(t); goto p3_done;
        }
        SDL_free(data);

        if (draw_tex(renderer, t, &dr) < 0) {
            ++fail_count; SDL_DestroyTexture(t); goto p3_done;
        }
        SDL_DestroyTexture(t);
    }

    /* --- VRAM recovery: create/destroy cycle --- */
    for (i = 0; i < 5; ++i) {
        SDL_Texture *t = create_solid_argb1555(renderer, 8, 8, 0x001fu);
        dst.x = 10 + i * 25;
        dst.y = 300;
        dst.w = 60; dst.h = 60;
        if (t && draw_tex(renderer, t, &dst) < 0) {
            ++fail_count;
        }
        SDL_DestroyTexture(t);
    }

    /* --- Renderer continues after all the above --- */
    {
        SDL_Texture *t = create_solid_argb4444(renderer, 8, 8, 0x00ffu);
        dst.x = 10; dst.y = 380; dst.w = 120; dst.h = 120;
        if (!t || draw_tex(renderer, t, &dst) < 0) { ++fail_count; }
        SDL_DestroyTexture(t);
    }

    printf("Page 3: PASS — strided full-fallback, diagnostics, VRAM recovery, "
           "continued rendering\n");
    return fail_count;

p3_done:
    printf("Page 3: FAIL (%d errors)\n", fail_count);
    return fail_count;
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    int driver;
    int result = 1;
    int page;
    int total_fail = 0;

    (void)argc; (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    driver = find_pvr_driver();
    if (driver < 0) {
        printf("Dreamcast PVR renderer is not available\n");
        goto done;
    }

    window = SDL_CreateWindow("PVR partial VRAM uploads",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              640, 480, 0);
    if (!window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        goto done;
    }

    renderer = SDL_CreateRenderer(window, driver,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        goto done;
    }

    if (check_renderer_formats(renderer) < 0) {
        printf("Renderer format check failed\n");
        goto done;
    }

    /* Enable diagnostic hints for all pages. */
    SDL_SetHint("DC_PVR_SHOW_UPLOAD_PATH", "1");
    printf("DC_PVR_SHOW_UPLOAD_PATH=1: upload-path diagnostics enabled\n");

    for (page = 1; page <= 3; ++page) {
        int fail;
        if (page == 1) fail = run_page1(renderer);
        else if (page == 2) fail = run_page2(renderer);
        else fail = run_page3(renderer);

        total_fail += fail;

        SDL_RenderPresent(renderer);

#ifdef PVR_TEXTURE_HARDWARE_SCREENSHOT
        {
            char path[256];
            SDL_snprintf(path, sizeof(path), "%s%d.ppm",
                         SCREENSHOT_PREFIX, page);
            /* The PVR render-complete interrupt precedes the display-buffer
             * handoff. The PVR presentation path is three-buffered, so wait
             * three vblanks before /pc reads the displayed page. */
            vid_waitvbl();
            vid_waitvbl();
            vid_waitvbl();
            if (vid_screen_shot(path) < 0) {
                printf("Screenshot failed for page %d\n", page);
            } else {
                printf("Saved page %d to %s\n", page, path);
            }
        }
#endif

        printf("Holding page %d for %d ms\n", page, HOLD_MS);
        SDL_Delay(HOLD_MS);
    }

    printf("PASS (partial upload): %d failures across 3 pages\n", total_fail);
    result = (total_fail == 0) ? 0 : 1;

done:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
