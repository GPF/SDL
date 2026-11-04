/* Bounded physical probe for PVR destination preservation and texture edges. */

#include <SDL.h>
#include <kos.h>

#include <inttypes.h>
#include <stdio.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define PAGE_COUNT 12
#ifndef PAGE_HOLD_MS
#define PAGE_HOLD_MS 8000
#endif

#ifdef DC_PVR_RTT_MICROPROBE_HARDWARE_SCREENSHOT
static const char *const capture_paths[PAGE_COUNT] = {
    "/pc/pvr_rtt_microprobe-final-page1.ppm",
    "/pc/pvr_rtt_microprobe-final-page2.ppm",
    "/pc/pvr_rtt_microprobe-final-page3.ppm",
    "/pc/pvr_rtt_microprobe-final-page4.ppm",
    "/pc/pvr_rtt_microprobe-final-page5.ppm",
    "/pc/pvr_rtt_microprobe-final-page6.ppm",
    "/pc/pvr_rtt_microprobe-final-page7.ppm",
    "/pc/pvr_rtt_microprobe-final-page8.ppm",
    "/pc/pvr_rtt_microprobe-1to1-page9.ppm",
    "/pc/pvr_rtt_microprobe-1to1-page10.ppm",
    "/pc/pvr_rtt_microprobe-max-inset-page11.ppm",
    "/pc/pvr_rtt_microprobe-max-inset-page12.ppm"
};
#endif

typedef struct TimingStats
{
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    int count;
} TimingStats;

static int failures;

static int failed_call(const char *call)
{
    printf("FAIL: %s: %s\n", call, SDL_GetError());
    ++failures;
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return failed_call(#call); } while (0)

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

static int set_color(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    return 0;
}

static int fill(SDL_Renderer *renderer, int x, int y, int w, int h)
{
    SDL_Rect rect = { x, y, w, h };
    return SDL_RenderFillRect(renderer, &rect);
}

static int clear_to(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(set_color(renderer, r, g, b));
    REQUIRE(SDL_RenderClear(renderer));
    return 0;
}

static SDL_Texture *create_target(SDL_Renderer *renderer, int w, int h)
{
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                             SDL_TEXTUREACCESS_TARGET, w, h);

    if (!texture) {
        failed_call("SDL_CreateTexture(RGB565 TARGET)");
        return NULL;
    }
    if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) < 0) {
        SDL_DestroyTexture(texture);
        failed_call("SDL_SetTextureBlendMode(target, NONE)");
        return NULL;
    }
    return texture;
}

static int copy_full(SDL_Renderer *renderer, SDL_Texture *texture)
{
    return SDL_RenderCopy(renderer, texture, NULL, NULL);
}

static int finish_selected_target(SDL_Renderer *renderer)
{
    SDL_RenderPresent(renderer);
    return 0;
}

static void timing_add(TimingStats *stats, uint64_t elapsed)
{
    stats->total_ns += elapsed;
    if (stats->count == 0 || elapsed < stats->min_ns) stats->min_ns = elapsed;
    if (stats->count == 0 || elapsed > stats->max_ns) stats->max_ns = elapsed;
    ++stats->count;
}

static void timing_print(const char *label, const TimingStats *stats)
{
    const uint64_t average = stats->count ? stats->total_ns / (uint64_t)stats->count : 0;

    printf("TIMING %s count=%d min=%" PRIu64 " ns avg=%" PRIu64
           " ns max=%" PRIu64 " ns (avg %.3f ms)\n",
           label, stats->count, stats->min_ns, average, stats->max_ns,
           (double)average / 1000000.0);
}

static int seed_scene(SDL_Renderer *renderer, SDL_Texture *current,
                      SDL_Texture *next, TimingStats *stats)
{
    uint64_t start;

    start = timer_ns_gettime64();
    REQUIRE(SDL_SetRenderTarget(renderer, next));
    REQUIRE(copy_full(renderer, current));
    REQUIRE(finish_selected_target(renderer));
    timing_add(stats, timer_ns_gettime64() - start);
    return 0;
}

static int present_page(SDL_Renderer *renderer, int page, const char *name,
                        const char *expectation)
{
    SDL_RenderPresent(renderer);
    printf("PAGE %d/%d PRESENTED: %s\n", page + 1, PAGE_COUNT, name);
    printf("  VISUAL EXPECTATION: %s\n", expectation);
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
#ifdef DC_PVR_RTT_MICROPROBE_HARDWARE_SCREENSHOT
    if (vid_screen_shot(capture_paths[page]) < 0) {
        printf("  CAPTURE FAIL: %s\n", capture_paths[page]);
        ++failures;
        return -1;
    }
    printf("  CAPTURE OK: %s\n", capture_paths[page]);
#endif
    SDL_Delay(PAGE_HOLD_MS);
    return 0;
}

static int draw_two_markers(SDL_Renderer *renderer, int w, int h,
                            SDL_bool first, SDL_bool second)
{
    const int marker_w = w / 3;
    const int marker_h = h / 3;

    if (first) {
        REQUIRE(set_color(renderer, 230, 25, 25));
        REQUIRE(fill(renderer, w / 12, h / 10, marker_w, marker_h));
    }
    if (second) {
        REQUIRE(set_color(renderer, 20, 225, 50));
        REQUIRE(fill(renderer, w - marker_w - w / 12,
                     h - marker_h - h / 10, marker_w, marker_h));
    }
    return 0;
}

static int preservation_page(SDL_Renderer *renderer, int page, int w, int h,
                             int rounds, const char *timing_label,
                             const char *page_name)
{
    SDL_Texture *a = NULL;
    SDL_Texture *b = NULL;
    SDL_Texture *current;
    SDL_Texture *next;
    TimingStats stats = { 0, 0, 0, 0 };
    size_t before;
    size_t after_a;
    size_t after_b;
    int i;

    printf("PAGE %d: %s\n", page + 1, page_name);
    before = pvr_mem_available();
    a = create_target(renderer, w, h);
    after_a = pvr_mem_available();
    b = create_target(renderer, w, h);
    after_b = pvr_mem_available();
    printf("VRAM %dx%d before=%zu after_a=%zu after_b=%zu used_a=%zu used_b=%zu\n",
           w, h, before, after_a, after_b,
           before >= after_a ? before - after_a : 0,
           after_a >= after_b ? after_a - after_b : 0);
    if (!a || !b) {
        SDL_DestroyTexture(a);
        SDL_DestroyTexture(b);
        return -1;
    }

    REQUIRE(SDL_SetRenderTarget(renderer, a));
    REQUIRE(clear_to(renderer, 8, 8, 22));
    REQUIRE(draw_two_markers(renderer, w, h, SDL_TRUE, SDL_FALSE));
    REQUIRE(finish_selected_target(renderer));
    current = a;
    next = b;

    for (i = 0; i < rounds; ++i) {
        SDL_Texture *swap;

        if (i == 0) {
            uint64_t start = timer_ns_gettime64();

            REQUIRE(SDL_SetRenderTarget(renderer, next));
            REQUIRE(copy_full(renderer, current));
            REQUIRE(draw_two_markers(renderer, w, h, SDL_FALSE, SDL_TRUE));
            REQUIRE(finish_selected_target(renderer));
            timing_add(&stats, timer_ns_gettime64() - start);
        } else {
            REQUIRE(seed_scene(renderer, current, next, &stats));
        }
        swap = current;
        current = next;
        next = swap;
    }
    timing_print(timing_label, &stats);

    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 0, 0, 0));
    if (w == OUTPUT_W && h == OUTPUT_H) {
        REQUIRE(copy_full(renderer, current));
    } else {
        SDL_Rect dst = { 80, 40, 480, 400 };
        REQUIRE(SDL_RenderCopy(renderer, current, NULL, &dst));
    }
    REQUIRE(present_page(renderer, page, page_name,
                         "BOTH red upper-left T1/S1 and green lower-right T2/S2 markers"));
    SDL_DestroyTexture(a);
    SDL_DestroyTexture(b);
    printf("VRAM %dx%d after_destroy=%zu baseline=%zu\n",
           w, h, pvr_mem_available(), before);
    return 0;
}

static int page4_selected_present(SDL_Renderer *renderer)
{
    SDL_Texture *a = create_target(renderer, 160, 120);
    SDL_Texture *b = create_target(renderer, 160, 120);
    TimingStats stats = { 0, 0, 0, 0 };

    printf("PAGE 4: target-selected present followed by explicit preservation seed\n");
    if (!a || !b) { SDL_DestroyTexture(a); SDL_DestroyTexture(b); return -1; }
    REQUIRE(SDL_SetRenderTarget(renderer, a));
    REQUIRE(clear_to(renderer, 8, 8, 22));
    REQUIRE(set_color(renderer, 25, 80, 240));
    REQUIRE(fill(renderer, 8, 8, 64, 48));
    REQUIRE(finish_selected_target(renderer));
    {
        uint64_t start = timer_ns_gettime64();

        REQUIRE(SDL_SetRenderTarget(renderer, b));
        REQUIRE(copy_full(renderer, a));
        REQUIRE(set_color(renderer, 245, 220, 20));
        REQUIRE(fill(renderer, 88, 64, 64, 48));
        REQUIRE(finish_selected_target(renderer));
        timing_add(&stats, timer_ns_gettime64() - start);
    }
    timing_print("160x120 selected-present seed", &stats);
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 0, 0, 0));
    {
        SDL_Rect dst = { 40, 60, 560, 360 };
        REQUIRE(SDL_RenderCopy(renderer, b, NULL, &dst));
    }
    REQUIRE(present_page(renderer, 3, "target-selected present preservation",
                         "BOTH blue upper-left T1 and yellow lower-right T2"));
    SDL_DestroyTexture(a);
    SDL_DestroyTexture(b);
    return 0;
}

static int draw_edge_pattern(SDL_Renderer *renderer, SDL_Texture *texture,
                             int w, int h)
{
    REQUIRE(SDL_SetRenderTarget(renderer, texture));
    REQUIRE(clear_to(renderer, 10, 10, 24));
    REQUIRE(set_color(renderer, 255, 255, 255));
    REQUIRE(fill(renderer, 0, 0, w, 2));
    REQUIRE(set_color(renderer, 255, 0, 255));
    REQUIRE(fill(renderer, 0, h - 2, w, 2));
    REQUIRE(set_color(renderer, 0, 255, 255));
    REQUIRE(fill(renderer, 0, 0, 2, h));
    REQUIRE(set_color(renderer, 255, 255, 0));
    REQUIRE(fill(renderer, w - 2, 0, 2, h));
    REQUIRE(set_color(renderer, 225, 30, 30));
    REQUIRE(fill(renderer, w / 4, h / 4, w / 2, h / 2));
    REQUIRE(finish_selected_target(renderer));
    return 0;
}

static int geometry_copy(SDL_Renderer *renderer, SDL_Texture *texture,
                         const SDL_FRect *dst, int source_w, int source_h,
                         int uv_mode)
{
    SDL_Vertex vertices[4];
    const int indices[6] = { 0, 1, 2, 0, 2, 3 };
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    int i;

    if (uv_mode == 1) {
        u0 = 0.5f / (float)source_w;
        v0 = 0.5f / (float)source_h;
        u1 = ((float)source_w - 0.5f) / (float)source_w;
        v1 = ((float)source_h - 0.5f) / (float)source_h;
    } else if (uv_mode == 2) {
        u1 = ((float)source_w - 1.0f) / (float)source_w;
        v1 = ((float)source_h - 1.0f) / (float)source_h;
    }
    SDL_zeroa(vertices);
    for (i = 0; i < 4; ++i) {
        vertices[i].color.r = 255;
        vertices[i].color.g = 255;
        vertices[i].color.b = 255;
        vertices[i].color.a = 255;
    }
    vertices[0].position.x = dst->x;
    vertices[0].position.y = dst->y;
    vertices[0].tex_coord.x = u0;
    vertices[0].tex_coord.y = v0;
    vertices[1].position.x = dst->x + dst->w;
    vertices[1].position.y = dst->y;
    vertices[1].tex_coord.x = u1;
    vertices[1].tex_coord.y = v0;
    vertices[2].position.x = dst->x + dst->w;
    vertices[2].position.y = dst->y + dst->h;
    vertices[2].tex_coord.x = u1;
    vertices[2].tex_coord.y = v1;
    vertices[3].position.x = dst->x;
    vertices[3].position.y = dst->y + dst->h;
    vertices[3].tex_coord.x = u0;
    vertices[3].tex_coord.y = v1;
    return SDL_RenderGeometry(renderer, texture, vertices, 4, indices, 6);
}

static int edge_page(SDL_Renderer *renderer, int page, int w, int h)
{
    SDL_Texture *texture = create_target(renderer, w, h);
    const SDL_FRect cells[4] = {
        { 20.0f, 20.0f, 290.0f, 210.0f },
        { 330.0f, 20.0f, 290.0f, 210.0f },
        { 20.0f, 250.0f, 290.0f, 210.0f },
        { 330.0f, 250.0f, 290.0f, 210.0f }
    };
    char name[80];

    printf("PAGE %d: edge matrix %dx%d\n", page + 1, w, h);
    printf("  cells TL=current nearest TR=half-texel nearest "
           "BL=current linear BR=half-texel linear\n");
    if (!texture) return -1;
    REQUIRE(draw_edge_pattern(renderer, texture, w, h));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 0, 20, 0));
    REQUIRE(SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest));
    REQUIRE(geometry_copy(renderer, texture, &cells[0], w, h, 0));
    REQUIRE(geometry_copy(renderer, texture, &cells[1], w, h, 1));
    REQUIRE(SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear));
    REQUIRE(geometry_copy(renderer, texture, &cells[2], w, h, 0));
    REQUIRE(geometry_copy(renderer, texture, &cells[3], w, h, 1));
    SDL_snprintf(name, sizeof(name), "edge matrix %dx%d", w, h);
    REQUIRE(present_page(renderer, page, name,
                         "four complete colored borders; compare right-column cells against left-column cells"));
    SDL_DestroyTexture(texture);
    return 0;
}

#if defined(DC_PVR_RTT_MICROPROBE_ONE_TO_ONE_ONLY)
static int edge_one_to_one_page(SDL_Renderer *renderer, int page,
                                SDL_bool half_texel)
{
    static const int widths[4] = { 128, 127, 160, 256 };
    static const int heights[4] = { 128, 93, 120, 128 };
    static const SDL_FRect cells[4] = {
        { 10.0f, 20.0f, 128.0f, 128.0f },
        { 160.0f, 20.0f, 127.0f, 93.0f },
        { 310.0f, 20.0f, 160.0f, 120.0f },
        { 192.0f, 220.0f, 256.0f, 128.0f }
    };
    SDL_Texture *textures[4] = { NULL, NULL, NULL, NULL };
    int i;
    int result = -1;

    printf("PAGE %d: 1:1 edge control (%s UVs)\n", page + 1,
           half_texel ? "half-texel" : "current");
    for (i = 0; i < 4; ++i) {
        textures[i] = create_target(renderer, widths[i], heights[i]);
        if (!textures[i] ||
            draw_edge_pattern(renderer, textures[i], widths[i], heights[i]) < 0) {
            goto done;
        }
    }
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 0, 20, 0));
    for (i = 0; i < 4; ++i) {
        REQUIRE(SDL_SetTextureScaleMode(textures[i], SDL_ScaleModeNearest));
        REQUIRE(geometry_copy(renderer, textures[i], &cells[i],
                              widths[i], heights[i], half_texel ? 1 : 0));
    }
    REQUIRE(present_page(renderer, page,
                         half_texel ? "1:1 half-texel edge control"
                                    : "1:1 current-UV edge control",
                         "four targets at exact size with intact two-pixel borders"));
    result = 0;

done:
    for (i = 0; i < 4; ++i) SDL_DestroyTexture(textures[i]);
    return result;
}
#endif

#if defined(DC_PVR_RTT_MICROPROBE_MAX_INSET_ONLY)
static int edge_max_inset_page(SDL_Renderer *renderer, int page, int w, int h)
{
    SDL_Texture *texture = create_target(renderer, w, h);
    const SDL_FRect cells[4] = {
        { 20.0f, 20.0f, 290.0f, 210.0f },
        { 330.0f, 20.0f, 290.0f, 210.0f },
        { 20.0f, 250.0f, 290.0f, 210.0f },
        { 330.0f, 250.0f, 290.0f, 210.0f }
    };
    char name[96];

    printf("PAGE %d: max-inset matrix %dx%d\n", page + 1, w, h);
    printf("  cells TL=current nearest TR=(logical-1) nearest "
           "BL=current linear BR=(logical-1) linear\n");
    if (!texture) return -1;
    REQUIRE(draw_edge_pattern(renderer, texture, w, h));
    REQUIRE(SDL_SetRenderTarget(renderer, NULL));
    REQUIRE(clear_to(renderer, 0, 20, 0));
    REQUIRE(SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest));
    REQUIRE(geometry_copy(renderer, texture, &cells[0], w, h, 0));
    REQUIRE(geometry_copy(renderer, texture, &cells[1], w, h, 2));
    REQUIRE(SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear));
    REQUIRE(geometry_copy(renderer, texture, &cells[2], w, h, 0));
    REQUIRE(geometry_copy(renderer, texture, &cells[3], w, h, 2));
    SDL_snprintf(name, sizeof(name), "max-inset edge matrix %dx%d", w, h);
    REQUIRE(present_page(renderer, page, name,
                         "right-column borders complete without the left-column periodic edge contamination"));
    SDL_DestroyTexture(texture);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_RendererInfo info;
    int driver;
    int rc = 1;

    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return failed_call("SDL_Init");
    driver = find_pvr_driver();
    if (driver < 0) { failed_call("find Dreamcast PVR driver"); goto done; }
    window = SDL_CreateWindow("PVR RTT preservation microprobe", 0, 0,
                              OUTPUT_W, OUTPUT_H, 0);
    if (!window) { failed_call("SDL_CreateWindow"); goto done; }
    renderer = SDL_CreateRenderer(window, driver,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) { failed_call("SDL_CreateRenderer"); goto done; }
    REQUIRE(SDL_GetRendererInfo(renderer, &info));
    printf("Renderer: %s flags=%08lx initial_pvr_mem=%zu\n",
           info.name, (unsigned long)info.flags, pvr_mem_available());

#if defined(DC_PVR_RTT_MICROPROBE_MAX_INSET_ONLY)
    {
        if (edge_max_inset_page(renderer, 10, 127, 93) < 0 ||
            edge_max_inset_page(renderer, 11, 256, 128) < 0) {
            goto done;
        }
        printf("MICROPROBE MAX-INSET %s: %d runtime/capture failures; visual "
               "verdict requires physical PPM inspection\n",
               failures ? "FAIL" : "PASS", failures);
        rc = failures ? 1 : 0;
        goto done;
    }
#elif defined(DC_PVR_RTT_MICROPROBE_ONE_TO_ONE_ONLY)
    {
        if (edge_one_to_one_page(renderer, 8, SDL_FALSE) < 0 ||
            edge_one_to_one_page(renderer, 9, SDL_TRUE) < 0) {
            goto done;
        }
        printf("MICROPROBE 1:1 %s: %d runtime/capture failures; visual verdict "
               "requires physical PPM inspection\n",
               failures ? "FAIL" : "PASS", failures);
        rc = failures ? 1 : 0;
        goto done;
    }
#endif

    if (preservation_page(renderer, 0, 640, 480, 4,
                          "640x480 logical-screen seed",
                          "offscreen logical-screen preservation") < 0 ||
        preservation_page(renderer, 1, 128, 128, 8,
                          "128x128 target seed",
                          "128x128 target preservation") < 0 ||
        preservation_page(renderer, 2, 512, 512, 4,
                          "512x512 target seed",
                          "512x512 target preservation") < 0 ||
        page4_selected_present(renderer) < 0 ||
        edge_page(renderer, 4, 128, 128) < 0 ||
        edge_page(renderer, 5, 127, 93) < 0 ||
        edge_page(renderer, 6, 160, 120) < 0 ||
        edge_page(renderer, 7, 256, 128) < 0) {
        goto done;
    }

    printf("MICROPROBE %s: %d runtime/capture failures; visual verdict requires "
           "physical PPM inspection\n", failures ? "FAIL" : "PASS", failures);
    rc = failures ? 1 : 0;

done:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
