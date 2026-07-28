#include <SDL.h>
#include <kos.h>

#include <math.h>
#include <stdio.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define TEX_W 13
#define TEX_H 9
#define PAGE_COUNT 3
#define PAGE_HOLD_MS 30000

#ifdef PVR_COPY_EX_HARDWARE_SCREENSHOT
static const char *const hardware_paths[PAGE_COUNT] = {
    "/pc/pvr_copy_ex-hardware-page1.ppm",
    "/pc/pvr_copy_ex-hardware-page2.ppm",
    "/pc/pvr_copy_ex-hardware-page3.ppm"
};
#endif

static int find_pvr_driver(void)
{
    SDL_RendererInfo info;
    int i;

    for (i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 && info.name &&
            SDL_strcmp(info.name, "Dreamcast PVR") == 0) {
            return i;
        }
    }
    return -1;
}

static int failed_call(const char *name)
{
    printf("FAIL: %s: %s\n", name, SDL_GetError());
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return failed_call(#call); } while (0)

static Uint16 rgb565(Uint8 r, Uint8 g, Uint8 b)
{
    return (Uint16)(((Uint16)(r >> 3) << 11) |
                    ((Uint16)(g >> 2) << 5) |
                    (Uint16)(b >> 3));
}

static void make_texture(Uint16 pixels[TEX_H][TEX_W])
{
    int x;
    int y;

    for (y = 0; y < TEX_H; ++y) {
        for (x = 0; x < TEX_W; ++x) {
            const Uint8 shade = ((x + y) & 1) ? 70 : 25;
            pixels[y][x] = rgb565(shade, shade, (Uint8)(shade + 20));
        }
    }

    for (y = 0; y < 3; ++y) {
        for (x = 0; x < 3; ++x) {
            pixels[y][x] = rgb565(255, 20, 20);                 /* top-left */
            pixels[y][TEX_W - 1 - x] = rgb565(20, 255, 20);    /* top-right */
            pixels[TEX_H - 1 - y][x] = rgb565(20, 40, 255);    /* bottom-left */
            pixels[TEX_H - 1 - y][TEX_W - 1 - x] =
                rgb565(255, 255, 20);                           /* bottom-right */
        }
    }

    /* White/cyan arrow points toward the logical right edge. */
    for (x = 3; x <= 9; ++x) {
        pixels[4][x] = rgb565(255, 255, 255);
    }
    pixels[2][9] = rgb565(20, 255, 255);
    pixels[3][10] = rgb565(20, 255, 255);
    pixels[4][11] = rgb565(20, 255, 255);
    pixels[5][10] = rgb565(20, 255, 255);
    pixels[6][9] = rgb565(20, 255, 255);
}

static int fill(SDL_Renderer *renderer, int x, int y, int w, int h,
                Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_Rect rect = { x, y, w, h };

    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, a));
    REQUIRE(SDL_RenderFillRect(renderer, &rect));
    return 0;
}

static int guard(SDL_Renderer *renderer, const SDL_Rect *dst)
{
    return fill(renderer, dst->x - 2, dst->y - 2, dst->w + 4, dst->h + 4,
                255, 0, 255, 255);
}

static int copy_guarded(SDL_Renderer *renderer, SDL_Texture *texture,
                        const SDL_Rect *src, const SDL_Rect *dst)
{
    REQUIRE(guard(renderer, dst));
    REQUIRE(SDL_RenderCopy(renderer, texture, src, dst));
    return 0;
}

static int copy_ex_guarded(SDL_Renderer *renderer, SDL_Texture *texture,
                           const SDL_Rect *src, const SDL_Rect *dst,
                           double angle, const SDL_Point *center,
                           SDL_RendererFlip flip)
{
    REQUIRE(guard(renderer, dst));
    REQUIRE(SDL_RenderCopyEx(renderer, texture, src, dst, angle, center, flip));
    return 0;
}

static int reset_page(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_RenderSetViewport(renderer, NULL));
    REQUIRE(SDL_RenderSetClipRect(renderer, NULL));
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderClear(renderer));
    return 0;
}

static int draw_page1(SDL_Renderer *renderer, SDL_Texture *nearest,
                      SDL_Texture *linear)
{
    SDL_Rect full = { 0, 0, TEX_W, TEX_H };
    SDL_Rect interior = { 3, 2, 7, 5 };
    SDL_Rect one_wide = { 6, 0, 1, TEX_H };
    SDL_Rect one_high = { 0, 4, TEX_W, 1 };
    SDL_Rect left_edge = { 0, 0, 5, TEX_H };
    SDL_Rect right_edge = { 8, 0, 5, TEX_H };
    SDL_Rect top_edge = { 0, 0, TEX_W, 4 };
    SDL_Rect bottom_edge = { 0, 5, TEX_W, 4 };
    SDL_Rect dst;
    SDL_FRect fdst;

    REQUIRE(reset_page(renderer, 4, 8, 18));
    REQUIRE(fill(renderer, 8, 8, 26, 8, 255, 40, 40, 255));

    dst = (SDL_Rect){ 20, 30, TEX_W, TEX_H };       /* exact 1:1 */
    REQUIRE(copy_guarded(renderer, nearest, &full, &dst));
    dst = (SDL_Rect){ 60, 25, 104, 72 };            /* enlargement */
    REQUIRE(copy_guarded(renderer, nearest, &full, &dst));
    dst = (SDL_Rect){ 195, 45, 7, 5 };              /* reduction */
    REQUIRE(copy_guarded(renderer, nearest, &full, &dst));
    dst = (SDL_Rect){ 235, 30, 130, 45 };           /* nonuniform */
    REQUIRE(copy_guarded(renderer, nearest, &full, &dst));
    fdst = (SDL_FRect){ 405.5f, 28.5f, 117.5f, 63.5f };
    REQUIRE(SDL_RenderCopyF(renderer, nearest, &full, &fdst));

    dst = (SDL_Rect){ 20, 125, 98, 70 };
    REQUIRE(copy_guarded(renderer, nearest, &interior, &dst));
    dst = (SDL_Rect){ 140, 115, 28, 90 };
    REQUIRE(copy_guarded(renderer, nearest, &one_wide, &dst));
    dst = (SDL_Rect){ 190, 145, 130, 18 };
    REQUIRE(copy_guarded(renderer, nearest, &one_high, &dst));
    dst = (SDL_Rect){ 340, 120, 50, 72 };
    REQUIRE(copy_guarded(renderer, nearest, &left_edge, &dst));
    dst = (SDL_Rect){ 402, 120, 50, 72 };
    REQUIRE(copy_guarded(renderer, nearest, &right_edge, &dst));
    dst = (SDL_Rect){ 466, 120, 78, 32 };
    REQUIRE(copy_guarded(renderer, nearest, &top_edge, &dst));
    dst = (SDL_Rect){ 554, 160, 78, 32 };
    REQUIRE(copy_guarded(renderer, nearest, &bottom_edge, &dst));

    dst = (SDL_Rect){ 55, 275, 208, 144 };
    REQUIRE(copy_guarded(renderer, nearest, &full, &dst));
    dst = (SDL_Rect){ 365, 275, 208, 144 };
    REQUIRE(copy_guarded(renderer, linear, &full, &dst));
    REQUIRE(fill(renderer, 55, 432, 208, 6, 255, 255, 255, 255));
    REQUIRE(fill(renderer, 365, 432, 208, 6, 80, 180, 255, 255));
    return 0;
}

static int draw_page2(SDL_Renderer *renderer, SDL_Texture *texture)
{
    static const double angles[4] = { 0.0, 90.0, 180.0, 270.0 };
    static const SDL_RendererFlip flips[4] = {
        SDL_FLIP_NONE, SDL_FLIP_HORIZONTAL, SDL_FLIP_VERTICAL,
        (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)
    };
    SDL_Rect full = { 0, 0, TEX_W, TEX_H };
    SDL_Rect interior = { 2, 1, 9, 7 };
    SDL_Rect dst;
    SDL_Point corner = { 0, 0 };
    int i;

    REQUIRE(reset_page(renderer, 12, 5, 18));
    REQUIRE(fill(renderer, 8, 8, 26, 8, 40, 255, 40, 255));

    for (i = 0; i < 4; ++i) {
        dst = (SDL_Rect){ 25 + i * 150, 35, 104, 72 };
        REQUIRE(copy_ex_guarded(renderer, texture, &full, &dst, 0.0, NULL,
                                flips[i]));
    }
    for (i = 0; i < 4; ++i) {
        dst = (SDL_Rect){ 25 + i * 150, 155, 104, 72 };
        REQUIRE(copy_ex_guarded(renderer, texture, &full, &dst, angles[i],
                                NULL, SDL_FLIP_NONE));
    }

    dst = (SDL_Rect){ 35, 305, 104, 72 };
    REQUIRE(copy_ex_guarded(renderer, texture, &full, &dst, 45.0, NULL,
                            SDL_FLIP_NONE));
    dst = (SDL_Rect){ 190, 305, 104, 72 };
    REQUIRE(copy_ex_guarded(renderer, texture, &full, &dst, 90.0, &corner,
                            SDL_FLIP_NONE));
    REQUIRE(fill(renderer, dst.x - 3, dst.y - 3, 7, 7, 255, 255, 255, 255));
    dst = (SDL_Rect){ 350, 305, 104, 72 };
    REQUIRE(copy_ex_guarded(renderer, texture, &interior, &dst, 30.0, NULL,
                            SDL_FLIP_HORIZONTAL));
    {
        SDL_FRect fdst = { 510.5f, 305.5f, 91.0f, 63.0f };
        SDL_FPoint fcorner = { fdst.w, fdst.h / 2.0f };

        REQUIRE(SDL_RenderCopyExF(renderer, texture, &interior, &fdst, -45.0,
                                  &fcorner,
                                  (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL |
                                                     SDL_FLIP_VERTICAL)));
    }
    return 0;
}

static int draw_panel(SDL_Renderer *renderer, SDL_Texture *texture, int view_x)
{
    SDL_Rect viewport = { view_x, 0, 320, OUTPUT_H };
    SDL_Rect clip = { 24, 45, 272, 190 };
    SDL_Rect src = { 2, 1, 9, 7 };
    SDL_Rect dst;

    REQUIRE(SDL_RenderSetViewport(renderer, &viewport));
    REQUIRE(SDL_RenderSetClipRect(renderer, NULL));
    REQUIRE(fill(renderer, 0, 0, 320, OUTPUT_H, 8, 12, 22, 255));
    REQUIRE(fill(renderer, 20, 41, 280, 198, 255, 0, 255, 255));
    REQUIRE(fill(renderer, 24, 45, 272, 190, 18, 26, 42, 255));
    REQUIRE(SDL_RenderSetClipRect(renderer, &clip));

    dst = (SDL_Rect){ -5, 55, 150, 104 };
    REQUIRE(SDL_RenderCopyEx(renderer, texture, NULL, &dst, 30.0, NULL,
                             SDL_FLIP_HORIZONTAL));
    dst = (SDL_Rect){ 180, 125, 150, 104 };
    REQUIRE(SDL_RenderCopyEx(renderer, texture, &src, &dst, -35.0, NULL,
                             SDL_FLIP_VERTICAL));

    REQUIRE(SDL_RenderSetClipRect(renderer, NULL));
    REQUIRE(SDL_SetTextureColorMod(texture, 190, 90, 255));
    REQUIRE(SDL_SetTextureAlphaMod(texture, 150));
    REQUIRE(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND));
    dst = (SDL_Rect){ 35, 285, 130, 90 };
    REQUIRE(SDL_RenderCopyEx(renderer, texture, NULL, &dst, 20.0, NULL,
                             SDL_FLIP_NONE));
    REQUIRE(SDL_SetTextureColorMod(texture, 255, 255, 255));
    REQUIRE(SDL_SetTextureAlphaMod(texture, 255));

    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(fill(renderer, 150, 325, 100, 70, 20, 50, 255, 255));
    REQUIRE(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND));
    dst = (SDL_Rect){ 170, 345, 100, 70 };
    REQUIRE(SDL_RenderCopyEx(renderer, texture, NULL, &dst, -15.0, NULL,
                             SDL_FLIP_NONE));
    REQUIRE(fill(renderer, 215, 365, 80, 55, 20, 255, 70, 255));
    REQUIRE(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE));
    REQUIRE(fill(renderer, 8, 8, 18, 6, 40, 80, 255, 255));
    return 0;
}

static int draw_page3(SDL_Renderer *renderer, SDL_Texture *texture)
{
    REQUIRE(reset_page(renderer, 0, 0, 0));
    REQUIRE(draw_panel(renderer, texture, 0));
    REQUIRE(draw_panel(renderer, texture, 320));
    REQUIRE(SDL_RenderSetViewport(renderer, NULL));
    REQUIRE(SDL_RenderSetClipRect(renderer, NULL));
    REQUIRE(fill(renderer, 307, 0, 3, OUTPUT_H, 255, 255, 255, 255));
    REQUIRE(fill(renderer, 330, 8, 18, 6, 40, 80, 255, 255));
    return 0;
}

static int expect_geometry_failure(SDL_Renderer *renderer, SDL_Texture *texture,
                                   const float *xy, const float *uv,
                                   const void *indices, int num_indices,
                                   int size_indices,
                                   const char *name)
{
    static const SDL_Color white[3] = {
        { 255, 255, 255, 255 }, { 255, 255, 255, 255 }, { 255, 255, 255, 255 }
    };

    SDL_ClearError();
    if (SDL_RenderGeometryRaw(renderer, texture, xy, 2 * (int)sizeof(float),
                              white, (int)sizeof(SDL_Color), uv,
                              2 * (int)sizeof(float), 3, indices, num_indices,
                              size_indices) == 0) {
        printf("FAIL: %s was unexpectedly accepted\n", name);
        return -1;
    }
    printf("Rejected %s: %s\n", name, SDL_GetError());
    return 0;
}

static float float_from_bits(Uint32 bits)
{
    float value;

    SDL_memcpy(&value, &bits, sizeof(value));
    return value;
}

static int run_rectangle_api_checks(SDL_Renderer *renderer)
{
    SDL_FRect rect = { 0.0f, 0.0f, 1.0f, 1.0f };

    rect.w = float_from_bits(0x7fc00000u);
    SDL_ClearError();
    if (SDL_RenderFillRectF(renderer, &rect) == 0) {
        printf("FAIL: non-finite rectangle width was unexpectedly accepted\n");
        return -1;
    }
    printf("Rejected non-finite rectangle width: %s\n", SDL_GetError());

    rect.w = 1.0f;
    rect.h = float_from_bits(0x7f800000u);
    SDL_ClearError();
    if (SDL_RenderFillRectF(renderer, &rect) == 0) {
        printf("FAIL: non-finite rectangle height was unexpectedly accepted\n");
        return -1;
    }
    printf("Rejected non-finite rectangle height: %s\n", SDL_GetError());

    rect.w = 0.0f;
    rect.h = 1.0f;
    REQUIRE(SDL_RenderFillRectF(renderer, &rect));
    rect.w = 1.0f;
    rect.h = -1.0f;
    REQUIRE(SDL_RenderFillRectF(renderer, &rect));
    printf("Accepted finite empty rectangles\n");
    return 0;
}

static int run_geometry_api_checks(SDL_Renderer *renderer, SDL_Texture *texture)
{
    static const float xy[6] = { 8.0f, 450.0f, 18.0f, 450.0f, 8.0f, 460.0f };
    static const float uv[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
    static const SDL_Color colors[3] = {
        { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }
    };
    static const Uint8 indices8[3] = { 0, 1, 2 };
    static const Uint16 indices16[3] = { 0, 1, 2 };
    static const Uint32 indices32[3] = { 0, 1, 2 };
    static const Uint8 bad_indices[3] = { 0, 1, 3 };
    float bad_xy[6];
    float bad_uv[6];

    REQUIRE(SDL_RenderGeometryRaw(renderer, NULL, xy, 2 * (int)sizeof(float),
                                  colors, (int)sizeof(SDL_Color), NULL, 0, 3,
                                  NULL, 0, 0));
    printf("Accepted valid unindexed geometry\n");
    REQUIRE(SDL_RenderGeometryRaw(renderer, texture, xy, 2 * (int)sizeof(float),
                                  colors, (int)sizeof(SDL_Color), uv,
                                  2 * (int)sizeof(float), 3, indices8, 3, 1));
    REQUIRE(SDL_RenderGeometryRaw(renderer, texture, xy, 2 * (int)sizeof(float),
                                  colors, (int)sizeof(SDL_Color), uv,
                                  2 * (int)sizeof(float), 3, indices16, 3, 2));
    REQUIRE(SDL_RenderGeometryRaw(renderer, texture, xy, 2 * (int)sizeof(float),
                                  colors, (int)sizeof(SDL_Color), uv,
                                  2 * (int)sizeof(float), 3, indices32, 3, 4));

    SDL_memcpy(bad_xy, xy, sizeof(bad_xy));
    bad_xy[0] = float_from_bits(0x7fc00000u);
    REQUIRE(expect_geometry_failure(renderer, texture, bad_xy, uv, indices8, 3, 1,
                                    "non-finite position"));
    SDL_memcpy(bad_uv, uv, sizeof(bad_uv));
    bad_uv[0] = float_from_bits(0x7fc00000u);
    REQUIRE(expect_geometry_failure(renderer, texture, xy, bad_uv, indices8, 3, 1,
                                    "non-finite UV"));
    REQUIRE(expect_geometry_failure(renderer, texture, xy, uv, indices8, 3, 3,
                                    "invalid index size"));
    REQUIRE(expect_geometry_failure(renderer, texture, xy, uv, bad_indices, 3, 1,
                                    "out-of-range index"));
    REQUIRE(expect_geometry_failure(renderer, texture, xy, uv, NULL, 3, 4,
                                    "null indices with positive index count"));
    REQUIRE(SDL_RenderFlush(renderer));
    return 0;
}

static int present_page(SDL_Renderer *renderer, SDL_Texture *nearest,
                        SDL_Texture *linear, int page)
{
    int frame;

    for (frame = 0; frame < 3; ++frame) {
        int rc;

        if (page == 0) {
            rc = draw_page1(renderer, nearest, linear);
        } else if (page == 1) {
            rc = draw_page2(renderer, nearest);
        } else {
            rc = draw_page3(renderer, nearest);
        }
        if (rc < 0) {
            return -1;
        }
        SDL_RenderPresent(renderer);
    }

#ifdef PVR_COPY_EX_HARDWARE_SCREENSHOT
    vid_waitvbl();
    if (vid_screen_shot(hardware_paths[page]) < 0) {
        printf("FAIL: could not save hardware page %d to %s\n",
               page + 1, hardware_paths[page]);
        return 1;
    }
    printf("Saved hardware page %d to %s\n", page + 1,
           hardware_paths[page]);
    SDL_Delay(500);
#else
    printf("Holding copy/copy-ex visual page %d/%d for %d ms\n",
           page + 1, PAGE_COUNT, PAGE_HOLD_MS);
    SDL_Delay(PAGE_HOLD_MS);
#endif
    return 0;
}

int main(int argc, char **argv)
{
    Uint16 pixels[TEX_H][TEX_W];
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *nearest = NULL;
    SDL_Texture *linear = NULL;
    int driver;
    int page;
    int first_page = 0;
    int last_page = PAGE_COUNT;
    int capture_failed = 0;
    int result = 1;

    (void)argc;
    (void)argv;
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
                      (cont_btn_callback_t)arch_exit);

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    driver = find_pvr_driver();
    if (driver < 0) {
        printf("FAIL: Dreamcast PVR renderer is unavailable\n");
        goto done;
    }
    window = SDL_CreateWindow("PVR copy/copy-ex", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, OUTPUT_W, OUTPUT_H,
                              SDL_WINDOW_SHOWN);
    if (!window) {
        printf("FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        goto done;
    }
    renderer = SDL_CreateRenderer(window, driver, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("FAIL: SDL_CreateRenderer: %s\n", SDL_GetError());
        goto done;
    }

    make_texture(pixels);
    nearest = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, TEX_W, TEX_H);
    linear = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_STATIC, TEX_W, TEX_H);
    if (!nearest || !linear) {
        printf("FAIL: SDL_CreateTexture: %s\n", SDL_GetError());
        goto done;
    }
    if (SDL_UpdateTexture(nearest, NULL, pixels, (int)sizeof(pixels[0])) < 0 ||
        SDL_UpdateTexture(linear, NULL, pixels, (int)sizeof(pixels[0])) < 0 ||
        SDL_SetTextureScaleMode(nearest, SDL_ScaleModeNearest) < 0 ||
        SDL_SetTextureScaleMode(linear, SDL_ScaleModeLinear) < 0 ||
        SDL_SetTextureBlendMode(nearest, SDL_BLENDMODE_NONE) < 0 ||
        SDL_SetTextureBlendMode(linear, SDL_BLENDMODE_NONE) < 0) {
        printf("FAIL: texture setup: %s\n", SDL_GetError());
        goto done;
    }

    if (run_rectangle_api_checks(renderer) < 0 ||
        run_geometry_api_checks(renderer, nearest) < 0) {
        goto done;
    }
#ifdef PVR_COPY_EX_VISUAL_PAGE
    if (PVR_COPY_EX_VISUAL_PAGE < 1 || PVR_COPY_EX_VISUAL_PAGE > PAGE_COUNT) {
        printf("FAIL: PVR_COPY_EX_VISUAL_PAGE must be in 1..%d\n", PAGE_COUNT);
        goto done;
    }
    first_page = PVR_COPY_EX_VISUAL_PAGE - 1;
    last_page = PVR_COPY_EX_VISUAL_PAGE;
#endif
    for (page = first_page; page < last_page; ++page) {
        const int page_result = present_page(renderer, nearest, linear, page);

        if (page_result < 0) {
            printf("FAIL: visual page %d: %s\n", page + 1, SDL_GetError());
            goto done;
        }
        if (page_result > 0) {
            capture_failed = 1;
        }
    }
    if (capture_failed) {
        printf("FAIL: one or more hardware framebuffer captures failed\n");
        goto done;
    }

    printf("PASS (API/runtime): pvr_copy_ex completed page range %d..%d\n",
           first_page + 1, last_page);
    result = 0;

done:
    SDL_DestroyTexture(linear);
    SDL_DestroyTexture(nearest);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
