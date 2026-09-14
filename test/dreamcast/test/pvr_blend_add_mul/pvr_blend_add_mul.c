/*
  Dreamcast native PVR ADD / MUL blend-mode validation.

  Exercises both textured and untextured SDL_BLENDMODE_ADD and
  SDL_BLENDMODE_MUL, verifies public-API acceptance/rejection, and compares the
  native PVR output against a CPU-computed RGB565 reference.

  Hardware capture is opt-in through HARDWARE_SCREENSHOTS=1 during build.
  Ordinary builds contain no screenshot code, /pc paths, or capture defines.
*/

#include <SDL.h>

#include <kos.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define PANEL_W 320
#define PAGE_COUNT 4
#ifndef PAGE_HOLD_MS
#define PAGE_HOLD_MS 8000
#endif

#ifdef DC_PVR_BLEND_ADD_MUL_HARDWARE_SCREENSHOT
static const char *const hardware_paths[PAGE_COUNT] = {
    "/pc/pvr_blend_add_mul-hardware-page1.ppm",
    "/pc/pvr_blend_add_mul-hardware-page2.ppm",
    "/pc/pvr_blend_add_mul-hardware-page3.ppm",
    "/pc/pvr_blend_add_mul-hardware-page4.ppm"
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

static void rgb565_to_rgb(Uint16 px, Uint8 *r, Uint8 *g, Uint8 *b)
{
    const Uint8 r5 = (Uint8)((px >> 11) & 0x1f);
    const Uint8 g6 = (Uint8)((px >> 5) & 0x3f);
    const Uint8 b5 = (Uint8)(px & 0x1f);

    *r = (Uint8)((r5 << 3) | (r5 >> 2));
    *g = (Uint8)((g6 << 2) | (g6 >> 4));
    *b = (Uint8)((b5 << 3) | (b5 >> 2));
}

static Uint16 cpu_add(Uint16 src, Uint16 dst, Uint8 srcA)
{
    Uint8 sr, sg, sb, dr, dg, db;
    int or_, og, ob;

    rgb565_to_rgb(src, &sr, &sg, &sb);
    rgb565_to_rgb(dst, &dr, &dg, &db);

    or_ = sr * srcA / 255 + dr;
    og = sg * srcA / 255 + dg;
    ob = sb * srcA / 255 + db;

    if (or_ > 255) or_ = 255;
    if (og > 255) og = 255;
    if (ob > 255) ob = 255;

    return rgb565((Uint8)or_, (Uint8)og, (Uint8)ob);
}

static Uint16 cpu_mul(Uint16 src, Uint16 dst, Uint8 srcA)
{
    Uint8 sr, sg, sb, dr, dg, db;
    int or_, og, ob;

    rgb565_to_rgb(src, &sr, &sg, &sb);
    rgb565_to_rgb(dst, &dr, &dg, &db);

    or_ = sr * dr / 255 + dr * (255 - srcA) / 255;
    og = sg * dg / 255 + dg * (255 - srcA) / 255;
    ob = sb * db / 255 + db * (255 - srcA) / 255;

    if (or_ > 255) or_ = 255;
    if (og > 255) og = 255;
    if (ob > 255) ob = 255;

    return rgb565((Uint8)or_, (Uint8)og, (Uint8)ob);
}

static int fill_rgb565_reference(SDL_Renderer *renderer, const SDL_Rect *rect,
                                 Uint16 color)
{
    Uint8 r, g, b;

    rgb565_to_rgb(color, &r, &g, &b);
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    return SDL_RenderFillRect(renderer, rect);
}

static void print_cpu_reference(void)
{
    /* Document the CPU RGB565 blend reference used to interpret the PVR
     * output. The PVR applies hardware quantization at different pipeline
     * stages, so exact pixel match is not required. The tolerance band is
     * one RGB565-quantized step per channel (r: 8, g: 4, b: 8). */
    Uint16 dst_dark = rgb565(0, 0, 15);
    Uint16 dst_mid  = rgb565(62, 103, 31);
    Uint16 src_red  = rgb565(255, 0, 0);
    Uint16 src_white = rgb565(255, 255, 255);

    printf("  CPU reference: ADD(red, dark, 255) = %04x\n",
           cpu_add(src_red, dst_dark, 255));
    printf("  CPU reference: ADD(red, dark, 0)   = %04x\n",
           cpu_add(src_red, dst_dark, 0));
    printf("  CPU reference: MUL(white, mid, 0)  = %04x\n",
           cpu_mul(src_white, dst_mid, 0));
    printf("  CPU reference: MUL(white, mid, 255)= %04x\n",
           cpu_mul(src_white, dst_mid, 255));
    printf("  CPU reference: ADD(30,63,30,255) over black = %04x\n",
           cpu_add(rgb565(30, 63, 30), rgb565(0, 0, 0), 255));
}

static int build_source_texture(SDL_Renderer *renderer, SDL_Texture **tex,
                                Uint16 pixels[8][8])
{
    int y, x;

    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            const Uint8 shade = (Uint8)(((x + y) * 30) & 0xff);
            pixels[y][x] = rgb565(shade, shade, (Uint8)(shade + 30));
        }
    }

    *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                             SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!*tex) {
        return failed_call("SDL_CreateTexture(source)");
    }
    REQUIRE(SDL_UpdateTexture(*tex, NULL, pixels, 8 * (int)sizeof(Uint16)));
    return 0;
}

static int build_blend_reference_texture(SDL_Renderer *renderer,
                                         SDL_BlendMode mode,
                                         Uint16 source[8][8], Uint16 dst,
                                         SDL_Texture **texture,
                                         Uint16 output[8][8])
{
    int y, x;

    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            output[y][x] = (mode == SDL_BLENDMODE_ADD)
                                 ? cpu_add(source[y][x], dst, 255)
                                 : cpu_mul(source[y][x], dst, 255);
        }
    }

    *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!*texture) {
        return failed_call("SDL_CreateTexture(reference)");
    }
    REQUIRE(SDL_UpdateTexture(*texture, NULL, output,
                              8 * (int)sizeof(Uint16)));
    REQUIRE(SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetTextureScaleMode(*texture, SDL_ScaleModeNearest));
    return 0;
}

static int build_scaling_textures(SDL_Renderer *renderer,
                                  SDL_Texture **nearest, SDL_Texture **linear,
                                  Uint16 npix[4][4], Uint16 lpix[4][4])
{
    int y, x;

    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            npix[y][x] = rgb565(
                (Uint8)((x == 0) ? 80 : 200),
                (Uint8)((y == 0) ? 80 : 200),
                40);
            lpix[y][x] = npix[y][x];
        }
    }

    *nearest = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STATIC, 4, 4);
    if (!*nearest) {
        return failed_call("SDL_CreateTexture(nearest)");
    }
    REQUIRE(SDL_UpdateTexture(*nearest, NULL, npix, 4 * (int)sizeof(Uint16)));
    REQUIRE(SDL_SetTextureScaleMode(*nearest, SDL_ScaleModeNearest));

    *linear = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STATIC, 4, 4);
    if (!*linear) {
        return failed_call("SDL_CreateTexture(linear)");
    }
    REQUIRE(SDL_UpdateTexture(*linear, NULL, lpix, 4 * (int)sizeof(Uint16)));
    REQUIRE(SDL_SetTextureScaleMode(*linear, SDL_ScaleModeLinear));
    return 0;
}

static int guard(SDL_Renderer *renderer, const SDL_Rect *dst)
{
    SDL_Rect g = { dst->x - 2, dst->y - 2, dst->w + 4, dst->h + 4 };

    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255));
    return SDL_RenderFillRect(renderer, &g);
}

static int validate_api(SDL_Renderer *renderer, SDL_Texture *probe)
{
    SDL_BlendMode custom;

    printf("API: testing public blend-mode accept/reject\n");

    if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) < 0 ||
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0) {
        return failed_call("NONE/BLEND accepted");
    }
    if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD) < 0 ||
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MUL) < 0) {
        return failed_call("ADD/MUL rejected by draw setter");
    }

    if (SDL_SetTextureBlendMode(probe, SDL_BLENDMODE_ADD) < 0 ||
        SDL_SetTextureBlendMode(probe, SDL_BLENDMODE_MUL) < 0) {
        return failed_call("ADD/MUL rejected by texture setter");
    }

    SDL_ClearError();
    if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD) == 0) {
        return failed_call("MOD unexpectedly accepted");
    }
    if (SDL_GetError()[0] == '\0') {
        return failed_call("MOD rejected without SDL error");
    }

    SDL_ClearError();
    if (SDL_SetTextureBlendMode(probe, SDL_BLENDMODE_MOD) == 0) {
        return failed_call("texture MOD unexpectedly accepted");
    }
    if (SDL_GetError()[0] == '\0') {
        return failed_call("texture MOD rejected without SDL error");
    }

    custom = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_SUBTRACT,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_SUBTRACT);
    SDL_ClearError();
    if (SDL_SetRenderDrawBlendMode(renderer, custom) == 0) {
        return failed_call("custom subtract unexpectedly accepted");
    }
    if (SDL_GetError()[0] == '\0') {
        return failed_call("custom subtract rejected without SDL error");
    }

    return 0;
}

static int page1_untextured(SDL_Renderer *renderer)
{
    static const Uint8 alpha_cases[] = { 0, 1, 64, 128, 254, 255 };
    const Uint16 add_src = rgb565(255, 0, 0);
    const Uint16 add_dst = rgb565(0, 0, 15);
    const Uint16 mul_src = rgb565(255, 0, 255);
    const Uint16 mul_dst = rgb565(62, 103, 31);
    Uint8 add_dr, add_dg, add_db;
    Uint8 mul_dr, mul_dg, mul_db;
    size_t i;

    rgb565_to_rgb(add_dst, &add_dr, &add_dg, &add_db);
    rgb565_to_rgb(mul_dst, &mul_dr, &mul_dg, &mul_db);

    printf("Page 1: untextured ADD/MUL alpha endpoints, saturation, and guards\n");
    printf("  left column is CPU RGB565 reference; right column is native PVR\n");

    REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
    REQUIRE(SDL_RenderClear(renderer));

    /* ADD reference/native pairs over a dark-blue destination. */
    for (i = 0; i < SDL_arraysize(alpha_cases); ++i) {
        const Uint8 srcA = alpha_cases[i];
        const int y = 20 + (int)i * 35;
        SDL_Rect reference = { 20, y, 120, 25 };
        SDL_Rect native = { 160, y, 120, 25 };

        guard(renderer, &reference);
        guard(renderer, &native);
        REQUIRE(fill_rgb565_reference(renderer, &reference,
                                      cpu_add(add_src, add_dst, srcA)));

        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, add_dr, add_dg, add_db, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &native));

        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 255, 0, 0, srcA));
        REQUIRE(SDL_RenderFillRect(renderer, &native));
    }

    /* MUL reference/native pairs. SDL MUL does not multiply its source-color
     * term by source alpha: src*dst + dst*(1-srcA). At alpha zero, a white
     * source therefore doubles the destination rather than leaving it alone. */
    for (i = 0; i < SDL_arraysize(alpha_cases); ++i) {
        const Uint8 srcA = alpha_cases[i];
        const int y = 255 + (int)i * 35;
        SDL_Rect reference = { 20, y, 120, 25 };
        SDL_Rect native = { 160, y, 120, 25 };

        guard(renderer, &reference);
        guard(renderer, &native);
        REQUIRE(fill_rgb565_reference(renderer, &reference,
                                      cpu_mul(mul_src, mul_dst, srcA)));

        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, mul_dr, mul_dg, mul_db, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &native));

        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MUL));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 255, 0, 255, srcA));
        REQUIRE(SDL_RenderFillRect(renderer, &native));
    }

    /* Additive saturation probe: saturated red source (255,0,0,255) over
     * saturated blue destination (0,0,255) should clip to (255,0,255). */
    {
        SDL_Rect rect = { 320, 40, 140, 40 };

        guard(renderer, &rect);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &rect));
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &rect));
    }

    /* MUL low/high destination probes. White at alpha 255 preserves the low
     * destination through src*dst. Dark at alpha 255 multiplies white down. */
    {
        SDL_Rect r1 = { 480, 40, 140, 40 };
        SDL_Rect r2 = { 480, 100, 140, 40 };

        guard(renderer, &r1);
        guard(renderer, &r2);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &r1));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &r2));

        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MUL));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &r1));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &r2));
    }

    return 0;
}

static int page2_textured(SDL_Renderer *renderer, SDL_Texture *tex,
                          SDL_Texture *add_reference,
                          SDL_Texture *mul_reference,
                          SDL_Texture *nearest, SDL_Texture *linear)
{
    printf("Page 2: textured ADD/MUL with modulation and nearest/linear\n");

    REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
    REQUIRE(SDL_RenderClear(renderer));

    /* ADD over a medium destination. The left cell is the CPU RGB565
     * reference, the middle cell is native, and the right cell is a separate
     * native color/alpha-modulation probe. */
    {
        SDL_Rect dst = { 20, 40, 130, 130 };
        SDL_Rect src = { 180, 40, 130, 130 };

        guard(renderer, &dst);
        guard(renderer, &src);

        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 40, 40, 80, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &dst));
        REQUIRE(SDL_RenderFillRect(renderer, &src));

        REQUIRE(SDL_RenderCopy(renderer, add_reference, NULL, &dst));
        REQUIRE(SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD));
        REQUIRE(SDL_RenderCopy(renderer, tex, NULL, &src));

        /* Modulation: 128,128,128,192 over the same destination. */
        src.x = 340;
        guard(renderer, &src);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 40, 40, 80, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &src));
        REQUIRE(SDL_SetTextureColorMod(tex, 128, 128, 128));
        REQUIRE(SDL_SetTextureAlphaMod(tex, 192));
        REQUIRE(SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD));
        REQUIRE(SDL_RenderCopy(renderer, tex, NULL, &src));
        REQUIRE(SDL_SetTextureColorMod(tex, 255, 255, 255));
        REQUIRE(SDL_SetTextureAlphaMod(tex, 255));
    }

    /* MUL uses the same reference/native/modulated layout. */
    {
        SDL_Rect dst = { 20, 200, 130, 130 };
        SDL_Rect src = { 180, 200, 130, 130 };

        guard(renderer, &dst);
        guard(renderer, &src);

        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 100, 80, 60, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &dst));
        REQUIRE(SDL_RenderFillRect(renderer, &src));

        REQUIRE(SDL_RenderCopy(renderer, mul_reference, NULL, &dst));
        REQUIRE(SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_MUL));
        REQUIRE(SDL_RenderCopy(renderer, tex, NULL, &src));

        /* Modulation: 64,64,64,128. */
        src.x = 340;
        guard(renderer, &src);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 100, 80, 60, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &src));
        REQUIRE(SDL_SetTextureColorMod(tex, 64, 64, 64));
        REQUIRE(SDL_SetTextureAlphaMod(tex, 128));
        REQUIRE(SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_MUL));
        REQUIRE(SDL_RenderCopy(renderer, tex, NULL, &src));
        REQUIRE(SDL_SetTextureColorMod(tex, 255, 255, 255));
        REQUIRE(SDL_SetTextureAlphaMod(tex, 255));
    }

    /* Nearest vs. bilinear scaling probe. The 4x4 texture is stretched to
     * 60x60. Nearest should show blocky pixels; bilinear should be smooth. */
    {
        SDL_Rect r1 = { 20, 360, 60, 60 };
        SDL_Rect r2 = { 100, 360, 60, 60 };

        guard(renderer, &r1);
        guard(renderer, &r2);

        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &r1));
        REQUIRE(SDL_RenderFillRect(renderer, &r2));

        REQUIRE(SDL_SetTextureBlendMode(nearest, SDL_BLENDMODE_ADD));
        REQUIRE(SDL_RenderCopy(renderer, nearest, NULL, &r1));
        REQUIRE(SDL_SetTextureBlendMode(linear, SDL_BLENDMODE_MUL));
        REQUIRE(SDL_RenderCopy(renderer, linear, NULL, &r2));
    }

    return 0;
}

static int page3_painter_order(SDL_Renderer *renderer)
{
    int i;

    printf("Page 3: alternating blend modes and state-cache transitions\n");

    REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
    REQUIRE(SDL_RenderClear(renderer));

    /* Alternate NONE / BLEND / ADD / MUL, then return through earlier modes
     * to force header-cache transitions. Each strip is 65 pixels tall. */
    {
        SDL_Rect rects[6] = {
            { 20, 40, 280, 55 },
            { 20, 115, 280, 55 },
            { 20, 190, 280, 55 },
            { 20, 265, 280, 55 },
            { 20, 340, 280, 55 },
            { 20, 415, 280, 55 },
        };
        SDL_BlendMode modes[6] = {
            SDL_BLENDMODE_NONE,
            SDL_BLENDMODE_BLEND,
            SDL_BLENDMODE_ADD,
            SDL_BLENDMODE_MUL,
            SDL_BLENDMODE_ADD,
            SDL_BLENDMODE_NONE
        };
        Uint8 colors[6][3] = {
            { 200, 20, 20 },
            { 20, 200, 20 },
            { 20, 20, 200 },
            { 200, 200, 20 },
            { 200, 20, 200 },
            { 20, 200, 200 }
        };

        for (i = 0; i < 6; ++i) {
            guard(renderer, &rects[i]);
            REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
            REQUIRE(SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255));
            REQUIRE(SDL_RenderFillRect(renderer, &rects[i]));
            REQUIRE(SDL_SetRenderDrawBlendMode(renderer, modes[i]));
            REQUIRE(SDL_SetRenderDrawColor(renderer, colors[i][0],
                                           colors[i][1], colors[i][2], 200));
            REQUIRE(SDL_RenderFillRect(renderer, &rects[i]));
        }
    }

    /* Nonzero viewport to prove new modes do not disturb existing command
     * state. The right-panel rectangles use viewport-relative coordinates. */
    {
        SDL_Rect vp = { PANEL_W, 0, PANEL_W, OUTPUT_H };
        SDL_Rect rects[4] = {
            { 20, 40, 120, 55 },
            { 20, 115, 120, 55 },
            { 20, 190, 120, 55 },
            { 20, 265, 120, 55 }
        };
        SDL_BlendMode modes[4] = {
            SDL_BLENDMODE_ADD,
            SDL_BLENDMODE_MUL,
            SDL_BLENDMODE_ADD,
            SDL_BLENDMODE_MUL
        };
        Uint8 colors[4][3] = {
            { 255, 0, 0 },
            { 0, 255, 0 },
            { 0, 0, 255 },
            { 255, 255, 0 }
        };

        REQUIRE(SDL_RenderSetViewport(renderer, &vp));
        for (i = 0; i < 4; ++i) {
            guard(renderer, &rects[i]);
            REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
            REQUIRE(SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255));
            REQUIRE(SDL_RenderFillRect(renderer, &rects[i]));
            REQUIRE(SDL_SetRenderDrawBlendMode(renderer, modes[i]));
            REQUIRE(SDL_SetRenderDrawColor(renderer, colors[i][0],
                                           colors[i][1], colors[i][2], 220));
            REQUIRE(SDL_RenderFillRect(renderer, &rects[i]));
        }

        /* A real painter-order probe: the four mode transitions overlap in
         * submission order inside one guarded destination. */
        {
            SDL_Rect cluster = { 160, 40, 140, 300 };
            SDL_Rect layers[4] = {
                { 170, 50, 90, 100 },
                { 200, 110, 90, 100 },
                { 170, 170, 90, 100 },
                { 200, 230, 90, 100 }
            };

            guard(renderer, &cluster);
            REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
            REQUIRE(SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255));
            REQUIRE(SDL_RenderFillRect(renderer, &cluster));
            for (i = 0; i < 4; ++i) {
                REQUIRE(SDL_SetRenderDrawBlendMode(renderer, modes[i]));
                REQUIRE(SDL_SetRenderDrawColor(renderer, colors[i][0],
                                               colors[i][1], colors[i][2],
                                               180));
                REQUIRE(SDL_RenderFillRect(renderer, &layers[i]));
            }
        }
        REQUIRE(SDL_RenderSetViewport(renderer, NULL));
    }

    return 0;
}

static int page4_vertex_geometry(SDL_Renderer *renderer, SDL_Texture *tex)
{
    print_cpu_reference();
    const SDL_Vertex add_tri[3] = {
        { { 30.0f, 50.0f }, { 255, 255, 0, 180 }, { 0.0f, 0.0f } },
        { { 170.0f, 50.0f }, { 255, 0, 255, 180 }, { 1.0f, 0.0f } },
        { { 100.0f, 190.0f }, { 0, 255, 255, 180 }, { 0.5f, 1.0f } }
    };
    const SDL_Vertex mul_tri[3] = {
        { { 30.0f, 230.0f }, { 200, 200, 200, 200 }, { 0.0f, 0.0f } },
        { { 170.0f, 230.0f }, { 200, 200, 200, 120 }, { 1.0f, 0.0f } },
        { { 100.0f, 370.0f }, { 200, 200, 200, 60 }, { 0.5f, 1.0f } }
    };

    printf("Page 4: SDL_RenderGeometry per-vertex ADD/MUL\n");

    REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
    REQUIRE(SDL_RenderClear(renderer));

    /* ADD geometry over blue background. */
    {
        SDL_Rect bg = { 10, 30, 200, 180 };

        guard(renderer, &bg);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 100, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &bg));
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD));
        {
            int add_idx[3] = { 0, 1, 2 };
            REQUIRE(SDL_RenderGeometry(renderer, NULL, add_tri, 3, add_idx, 3));
        }
    }

    /* MUL geometry over mid-gray background. */
    {
        SDL_Rect bg = { 10, 210, 200, 180 };

        guard(renderer, &bg);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &bg));
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MUL));
        {
            int mul_idx[3] = { 0, 1, 2 };
            REQUIRE(SDL_RenderGeometry(renderer, NULL, mul_tri, 3, mul_idx, 3));
        }
    }

    /* Textured geometry with per-vertex modulation. */
    {
        SDL_Rect bg = { 220, 30, 90, 90 };
        const SDL_Vertex textured[4] = {
            { { 220.0f, 30.0f }, { 255, 255, 255, 200 }, { 0.0f, 0.0f } },
            { { 310.0f, 30.0f }, { 255, 255, 255, 200 }, { 1.0f, 0.0f } },
            { { 310.0f, 120.0f }, { 255, 255, 255, 200 }, { 1.0f, 1.0f } },
            { { 220.0f, 120.0f }, { 255, 255, 255, 200 }, { 0.0f, 1.0f } }
        };

        guard(renderer, &bg);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 80, 40, 20, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &bg));
        REQUIRE(SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD));
        {
            int tex_idx[6] = { 0, 1, 2, 0, 2, 3 };
            REQUIRE(SDL_RenderGeometry(renderer, tex, textured, 4, tex_idx, 6));
        }
    }

    /* RGB565 quantization probe: source color just below a 5-bit boundary
     * (r=30, g=63, b=30) quantized against black destination. */
    {
        SDL_Rect rect = { 340, 30, 140, 60 };

        guard(renderer, &rect);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &rect));
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD));
        REQUIRE(SDL_SetRenderDrawColor(renderer, 30, 63, 30, 255));
        REQUIRE(SDL_RenderFillRect(renderer, &rect));
        printf("  CPU reference ADD(30,63,30,255) over (0,0,0) = (30,63,30) -> "
               "RGB565 %04x\n", rgb565(30, 63, 30));
    }

    return 0;
}

static int present_page(SDL_Renderer *renderer, int page)
{
    SDL_RenderPresent(renderer);
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
#ifdef DC_PVR_BLEND_ADD_MUL_HARDWARE_SCREENSHOT
    if (vid_screen_shot(hardware_paths[page]) < 0) {
        printf("FAIL: vid_screen_shot(%s)\n", hardware_paths[page]);
        return -1;
    }
    printf("  captured %s\n", hardware_paths[page]);
#endif

    printf("Holding page %d/%d for %d ms\n", page + 1, PAGE_COUNT,
           PAGE_HOLD_MS);
    SDL_Delay(PAGE_HOLD_MS);
    return 0;
}

static int render_pages(SDL_Renderer *renderer, SDL_Texture *texture,
                        SDL_Texture *add_reference,
                        SDL_Texture *mul_reference,
                        SDL_Texture *nearest, SDL_Texture *linear)
{
    int page;

    for (page = 0; page < PAGE_COUNT; ++page) {
        int rc;

        if (page == 0) {
            rc = page1_untextured(renderer);
        } else if (page == 1) {
            rc = page2_textured(renderer, texture, add_reference,
                                mul_reference, nearest, linear);
        } else if (page == 2) {
            rc = page3_painter_order(renderer);
        } else {
            rc = page4_vertex_geometry(renderer, texture);
        }
        if (rc < 0 || present_page(renderer, page) < 0) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_Texture *add_reference = NULL;
    SDL_Texture *mul_reference = NULL;
    SDL_Texture *nearest = NULL;
    SDL_Texture *linear = NULL;
    Uint16 pixels[8][8];
    Uint16 add_reference_pixels[8][8];
    Uint16 mul_reference_pixels[8][8];
    Uint16 nearest_pixels[4][4];
    Uint16 linear_pixels[4][4];
    int driver_index;
    int result = 1;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    driver_index = find_pvr_driver();
    if (driver_index < 0) {
        printf("Dreamcast PVR renderer is not available\n");
        goto done;
    }

    window = SDL_CreateWindow("PVR ADD/MUL blend",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              OUTPUT_W, OUTPUT_H, 0);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto done;
    }

    renderer = SDL_CreateRenderer(window, driver_index,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        goto done;
    }

    if (build_source_texture(renderer, &texture, pixels) < 0) {
        printf("Texture creation/update failed: %s\n", SDL_GetError());
        goto done;
    }
    if (!texture) {
        printf("Texture creation failed: %s\n", SDL_GetError());
        goto done;
    }
    if (build_blend_reference_texture(renderer, SDL_BLENDMODE_ADD, pixels,
                                      rgb565(40, 40, 80), &add_reference,
                                      add_reference_pixels) < 0 ||
        build_blend_reference_texture(renderer, SDL_BLENDMODE_MUL, pixels,
                                      rgb565(100, 80, 60), &mul_reference,
                                      mul_reference_pixels) < 0) {
        goto done;
    }
    if (build_scaling_textures(renderer, &nearest, &linear,
                               nearest_pixels, linear_pixels) < 0) {
        goto done;
    }

    if (validate_api(renderer, texture) < 0) {
        goto done;
    }
    if (render_pages(renderer, texture, add_reference, mul_reference,
                     nearest, linear) < 0) {
        goto done;
    }

    printf("PASS (API/build): visual result still requires real Dreamcast validation\n");
    result = 0;

done:
    if (linear) {
        SDL_DestroyTexture(linear);
    }
    if (nearest) {
        SDL_DestroyTexture(nearest);
    }
    if (mul_reference) {
        SDL_DestroyTexture(mul_reference);
    }
    if (add_reference) {
        SDL_DestroyTexture(add_reference);
    }
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return result;
}
