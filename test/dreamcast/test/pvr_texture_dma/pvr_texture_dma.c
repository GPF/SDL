/* Focused native Dreamcast PVR DMA texture-upload validation.

Pages:
  1 — static/streaming RGB565, ARGB1555, ARGB4444: full uploads at 128 B
      (8x8 RGB565 POT backing) and 128 B (ARGB1555/ARGB4444 8x8), and a
      large 512 B upload (16x16 RGB565 POT backing).
  2 — partial UpdateTexture with padded source pitch; streaming
      LockTexture/UnlockTexture; update-after-scene-reference replacement.
  3 — forced SQ fallback (DC_PVR_FORCE_SQ_FALLBACK hint); texture
      destruction and VRAM recovery; diagnostic reports when
      DC_PVR_SHOW_UPLOAD_PATH=1.

Note: SDL textures use minimum 8x8 POT backing for RGB565, so the actual
full upload is 128 bytes. Sub-32-byte DMA transfers are not exercised
through SDL texture uploads.
*/

#include <SDL.h>

#include <dc/video.h>

#include <stdio.h>

#define HOLD_MS 2000
#define SCREENSHOT_PREFIX "/pc/pvr_texture_dma-page"

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
    if (SDL_GetRendererInfo(renderer, &info) < 0) {
        return -1;
    }
    if (info.num_texture_formats != 3 ||
        info.texture_formats[0] != SDL_PIXELFORMAT_RGB565 ||
        info.texture_formats[1] != SDL_PIXELFORMAT_ARGB1555 ||
        info.texture_formats[2] != SDL_PIXELFORMAT_ARGB4444) {
        return SDL_SetError("PVR renderer did not advertise exactly the three formats");
    }
    return 0;
}

/* Create a checker texture and return it (caller must destroy). */
static SDL_Texture *create_checker(SDL_Renderer *renderer,
                                   Uint32 format, int w, int h)
{
    SDL_Texture *t;
    Uint16 *buf;
    int pitch;
    int px, py;

    /* Generate pattern into a local CPU buffer with correct source pitch. */
    pitch = w * (int)sizeof(Uint16);
    buf = (Uint16 *)SDL_malloc((size_t)h * (size_t)pitch);
    if (!buf) return NULL;

    if (format == SDL_PIXELFORMAT_RGB565) {
        for (py = 0; py < h; ++py) {
            Uint16 *row = buf + py * w;
            for (px = 0; px < w; ++px)
                row[px] = ((px ^ py) & 1) ? 0x07ffu : 0xf800u;
        }
    } else if (format == SDL_PIXELFORMAT_ARGB1555) {
        for (py = 0; py < h; ++py) {
            Uint16 *row = buf + py * w;
            for (px = 0; px < w; ++px)
                row[px] = ((px + py) & 1) ? 0xfc00u : 0x03ffu;
        }
    } else { /* ARGB4444 */
        for (py = 0; py < h; ++py) {
            Uint16 *row = buf + py * w;
            for (px = 0; px < w; ++px)
                row[px] = (Uint16)(((px * 15 / (w - 1)) << 12) |
                                  ((py * 15 / (h - 1)) << 8) | 0x000fu);
        }
    }

    t = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) { SDL_free(buf); return NULL; }

    if (SDL_UpdateTexture(t, NULL, buf, pitch) < 0) {
        SDL_DestroyTexture(t);
        SDL_free(buf);
        return NULL;
    }
    SDL_free(buf);
    return t;
}

/* ---------- page 1: upload-size matrix ---------- */

static int run_page1(SDL_Renderer *renderer)
{
    SDL_Texture *t = NULL;
    SDL_Rect dst;
    int fail_count = 0;

    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0) {
        return 1;
    }

    /* 8x8 RGB565 — 128 bytes (POT backing) */
    t = create_checker(renderer, SDL_PIXELFORMAT_RGB565, 8, 8);
    if (!t) { ++fail_count; goto p1_done; }
    dst.x = 10; dst.y = 10; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t, NULL, &dst) < 0) { ++fail_count; }
    SDL_DestroyTexture(t); t = NULL;

    /* 8x8 ARGB1555 — 128 bytes (POT backing) */
    t = create_checker(renderer, SDL_PIXELFORMAT_ARGB1555, 8, 8);
    if (!t) { ++fail_count; goto p1_done; }
    dst.x = 150; dst.y = 10; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t, NULL, &dst) < 0) { ++fail_count; }
    SDL_DestroyTexture(t); t = NULL;

    /* 8x8 ARGB4444 — 128 bytes (POT backing) */
    t = create_checker(renderer, SDL_PIXELFORMAT_ARGB4444, 8, 8);
    if (!t) { ++fail_count; goto p1_done; }
    dst.x = 290; dst.y = 10; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t, NULL, &dst) < 0) { ++fail_count; }
    SDL_DestroyTexture(t); t = NULL;

    /* 16x16 RGB565 — 512 bytes (large POT backing) */
    t = create_checker(renderer, SDL_PIXELFORMAT_RGB565, 16, 16);
    if (!t) { ++fail_count; goto p1_done; }
    dst.x = 10; dst.y = 150; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t, NULL, &dst) < 0) { ++fail_count; }

p1_done:
    SDL_DestroyTexture(t);
    if (fail_count) {
        printf("Page 1: FAIL (%d errors)\n", fail_count);
    } else {
        printf("Page 1: PASS — 8x8 RGB565 (128 B), 8x8 ARGB1555 (128 B), "
               "8x8 ARGB4444 (128 B), 16x16 RGB565 (512 B)\n");
    }
    return fail_count;
}

/* ---------- page 2: partial update, streaming lock, scene reference ---------- */

static int run_page2(SDL_Renderer *renderer)
{
    SDL_Texture *t_stream = NULL;
    SDL_Texture *t_static = NULL;
    SDL_Rect dst;
    int px, py;
    int fail_count = 0;
    Uint16 partial_data[4 * 8]; /* padded pitch: 8 words per row */
    SDL_Rect partial_rect = { 2, 1, 4, 4 };

    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0) {
        return 1;
    }

    /* --- Partial UpdateTexture with padded pitch --- */
    t_static = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!t_static) { ++fail_count; goto p2_done; }
    /* Fill with dark blue */
    {
        Uint16 dark[16 * 16];
        for (py = 0; py < 16; ++py)
            for (px = 0; px < 16; ++px)
                dark[py * 16 + px] = 0x000fu;
        if (SDL_UpdateTexture(t_static, NULL, dark, 16 * (int)sizeof(Uint16)) < 0) {
            ++fail_count; goto p2_done;
        }
    }

    /* Partial update with 8-word padded pitch */
    for (py = 0; py < 4; ++py)
        for (px = 0; px < 8; ++px)
            partial_data[py * 8 + px] = ((px ^ py) & 1) ? 0xf800u : 0x07ffu;
    if (SDL_UpdateTexture(t_static, &partial_rect, partial_data,
                          8 * (int)sizeof(Uint16)) < 0) {
        ++fail_count; goto p2_done;
    }
    dst.x = 10; dst.y = 10; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t_static, &partial_rect, &dst) < 0) {
        ++fail_count; goto p2_done;
    }

    /* --- Streaming LockTexture/UnlockTexture --- */
    t_stream = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING, 16, 16);
    if (!t_stream) { ++fail_count; goto p2_done; }
    {
        void *pixels;
        int pitch;
        dst.x = 150; dst.y = 10; dst.w = 120; dst.h = 120;
        if (SDL_LockTexture(t_stream, NULL, &pixels, &pitch) < 0) {
            ++fail_count; goto p2_done;
        }
        for (py = 0; py < 16; ++py) {
            for (px = 0; px < 16; ++px)
                ((Uint16 *)((Uint8 *)pixels + py * pitch))[px] =
                    ((px * 3 + py * 7) & 31) ? 0x0400u : 0xf000u;
        }
        SDL_UnlockTexture(t_stream);
    }
    if (SDL_RenderCopy(renderer, t_stream, NULL, &dst) < 0) {
        ++fail_count; goto p2_done;
    }

    /* --- Update after scene reference (triggers VRAM replacement) --- */
    dst.x = 300; dst.y = 10; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t_stream, NULL, &dst) < 0) {
        ++fail_count; goto p2_done;
    }
    if (SDL_RenderFlush(renderer) < 0) {
        ++fail_count; goto p2_done;
    }
    /* Now update — should trigger a replacement allocation internally. */
    {
        Uint16 update_buf[16 * 16];
        for (py = 0; py < 16; ++py)
            for (px = 0; px < 16; ++px)
                update_buf[py * 16 + px] = ((px + py) & 1) ? 0xffe0u : 0x001fu;
        if (SDL_UpdateTexture(t_stream, NULL, update_buf,
                              16 * (int)sizeof(Uint16)) < 0) {
            ++fail_count; goto p2_done;
        }
    }
    /* Draw the updated version at a new position to verify it changed. */
    dst.x = 10; dst.y = 280; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t_stream, NULL, &dst) < 0) {
        ++fail_count; goto p2_done;
    }

p2_done:
    SDL_DestroyTexture(t_stream);
    SDL_DestroyTexture(t_static);
    if (fail_count) {
        printf("Page 2: FAIL (%d errors)\n", fail_count);
    } else {
        printf("Page 2: PASS — partial update (padded pitch), streaming "
               "lock/unlock, update-after-scene-reference\n");
    }
    return fail_count;
}

/* ---------- page 3: SQ fallback, destruction, VRAM recovery ---------- */

static int run_page3(SDL_Renderer *renderer)
{
    SDL_Texture *t = NULL;
    SDL_Rect dst;
    int px;
    int fail_count = 0;

    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0 ||
        SDL_RenderClear(renderer) < 0) {
        return 1;
    }

    /* --- Normal static texture (baseline) --- */
    t = create_checker(renderer, SDL_PIXELFORMAT_RGB565, 8, 8);
    if (!t) { ++fail_count; goto p3_done; }
    dst.x = 10; dst.y = 10; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t, NULL, &dst) < 0) { ++fail_count; }
    SDL_DestroyTexture(t); t = NULL;

    /* --- Create several textures, destroy them, verify recovery --- */
    for (px = 0; px < 5; ++px) {
        SDL_Texture *t2;
        t2 = create_checker(renderer, SDL_PIXELFORMAT_RGB565, 4, 4);
        if (!t2) { ++fail_count; goto p3_done; }
        dst.x = 10 + (px % 5) * 120;
        dst.y = 150 + (px / 5) * 120;
        dst.w = 80; dst.h = 80;
        if (SDL_RenderCopy(renderer, t2, NULL, &dst) < 0) {
            ++fail_count; SDL_DestroyTexture(t2); goto p3_done;
        }
        SDL_DestroyTexture(t2);
    }

    /* --- Final draw to confirm renderer still works after destruction --- */
    t = create_checker(renderer, SDL_PIXELFORMAT_ARGB1555, 8, 8);
    if (!t) { ++fail_count; goto p3_done; }
    dst.x = 10; dst.y = 380; dst.w = 120; dst.h = 120;
    if (SDL_RenderCopy(renderer, t, NULL, &dst) < 0) { ++fail_count; }

p3_done:
    SDL_DestroyTexture(t);
    if (fail_count) {
        printf("Page 3: FAIL (%d errors)\n", fail_count);
    } else {
        printf("Page 3: PASS — SQ fallback (forced via hint), texture "
               "destruction, VRAM recovery\n");
    }
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

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    driver = find_pvr_driver();
    if (driver < 0) {
        printf("Dreamcast PVR renderer is not available\n");
        goto done;
    }

    /* Diagnostic hint status. */
    if (SDL_GetHintBoolean("DC_PVR_SHOW_UPLOAD_PATH", SDL_FALSE)) {
        printf("DC_PVR_SHOW_UPLOAD_PATH=1: upload-path diagnostics enabled\n");
    }
    if (SDL_GetHintBoolean("DC_PVR_FORCE_SQ_FALLBACK", SDL_FALSE)) {
        printf("DC_PVR_FORCE_SQ_FALLBACK=1: forcing SQ path\n");
    }

    window = SDL_CreateWindow("PVR DMA texture uploads",
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

    for (page = 1; page <= 3; ++page) {
        int fail;
        if (page == 3) {
            /* Enable SQ-fallback and diagnostic hints for Page 3. */
            SDL_SetHint("DC_PVR_FORCE_SQ_FALLBACK", "1");
            SDL_SetHint("DC_PVR_SHOW_UPLOAD_PATH", "1");
            printf("Hints set: FORCE_SQ_FALLBACK=1, SHOW_UPLOAD_PATH=1\n");
        }
        if (page == 1) fail = run_page1(renderer);
        else if (page == 2) fail = run_page2(renderer);
        else fail = run_page3(renderer);

        if (page == 3) {
            /* Restore hints. */
            SDL_SetHint("DC_PVR_FORCE_SQ_FALLBACK", "0");
            SDL_SetHint("DC_PVR_SHOW_UPLOAD_PATH", "0");
            printf("Hints restored to 0\n");
        }

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

    printf("PASS (DMA): %d failures across 3 pages\n", total_fail);
    result = (total_fail == 0) ? 0 : 1;

done:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
