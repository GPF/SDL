/*
 * Phase 6.2 strided NPOT texture microprobe.
 * Standalone KallistiOS/PVR test: no SDL, OpenGL, or GLdc dependencies.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos.h>
#include <dc/pvr.h>
#include <dc/pvr/pvr_mem.h>
#include <dc/pvr/pvr_txr.h>
#include <dc/video.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define PAGE_COUNT 10
#define PAGE_HOLD_MS 6000
#define CAPTURE_SETTLE_MS 250

typedef struct ProbeTexture {
    uint16_t *cpu;
    pvr_ptr_t vram;
    int logical_w;
    int logical_h;
    int pitch_px;
    int header_w;
    int header_h;
    size_t alloc_size;
    uint32_t format;
    int strided;
    const char *label;
} ProbeTexture;

static int stride_register_failures;

#ifdef HARDWARE_SCREENSHOTS
static const char *const capture_paths[PAGE_COUNT] = {
    "/pc/pvr_strided_texture_microprobe-phase6_2-page1.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page2.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page3.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page4.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page5.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page6.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page7.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page8.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page9.ppm",
    "/pc/pvr_strided_texture_microprobe-phase6_2-page10.ppm"
};
#endif

static int next_pot(int value)
{
    int result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

static uint16_t pattern_pixel(int x, int y, int w, int h, int seed)
{
    static const uint16_t colors[] = {
        0xf800, 0x07e0, 0x001f, 0xffe0, 0xf81f, 0x07ff
    };

    if (x == 0)
        return colors[(seed + 4) % 6];
    if (x == w - 1)
        return 0xffff;
    if (y == 0)
        return colors[seed % 6];
    if (y == h - 1)
        return colors[(seed + 1) % 6];
    if (((x / 32) + (y / 24) + seed) & 1)
        return (uint16_t)(0x2104u | ((seed & 3) << 11));
    return (uint16_t)(0x8410u | ((seed & 3) << 5));
}

static void texture_destroy(ProbeTexture *tex)
{
    if (tex->vram) {
        pvr_mem_free(tex->vram);
        tex->vram = 0;
    }
    free(tex->cpu);
    tex->cpu = NULL;
}

static int texture_create(ProbeTexture *tex, const char *label,
                          int logical_w, int logical_h, int pitch_px,
                          int strided, int seed)
{
    int x, y;

    memset(tex, 0, sizeof(*tex));
    tex->logical_w = logical_w;
    tex->logical_h = logical_h;
    tex->header_w = next_pot(logical_w);
    tex->header_h = next_pot(logical_h);
    tex->pitch_px = strided ? pitch_px : tex->header_w;
    tex->strided = strided;
    tex->label = label;

    if (logical_w <= 0 || logical_h <= 0 || tex->pitch_px < logical_w ||
        (strided && ((tex->pitch_px & 31) != 0 || tex->pitch_px > 992))) {
        printf("  FAIL: invalid texture geometry for %s\n", label);
        return -1;
    }

    tex->alloc_size = (size_t)tex->pitch_px * tex->header_h * sizeof(uint16_t);
    tex->format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED |
                  (strided ? PVR_TXRFMT_X32_STRIDE : PVR_TXRFMT_POW2_STRIDE);
    tex->cpu = (uint16_t *)aligned_alloc(32, tex->alloc_size);
    if (!tex->cpu) {
        printf("  FAIL: CPU allocation for %s (%lu bytes)\n",
               label, (unsigned long)tex->alloc_size);
        return -1;
    }
    memset(tex->cpu, 0, tex->alloc_size);
    for (y = 0; y < logical_h; ++y) {
        for (x = 0; x < logical_w; ++x)
            tex->cpu[y * tex->pitch_px + x] =
                pattern_pixel(x, y, logical_w, logical_h, seed);
    }

    tex->vram = pvr_mem_malloc(tex->alloc_size);
    if (!tex->vram) {
        printf("  FAIL: VRAM allocation for %s (%lu bytes)\n",
               label, (unsigned long)tex->alloc_size);
        texture_destroy(tex);
        return -1;
    }
    pvr_txr_load(tex->cpu, tex->vram, tex->alloc_size);
    printf("  TEX %s type=%s logical=%dx%d pitch=%d header=%dx%d bytes=%lu\n",
           label, strided ? "strided" : "ordinary", logical_w, logical_h,
           tex->pitch_px, tex->header_w, tex->header_h,
           (unsigned long)tex->alloc_size);
    return 0;
}

static void set_stride(int stride, const char *reason)
{
    size_t active;

    pvr_txr_set_stride((size_t)stride);
    active = pvr_txr_get_stride();
    printf("  STRIDE set=%d intended_active=%d actual=%lu "
           "texture_type=strided reason=%s\n",
           stride, stride, (unsigned long)active, reason);
    if (active != (size_t)stride) {
        ++stride_register_failures;
        printf("  FAIL: stride register readback mismatch\n");
    }
}

static void emit_quad_uv(const ProbeTexture *tex, pvr_list_t list,
                         float x, float y,
                         float w, float h, float u0, float v0,
                         float u1, float v1, int filter)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t v[4];
    uint32_t color = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);

    pvr_poly_cxt_txr(&cxt, list, tex->format,
                     tex->header_w, tex->header_h, tex->vram, filter);
    pvr_poly_compile(&hdr, &cxt);
    memset(v, 0, sizeof(v));

    v[0].x = x;     v[0].y = y;     v[0].z = 1.0f;
    v[0].u = u0;    v[0].v = v0;    v[0].argb = color;
    v[0].flags = PVR_CMD_VERTEX;
    v[1].x = x + w; v[1].y = y;     v[1].z = 1.0f;
    v[1].u = u1;    v[1].v = v0;    v[1].argb = color;
    v[1].flags = PVR_CMD_VERTEX;
    v[2].x = x;     v[2].y = y + h; v[2].z = 1.0f;
    v[2].u = u0;    v[2].v = v1;    v[2].argb = color;
    v[2].flags = PVR_CMD_VERTEX;
    v[3].x = x + w; v[3].y = y + h; v[3].z = 1.0f;
    v[3].u = u1;    v[3].v = v1;    v[3].argb = color;
    v[3].flags = PVR_CMD_VERTEX_EOL;

    pvr_prim(&hdr, sizeof(hdr));
    pvr_prim(&v[0], sizeof(v[0]));
    pvr_prim(&v[1], sizeof(v[1]));
    pvr_prim(&v[2], sizeof(v[2]));
    pvr_prim(&v[3], sizeof(v[3]));
}

static void emit_quad(const ProbeTexture *tex, float x, float y,
                      float w, float h, int filter)
{
    emit_quad_uv(tex, PVR_LIST_OP_POLY, x, y, w, h, 0.0f, 0.0f,
                 (float)tex->logical_w / tex->header_w,
                 (float)tex->logical_h / tex->header_h, filter);
}

static void emit_quad_list(const ProbeTexture *tex, pvr_list_t list,
                           float x, float y, float w, float h, int filter)
{
    emit_quad_uv(tex, list, x, y, w, h, 0.0f, 0.0f,
                 (float)tex->logical_w / tex->header_w,
                 (float)tex->logical_h / tex->header_h, filter);
}

static void emit_texel(const ProbeTexture *tex, int sx, int sy,
                       float x, float y, float w, float h)
{
    emit_quad_uv(tex, PVR_LIST_OP_POLY, x, y, w, h,
                 (float)sx / tex->header_w,
                 (float)sy / tex->header_h,
                 (float)(sx + 1) / tex->header_w,
                 (float)(sy + 1) / tex->header_h, PVR_FILTER_NONE);
}

static int scene_begin(void)
{
    pvr_scene_begin();
    if (pvr_list_begin(PVR_LIST_OP_POLY) < 0) {
        printf("  FAIL: pvr_list_begin\n");
        return -1;
    }
    return 0;
}

static int scene_finish(void)
{
    if (pvr_list_finish() < 0) {
        printf("  FAIL: pvr_list_finish\n");
        return -1;
    }
    if (pvr_scene_finish() < 0) {
        printf("  FAIL: pvr_scene_finish\n");
        return -1;
    }
    if (pvr_wait_render_done() < 0) {
        printf("  FAIL: pvr_wait_render_done\n");
        return -1;
    }
    return 0;
}

static int finish_page(int page)
{
#ifdef HARDWARE_SCREENSHOTS
    thd_sleep(CAPTURE_SETTLE_MS);
    if (vid_screen_shot(capture_paths[page - 1]) < 0) {
        printf("  FAIL: capture %s\n", capture_paths[page - 1]);
        return -1;
    }
    printf("  CAPTURE OK: %s\n", capture_paths[page - 1]);
#else
    (void)page;
    thd_sleep(PAGE_HOLD_MS);
#endif
    return 0;
}

static int page1(void)
{
    ProbeTexture t;
    int rc = -1;
    if (texture_create(&t, "p1-640", 640, 480, 640, 1, 1) < 0)
        return -1;
    set_stride(640, "page1 nearest");
    if (scene_begin() == 0) {
        emit_quad(&t, 0, 0, 640, 480, PVR_FILTER_NONE);
        if (scene_finish() == 0 && finish_page(1) == 0)
            rc = 0;
    }
    texture_destroy(&t);
    return rc;
}

static int page2(void)
{
    ProbeTexture t;
    int rc = -1;
    if (texture_create(&t, "p2-640", 640, 480, 640, 1, 2) < 0)
        return -1;
    set_stride(640, "page2 bilinear edge observation");
    if (scene_begin() == 0) {
        emit_quad(&t, 0, 0, 640, 480, PVR_FILTER_BILINEAR);
        if (scene_finish() == 0 && finish_page(2) == 0)
            rc = 0;
    }
    texture_destroy(&t);
    return rc;
}

static int page3(void)
{
    ProbeTexture s, p;
    int rc = -1;
    if (texture_create(&s, "p3-strided", 640, 480, 640, 1, 3) < 0)
        return -1;
    if (texture_create(&p, "p3-pot", 512, 512, 512, 0, 4) < 0) {
        texture_destroy(&s);
        return -1;
    }
    set_stride(640, "page3 strided plus ordinary");
    if (scene_begin() == 0) {
        emit_quad(&s, 8, 32, 304, 416, PVR_FILTER_NONE);
        emit_quad(&p, 328, 32, 304, 416, PVR_FILTER_NONE);
        if (scene_finish() == 0 && finish_page(3) == 0)
            rc = 0;
    }
    texture_destroy(&p);
    texture_destroy(&s);
    return rc;
}

static int page4(void)
{
    ProbeTexture a, b;
    int rc = -1;
    if (texture_create(&a, "p4-a", 512, 256, 512, 1, 4) < 0)
        return -1;
    if (texture_create(&b, "p4-b", 384, 288, 512, 1, 5) < 0) {
        texture_destroy(&a);
        return -1;
    }
    set_stride(512, "page4 shared stride");
    if (scene_begin() == 0) {
        emit_quad(&a, 16, 24, 608, 200, PVR_FILTER_NONE);
        emit_quad(&b, 128, 248, 384, 216, PVR_FILTER_NONE);
        if (scene_finish() == 0 && finish_page(4) == 0)
            rc = 0;
    }
    texture_destroy(&b);
    texture_destroy(&a);
    return rc;
}

static int page5(void)
{
    ProbeTexture a, b, p;
    int rc = -1;
    if (texture_create(&a, "p5-a640", 640, 480, 640, 1, 5) < 0)
        return -1;
    if (texture_create(&b, "p5-b384", 384, 288, 384, 1, 0) < 0) {
        texture_destroy(&a);
        return -1;
    }
    if (texture_create(&p, "p5-pot-control", 256, 256, 256, 0, 2) < 0) {
        texture_destroy(&b);
        texture_destroy(&a);
        return -1;
    }
    if (scene_begin() == 0) {
        set_stride(640, "page5 panel A submission");
        emit_quad(&a, 8, 24, 304, 328, PVR_FILTER_NONE);
        set_stride(384, "page5 panel B submission; final global value");
        emit_quad(&b, 328, 24, 304, 328, PVR_FILTER_NONE);
        emit_quad(&p, 224, 368, 192, 96, PVR_FILTER_NONE);
        if (scene_finish() == 0 && finish_page(5) == 0)
            rc = 0;
    }
    texture_destroy(&p);
    texture_destroy(&b);
    texture_destroy(&a);
    return rc;
}

static int page6(void)
{
    ProbeTexture a, b;
    int rc = -1;
    if (texture_create(&a, "p6-opaque-list-640", 640, 480, 640, 1, 1) < 0)
        return -1;
    if (texture_create(&b, "p6-transparent-list-384", 384, 288, 384, 1, 4) < 0) {
        texture_destroy(&a);
        return -1;
    }
    pvr_scene_begin();
    if (pvr_list_begin(PVR_LIST_OP_POLY) < 0)
        goto done;
    set_stride(640, "page6 opaque list submission");
    emit_quad_list(&a, PVR_LIST_OP_POLY, 8, 32, 304, 416,
                   PVR_FILTER_NONE);
    if (pvr_list_finish() < 0)
        goto done;
    if (pvr_list_begin(PVR_LIST_TR_POLY) < 0)
        goto done;
    set_stride(384, "page6 transparent list submission; final global value");
    emit_quad_list(&b, PVR_LIST_TR_POLY, 328, 32, 304, 416,
                   PVR_FILTER_NONE);
    if (scene_finish() == 0 && finish_page(6) == 0)
        rc = 0;
done:
    texture_destroy(&b);
    texture_destroy(&a);
    return rc;
}

static int page7(void)
{
    ProbeTexture a, b;
    int rc = -1;
    if (texture_create(&a, "p7-frame1-384", 384, 288, 384, 1, 3) < 0)
        return -1;
    if (texture_create(&b, "p7-frame2-640", 640, 480, 640, 1, 5) < 0) {
        texture_destroy(&a);
        return -1;
    }
    set_stride(384, "page7 prior frame");
    if (scene_begin() < 0)
        goto done;
    emit_quad(&a, 64, 48, 512, 384, PVR_FILTER_NONE);
    if (scene_finish() < 0)
        goto done;
    vid_waitvbl();
    set_stride(640, "page7 captured consecutive frame");
    if (scene_begin() < 0)
        goto done;
    emit_quad(&b, 0, 0, 640, 480, PVR_FILTER_NONE);
    if (scene_finish() == 0 && finish_page(7) == 0)
        rc = 0;
done:
    texture_destroy(&b);
    texture_destroy(&a);
    return rc;
}

static int page8(void)
{
    ProbeTexture t;
    uint16_t *update = NULL;
    const int update_w = 64, update_h = 64, source_pitch = 128;
    int x, y, rc = -1;

    if (texture_create(&t, "p8-update", 640, 480, 640, 1, 2) < 0)
        return -1;
    update = (uint16_t *)aligned_alloc(32,
               (size_t)source_pitch * update_h * sizeof(uint16_t));
    if (!update) {
        printf("  FAIL: page8 padded source allocation\n");
        texture_destroy(&t);
        return -1;
    }
    memset(update, 0x33, (size_t)source_pitch * update_h * sizeof(uint16_t));
    for (y = 0; y < update_h; ++y) {
        for (x = 0; x < update_w; ++x)
            update[y * source_pitch + x] = 0xffe0;
        memcpy(&t.cpu[(100 + y) * t.pitch_px + 100],
               &update[y * source_pitch], update_w * sizeof(uint16_t));
    }
    printf("  UPDATE dst=100,100 size=64x64 source_pitch=256 bytes\n");
    pvr_txr_load(t.cpu, t.vram, t.alloc_size);
    set_stride(640, "page8 post-update render");
    if (scene_begin() == 0) {
        emit_quad(&t, 0, 0, 640, 480, PVR_FILTER_NONE);
        if (scene_finish() == 0 && finish_page(8) == 0)
            rc = 0;
    }
    free(update);
    texture_destroy(&t);
    return rc;
}

static int page9(void)
{
    ProbeTexture t;
    int rc = -1;
    if (texture_create(&t, "p9-edges", 640, 480, 640, 1, 4) < 0)
        return -1;
    set_stride(640, "page9 logical edges and texels");
    if (scene_begin() == 0) {
        emit_quad(&t, 8, 24, 464, 432, PVR_FILTER_NONE);
        emit_texel(&t, 0, 0, 496, 32, 56, 56);
        emit_texel(&t, 639, 0, 568, 32, 56, 56);
        emit_texel(&t, 0, 479, 496, 104, 56, 56);
        emit_texel(&t, 639, 479, 568, 104, 56, 56);
        emit_quad_uv(&t, PVR_LIST_OP_POLY, 496, 192, 128, 112,
                     639.0f / t.header_w, 0.0f,
                     640.0f / t.header_w, 480.0f / t.header_h,
                     PVR_FILTER_NONE);
        emit_quad_uv(&t, PVR_LIST_OP_POLY, 496, 320, 128, 112,
                     0.0f, 479.0f / t.header_h,
                     640.0f / t.header_w, 480.0f / t.header_h,
                     PVR_FILTER_NONE);
        if (scene_finish() == 0 && finish_page(9) == 0)
            rc = 0;
    }
    texture_destroy(&t);
    return rc;
}

static int page10(void)
{
    ProbeTexture p, s;
    int rc = -1;
    if (texture_create(&p, "p10-pot", 512, 512, 512, 0, 0) < 0)
        return -1;
    if (texture_create(&s, "p10-strided320", 320, 240, 320, 1, 5) < 0) {
        texture_destroy(&p);
        return -1;
    }
    set_stride(320, "page10 recovery reference");
    if (scene_begin() == 0) {
        emit_quad(&p, 8, 24, 304, 432, PVR_FILTER_NONE);
        emit_quad(&s, 328, 120, 304, 240, PVR_FILTER_NONE);
        if (scene_finish() == 0 && finish_page(10) == 0)
            rc = 0;
    }
    texture_destroy(&s);
    texture_destroy(&p);
    return rc;
}

int main(int argc, char **argv)
{
    static int (*const pages[PAGE_COUNT])(void) = {
        page1, page2, page3, page4, page5,
        page6, page7, page8, page9, page10
    };
    size_t initial_vram, final_vram;
    pvr_ptr_t allocator_probe;
    int failures = 0;
    int page;

    (void)argc;
    (void)argv;
    printf("=== Phase 6.2 Strided NPOT Texture Microprobe ===\n");
    vid_set_mode(DM_640x480, PM_RGB565);
    printf("Video mode: 640x480 RGB565\n");
    if (pvr_init_defaults() < 0) {
        printf("FAIL: pvr_init_defaults()\n");
        return 1;
    }
    printf("OK: pvr_init_defaults() succeeded\n");

    /* Exclude dlmalloc's one-time arena metadata from the leak baseline. */
    allocator_probe = pvr_mem_malloc(32);
    if (!allocator_probe) {
        printf("FAIL: PVR allocator warmup\n");
        pvr_shutdown();
        return 1;
    }
    pvr_mem_free(allocator_probe);
    initial_vram = pvr_mem_available();
    printf("VRAM available (after init): %lu bytes\n",
           (unsigned long)initial_vram);
    printf("Initial stride readback: %lu pixels\n",
           (unsigned long)pvr_txr_get_stride());

    for (page = 0; page < PAGE_COUNT; ++page) {
        printf("\n--- Page %d/%d ---\n", page + 1, PAGE_COUNT);
        if (pages[page]() < 0) {
            ++failures;
            printf("Page %d: FAIL\n", page + 1);
        } else {
            printf("Page %d: EXECUTION OK (hardware image is verdict)\n",
                   page + 1);
        }
    }

    final_vram = pvr_mem_available();
    if (stride_register_failures) {
        failures += stride_register_failures;
        printf("FAIL: %d stride register readback mismatch(es)\n",
               stride_register_failures);
    }
    printf("\n=== VRAM Recovery ===\n");
    printf("Initial VRAM: %lu\n", (unsigned long)initial_vram);
    printf("Final VRAM:   %lu\n", (unsigned long)final_vram);
    if (final_vram != initial_vram) {
        ++failures;
        printf("FAIL: VRAM delta=%ld bytes\n",
               (long)final_vram - (long)initial_vram);
    } else {
        printf("VRAM delta: 0 bytes\n");
    }

    if (pvr_shutdown() < 0) {
        ++failures;
        printf("FAIL: pvr_shutdown()\n");
    } else {
        printf("OK: pvr_shutdown() completed\n");
    }
    if (failures)
        printf("FAIL (%d check(s) failed)\n", failures);
    else
        printf("EXECUTION PASS; PHYSICAL CAPTURES REQUIRE INSPECTION\n");
    return failures ? 1 : 0;
}
