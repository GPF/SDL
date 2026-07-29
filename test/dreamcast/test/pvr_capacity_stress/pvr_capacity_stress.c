/*
 * Native Dreamcast PVR renderer: TA/OPB command-capacity stress test.
 *
 * Workloads A-E execute safe below-boundary loads that stay under the
 * 8192 32-byte-unit scene budget. Workload F is a compile-time opt-in
 * boundary probe that is excluded from ordinary builds.
 *
 * See src/render/dreamcast/PVR_CAPACITY_RESEARCH.md for the design
 * and per-workload formulas.
 */

#include <SDL.h>
#include <kos.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define OUTPUT_W 640
#define OUTPUT_H 480
#define BUDGET_UNITS 8192
#define STAGE_HOLD_MS 2000
#define RECOVERY_HOLD_MS 1000
#define FINAL_HOLD_MS 10000
#ifdef DC_PVR_CAPACITY_HARDWARE_SCREENSHOTS
#define HARDWARE_CAPTURE_PATH(name) \
    "/pc/test/dreamcast/test/pvr_capacity_stress/" name
#endif

static int fail_message(const char *operation)
{
    printf("FAIL: %s: %s\n", operation, SDL_GetError());
    return -1;
}

#define REQUIRE(call) do { if ((call) < 0) return fail_message(#call); } while (0)

static int g_workload_id = 0;

static void begin_workload(const char *name)
{
    ++g_workload_id;
    printf("\n=== Workload %d: %s ===\n", g_workload_id, name);
}

static void print_stage(const char *stage_name, int expected_submitted,
                        int expected_headers, int expected_vertices,
                        int expected_total, double percentage)
{
    printf("  %s completed: expected_submitted=%d expected_hdr=%d expected_vert=%d "
           "expected_total=%d percentage=%.1f%%\n",
           stage_name, expected_submitted, expected_headers, expected_vertices,
           expected_total, percentage);
}

static int present_checked(SDL_Renderer *renderer)
{
    SDL_ClearError();
    SDL_RenderPresent(renderer);
    if (*SDL_GetError()) {
        return fail_message("SDL_RenderPresent");
    }
    return 0;
}

static int clear_checked(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderClear(renderer));
    return 0;
}

static int draw_quad_filled(SDL_Renderer *renderer, int x, int y, int w, int h,
                            Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Rect rect = { x, y, w, h };
    REQUIRE(SDL_SetRenderDrawColor(renderer, r, g, b, 255));
    REQUIRE(SDL_RenderFillRect(renderer, &rect));
    return 0;
}

static SDL_Color make_color(Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Color c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = 255;
    return c;
}

static int draw_quad_textured(SDL_Renderer *renderer, SDL_Texture *texture,
                              int dst_x, int dst_y, int dst_w, int dst_h,
                              SDL_BlendMode blend)
{
    SDL_Rect dst = { dst_x, dst_y, dst_w, dst_h };
    SDL_BlendMode original_blend = SDL_BLENDMODE_NONE;
    int draw_result;

    if (SDL_GetTextureBlendMode(texture, &original_blend) < 0) {
        return fail_message("SDL_GetTextureBlendMode");
    }
    if (blend != original_blend) {
        REQUIRE(SDL_SetTextureBlendMode(texture, blend));
    }
    draw_result = SDL_RenderCopy(renderer, texture, NULL, &dst);
    if (blend != original_blend) {
        const int restore_result = SDL_SetTextureBlendMode(texture, original_blend);

        if (draw_result == 0 && restore_result < 0) {
            return fail_message("SDL_SetTextureBlendMode(restore)");
        }
    }
    if (draw_result < 0) {
        return fail_message("SDL_RenderCopy");
    }
    return 0;
}

static int recovery_frame(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    if (clear_checked(renderer, 0, 0, 0) < 0 ||
        draw_quad_filled(renderer, 200, 200, 240, 80, r, g, b) < 0 ||
        present_checked(renderer) < 0) {
        return -1;
    }
    SDL_Delay(RECOVERY_HOLD_MS);
    printf("    Known-good recovery frame presented.\n");
    return 0;
}

#ifdef DC_PVR_CAPACITY_HARDWARE_SCREENSHOTS
static int capture_hardware_frame(const char *path)
{
    /* RenderPresent waits for rendering, but the visible framebuffer advances
     * from the vblank handler. Let the three-buffer PVR presentation reach the
     * displayed page before KOS reads it. */
    vid_waitvbl();
    vid_waitvbl();
    vid_waitvbl();
    if (vid_screen_shot(path) < 0) {
        printf("  FAIL: hardware screenshot failed: %s\n", path);
        return -1;
    }
    printf("    Hardware screenshot saved: %s\n", path);
    return 0;
}
#define CAPTURE_HARDWARE_FRAME(path) capture_hardware_frame(path)
#else
#define CAPTURE_HARDWARE_FRAME(path) 0
#endif

/* ----------------------------------------------------------------
 * Workload A: stable untextured triangles
 *
 * Formula: 1 + 3N
 * Ramp: 256, 512, 1024, 2048, 2300
 * Final: 1 + 3 * 2300 = 6901 units (84.2%)
 *
 * One untextured NONE header is compiled for the whole workload.
 * All triangles share that cached header. The first two are the dark
 * background and the remainder are light-green probes. SDL geometry
 * submission emits 3 vertex commands per triangle.
 * ---------------------------------------------------------------- */

static int workload_a(SDL_Renderer *renderer)
{
    static const int ramp[] = { 256, 512, 1024, 2048, 2300 };
    static const int ramp_count = 5;
    SDL_Vertex *geom = NULL;
    int *indices = NULL;
    int stage;

    begin_workload("Stable untextured triangles");

    for (stage = 0; stage < ramp_count; ++stage) {
        const int n = ramp[stage];
        const int verts_total = n * 3;
        const int indices_total = n * 3;
        const int header_units = 1;
        const int vertex_units = 3 * n;
        const int total_units = header_units + vertex_units;
        const double percentage = (double)total_units / (double)BUDGET_UNITS * 100.0;
        const int submitted = verts_total + header_units;
        int i;

        printf("  Ramp %d/%d: N=%d triangles\n", stage + 1, ramp_count, n);

        if ((size_t)verts_total > SDL_SIZE_MAX / sizeof(SDL_Vertex) ||
            (size_t)indices_total > SDL_SIZE_MAX / sizeof(int)) {
            printf("  FAIL: integer overflow in allocation size\n");
            goto fail_cleanup;
        }

        geom = (SDL_Vertex *)SDL_malloc((size_t)verts_total * sizeof(SDL_Vertex));
        if (!geom) {
            return fail_message("geom malloc");
        }
        indices = (int *)SDL_malloc((size_t)indices_total * sizeof(int));
        if (!indices) {
            SDL_free(geom);
            return fail_message("indices malloc");
        }

        for (i = 0; i < n; ++i) {
            const int tx = (i * 37 % 600) + 20;
            const int ty = (i * 53 % 420) + 20;
            const SDL_Color c = (i < 2) ? make_color(5, 5, 25) :
                                          make_color(200, 220, 180);

            if (i == 0) {
                geom[0].position.x = 0.0f;
                geom[0].position.y = 0.0f;
                geom[1].position.x = 639.0f;
                geom[1].position.y = 0.0f;
                geom[2].position.x = 639.0f;
                geom[2].position.y = 479.0f;
            } else if (i == 1) {
                geom[3].position.x = 0.0f;
                geom[3].position.y = 0.0f;
                geom[4].position.x = 639.0f;
                geom[4].position.y = 479.0f;
                geom[5].position.x = 0.0f;
                geom[5].position.y = 479.0f;
            } else {
                geom[i * 3 + 0].position.x = (float)tx;
                geom[i * 3 + 0].position.y = (float)ty;
                geom[i * 3 + 1].position.x = (float)(tx + 16);
                geom[i * 3 + 1].position.y = (float)(ty + 16);
                geom[i * 3 + 2].position.x = (float)(tx + 4);
                geom[i * 3 + 2].position.y = (float)(ty + 28);
            }

            geom[i * 3 + 0].color = c;
            geom[i * 3 + 0].tex_coord.x = 0.0f;
            geom[i * 3 + 0].tex_coord.y = 0.0f;
            geom[i * 3 + 1].color = c;
            geom[i * 3 + 1].tex_coord.x = 0.0f;
            geom[i * 3 + 1].tex_coord.y = 0.0f;
            geom[i * 3 + 2].color = c;
            geom[i * 3 + 2].tex_coord.x = 0.0f;
            geom[i * 3 + 2].tex_coord.y = 0.0f;
            indices[i * 3 + 0] = i * 3 + 0;
            indices[i * 3 + 1] = i * 3 + 1;
            indices[i * 3 + 2] = i * 3 + 2;
        }

        if (SDL_RenderGeometry(renderer, NULL, geom, verts_total,
                               indices, indices_total) < 0) {
            goto fail_cleanup;
        }
        if (present_checked(renderer) < 0) {
            goto fail_cleanup;
        }
        if (stage == ramp_count - 1 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-a-final.ppm")) < 0) {
            goto fail_cleanup;
        }

        SDL_free(geom);
        SDL_free(indices);
        geom = NULL;
        indices = NULL;

        print_stage("triangle stage", submitted, header_units, vertex_units,
                    total_units, percentage);
        SDL_Delay(STAGE_HOLD_MS);
        if (recovery_frame(renderer, 0, 255, 0) < 0) {
            goto fail_cleanup;
        }
        if (stage == ramp_count - 1 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-a-recovery.ppm")) < 0) {
            goto fail_cleanup;
        }
    }

    printf("  Workload A complete.\n");
    return 0;

fail_cleanup:
    if (geom) {
        SDL_free(geom);
    }
    if (indices) {
        SDL_free(indices);
    }
    return -1;
}

/* ----------------------------------------------------------------
 * Workload B: deliberately header-heavy textured quads
 *
 * Formula: 7N (1 header + 6 vertices per quad, no shared header)
 * Ramp: 256, 512, 768, 900, 990
 * Final: 7 * 990 = 6930 units (84.6%)
 *
 * Header-churn strategy:
 *   The renderer's polygon-header cache is keyed on (texture pointer,
 *   blend mode, texture_env, filter). By alternating SDL_BLENDMODE_NONE
 *   and SDL_BLENDMODE_BLEND on every consecutive quad, the cached
 *   header is replaced by the opposite blend on every draw. Each quad
 *   therefore forces a new header compilation and submission, giving
 *   exactly one header per quad and 7N total units. The renderer
 *   natively supports both blend modes; the same single texture is
 *   reused; no large texture pool is required.
 * ---------------------------------------------------------------- */

static int workload_b(SDL_Renderer *renderer, SDL_Texture *tex)
{
    static const int ramp[] = { 256, 512, 768, 900, 990 };
    static const int ramp_count = 5;
    int stage;

    begin_workload("Header-heavy textured quads");

    for (stage = 0; stage < ramp_count; ++stage) {
        const int n = ramp[stage];
        const int vertices_per_quad = 6;
        const int header_units = n;
        const int vertex_units = vertices_per_quad * n;
        const int total_units = header_units + vertex_units;
        const double percentage = (double)total_units / (double)BUDGET_UNITS * 100.0;
        int i;

        printf("  Ramp %d/%d: N=%d quads\n", stage + 1, ramp_count, n);

        for (i = 0; i < n; ++i) {
            const int x = (i == 0) ? 0 : 10 + (i % 32) * 19;
            const int y = (i == 0) ? 0 : 10 + (i / 32) * 14;
            const int w = (i == 0) ? OUTPUT_W - 1 : 18;
            const int h = (i == 0) ? OUTPUT_H - 1 : 13;
            const SDL_BlendMode blend = (i & 1) ? SDL_BLENDMODE_NONE :
                                                   SDL_BLENDMODE_BLEND;

            if (blend == SDL_BLENDMODE_NONE) {
                if (draw_quad_textured(renderer, tex, x, y, w, h,
                                       SDL_BLENDMODE_NONE) < 0) {
                    goto fail_cleanup;
                }
            } else {
                if (draw_quad_textured(renderer, tex, x, y, w, h,
                                       SDL_BLENDMODE_BLEND) < 0) {
                    goto fail_cleanup;
                }
            }
        }
        if (present_checked(renderer) < 0) {
            goto fail_cleanup;
        }
        if (stage == ramp_count - 1 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-b-final.ppm")) < 0) {
            goto fail_cleanup;
        }

        print_stage("quad stage", header_units + vertex_units, header_units,
                    vertex_units, total_units, percentage);
        SDL_Delay(STAGE_HOLD_MS);
        if (recovery_frame(renderer, 255, 200, 0) < 0) {
            goto fail_cleanup;
        }
        if (stage == ramp_count - 1 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-b-recovery.ppm")) < 0) {
            goto fail_cleanup;
        }
    }

    printf("  Workload B complete.\n");
    return 0;

fail_cleanup:
    return -1;
}

/* ----------------------------------------------------------------
 * Workload C: same-texture copies
 *
 * Formula: 1 + 6N (1 cached header + 6 vertices per copy)
 * Ramp: 256, 512, 1024, 1150
 * Final: 1 + 6 * 1150 = 6901 units (84.2%)
 *
 * One texture, one blend mode, one filter. The header is compiled
 * on the first copy and reused for every subsequent copy.
 * ---------------------------------------------------------------- */

static int workload_c(SDL_Renderer *renderer, SDL_Texture *tex)
{
    static const int ramp[] = { 256, 512, 1024, 1150 };
    static const int ramp_count = 4;
    int stage;

    begin_workload("Same-texture copies");

    for (stage = 0; stage < ramp_count; ++stage) {
        const int n = ramp[stage];
        const int header_units = 1;
        const int vertex_units = 6 * n;
        const int total_units = header_units + vertex_units;
        const double percentage = (double)total_units / (double)BUDGET_UNITS * 100.0;
        int i;

        printf("  Ramp %d/%d: N=%d copies\n", stage + 1, ramp_count, n);

        for (i = 0; i < n; ++i) {
            const int x = (i == 0) ? 0 : 10 + (i % 40) * 15;
            const int y = (i == 0) ? 0 : 10 + (i / 40) * 11;
            const int w = (i == 0) ? OUTPUT_W - 1 : 14;
            const int h = (i == 0) ? OUTPUT_H - 1 : 10;
            if (draw_quad_textured(renderer, tex, x, y, w, h,
                                   SDL_BLENDMODE_NONE) < 0) {
                goto fail_cleanup;
            }
        }
        if (present_checked(renderer) < 0) {
            goto fail_cleanup;
        }
        if (stage == ramp_count - 1 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-c-final.ppm")) < 0) {
            goto fail_cleanup;
        }

        print_stage("copy stage", header_units + vertex_units, header_units,
                    vertex_units, total_units, percentage);
        SDL_Delay(STAGE_HOLD_MS);
        if (recovery_frame(renderer, 0, 180, 255) < 0) {
            goto fail_cleanup;
        }
        if (stage == ramp_count - 1 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-c-recovery.ppm")) < 0) {
            goto fail_cleanup;
        }
    }

    printf("  Workload C complete.\n");
    return 0;

fail_cleanup:
    return -1;
}

/* ----------------------------------------------------------------
 * Workload D: stable-state points
 *
 * Formula: 1 + 6N (1 cached header + 6 vertices per point)
 * Ramp: 256, 512, 1024, 1150
 * Final: 1 + 6 * 1150 = 6901 units (84.2%)
 *
 * One untextured NONE header, all points share it. The backend
 * expands each point into two triangles (6 vertex commands).
 * ---------------------------------------------------------------- */

static int workload_d(SDL_Renderer *renderer)
{
    static const int ramp[] = { 256, 512, 1024, 1150 };
    static const int ramp_count = 4;
    SDL_Point *points = NULL;
    int stage;

    begin_workload("Stable-state points");

    for (stage = 0; stage < ramp_count; ++stage) {
        const int n = ramp[stage];
        const int header_units = 1;
        const int vertex_units = 6 * n;
        const int total_units = header_units + vertex_units;
        const double percentage = (double)total_units / (double)BUDGET_UNITS * 100.0;
        int i;

        printf("  Ramp %d/%d: N=%d points\n", stage + 1, ramp_count, n);

        if ((size_t)n > SDL_SIZE_MAX / sizeof(SDL_Point)) {
            printf("  FAIL: integer overflow in point buffer size\n");
            goto fail_cleanup;
        }

        points = (SDL_Point *)SDL_malloc((size_t)n * sizeof(SDL_Point));
        if (!points) {
            return fail_message("points malloc");
        }

        for (i = 0; i < n; ++i) {
            points[i].x = (i * 37 % 600) + 20;
            points[i].y = (i * 53 % 420) + 20;
        }
        if (SDL_SetRenderDrawColor(renderer, 180, 220, 120, 255) < 0) {
            goto fail_cleanup;
        }
        if (SDL_RenderDrawPoints(renderer, points, n) < 0) {
            goto fail_cleanup;
        }
        if (present_checked(renderer) < 0) {
            goto fail_cleanup;
        }
        if (stage == ramp_count - 1 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-d-final.ppm")) < 0) {
            goto fail_cleanup;
        }

        SDL_free(points);
        points = NULL;

        print_stage("point stage", header_units + vertex_units, header_units,
                    vertex_units, total_units, percentage);
        SDL_Delay(STAGE_HOLD_MS);
        if (recovery_frame(renderer, 120, 255, 0) < 0) {
            goto fail_cleanup;
        }
        if (stage == ramp_count - 1 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-d-recovery.ppm")) < 0) {
            goto fail_cleanup;
        }
    }

    printf("  Workload D complete.\n");
    return 0;

fail_cleanup:
    if (points) {
        SDL_free(points);
    }
    return -1;
}

/* ----------------------------------------------------------------
 * Workload E: mixed realistic frame
 *
 * Composition and order:
 *   1. Clear (untextured NONE): 1 header + 4 vertices = 5
 *   2. One background textured quad (NONE): 1 header + 6 vertices = 7
 *   3. S sprites (same texture/state as background): 6S vertices
 *   4. O translucent overlay quads (unique texture each):
 *         O headers + 6O vertices
 *   5. 25 UI rectangles (untextured NONE): 1 shared header + 150 vertices
 *   6. 600 untextured points (cached from UI): 3600 vertices
 *
 * Total: (O+3) headers + (3760+6S+6O) vertices = 3763+6S+7O units
 *
 * Five stages with increasing sprite/overlay counts:
 *   Stage 1: 100 sprites, 15 overlays     -> 4468 units (54.5%)
 *   Stage 2: 200 sprites, 30 overlays     -> 5173 units (63.1%)
 *   Stage 3: 300 sprites, 45 overlays     -> 5878 units (71.8%)
 *   Stage 4: 350 sprites, 60 overlays     -> 6283 units (76.7%)
 *   Stage 5: 400 sprites, 75 overlays     -> 6688 units (81.6%)
 * ---------------------------------------------------------------- */

#define UI_RECT_COUNT 25
#define UI_POINT_COUNT 600

static SDL_Texture *create_overlay_texture(SDL_Renderer *renderer, int seed)
{
    SDL_Texture *texture;
    Uint16 pixels[8 * 8];
    int px;
    int py;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555,
                                SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!texture) {
        return NULL;
    }
    for (py = 0; py < 8; ++py) {
        for (px = 0; px < 8; ++px) {
            pixels[py * 8 + px] = (Uint16)(0x8000u |
                ((Uint16)(seed & 0x1f) << 10) |
                ((Uint16)((seed * 3 + px) & 0x1f) << 5) |
                (Uint16)((seed * 7 + py) & 0x1f));
        }
    }
    if (SDL_UpdateTexture(texture, NULL, pixels,
                          8 * (int)sizeof(Uint16)) < 0 ||
        SDL_SetTextureAlphaMod(texture, 128) < 0) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    return texture;
}

static int compose_stage(SDL_Renderer *renderer, SDL_Texture *bg_tex,
                         int sprite_count, int overlay_count,
                         const char *stage_name)
{
    SDL_Texture **overlays = NULL;
    SDL_Point *ui_pts = NULL;
    int result = -1;
    int i;
    const int header_units = overlay_count + 3;
    const int vertex_units = 3760 + 6 * sprite_count + 6 * overlay_count;
    const int total_units = header_units + vertex_units;
    const double percentage = (double)total_units / (double)BUDGET_UNITS * 100.0;

    printf("  %s: sprites=%d overlays=%d\n", stage_name, sprite_count,
           overlay_count);

    if (overlay_count < 0 ||
        (size_t)overlay_count > SDL_SIZE_MAX / sizeof(*overlays)) {
        printf("  FAIL: invalid overlay texture count\n");
        goto cleanup;
    }
    overlays = (SDL_Texture **)SDL_calloc((size_t)overlay_count,
                                          sizeof(*overlays));
    if (!overlays) {
        fail_message("overlay texture array");
        goto cleanup;
    }
    for (i = 0; i < overlay_count; ++i) {
        overlays[i] = create_overlay_texture(renderer, i);
        if (!overlays[i]) {
            fail_message("overlay texture");
            goto cleanup;
        }
    }

    if (clear_checked(renderer, 8, 12, 40) < 0) {
        goto cleanup;
    }

    /* 1. Background textured quad (NONE). */
    if (draw_quad_textured(renderer, bg_tex, 20, 20, 260, 220,
                           SDL_BLENDMODE_NONE) < 0) {
        goto cleanup;
    }

    /* 2. Sprites using the same texture and state as the background. */
    for (i = 0; i < sprite_count; ++i) {
        const int x = 300 + (i % 20) * 16;
        const int y = 30 + (i / 20) * 12;
        if (draw_quad_textured(renderer, bg_tex, x, y, 14, 10,
                               SDL_BLENDMODE_NONE) < 0) {
            goto cleanup;
        }
    }

    /* 3. Translucent overlays, each forcing a header change. */
    for (i = 0; i < overlay_count; ++i) {
        const int x = 30 + (i % 15) * 17;
        const int y = 260 + (i / 15) * 12;

        if (draw_quad_textured(renderer, overlays[i], x, y, 15, 10,
                               SDL_BLENDMODE_BLEND) < 0) {
            goto cleanup;
        }
    }

    /* 4. UI rectangles (untextured NONE), one shared header. */
    {
        int ux, uy, ur;

        if (SDL_SetRenderDrawColor(renderer, 255, 255, 100, 255) < 0) {
            goto cleanup;
        }
        ux = 20;
        uy = 400;
        for (ur = 0; ur < UI_RECT_COUNT; ++ur) {
            SDL_Rect r = { ux, uy, 24, 16 };
            if (SDL_RenderFillRect(renderer, &r) < 0) {
                fail_message("UI rect");
                goto cleanup;
            }
            ux += 26;
            if (ux > 580) {
                ux = 20;
                uy += 18;
            }
        }
    }

    /* 5. Untextured points, cached from the UI header. */
    {
        int pi;

        if ((size_t)UI_POINT_COUNT > SDL_SIZE_MAX / sizeof(SDL_Point)) {
            printf("  FAIL: integer overflow in UI point buffer size\n");
            goto cleanup;
        }
        ui_pts = (SDL_Point *)SDL_malloc((size_t)UI_POINT_COUNT *
                                          sizeof(SDL_Point));
        if (!ui_pts) {
            fail_message("UI points malloc");
            goto cleanup;
        }
        if (SDL_SetRenderDrawColor(renderer, 200, 255, 200, 255) < 0) {
            fail_message("SDL_SetRenderDrawColor(UI points)");
            goto cleanup;
        }
        for (pi = 0; pi < UI_POINT_COUNT; ++pi) {
            ui_pts[pi].x = 20 + (pi % 40) * 15;
            ui_pts[pi].y = 380 + (pi / 40) * 5;
        }
        if (SDL_RenderDrawPoints(renderer, ui_pts, UI_POINT_COUNT) < 0) {
            fail_message("UI points draw");
            goto cleanup;
        }
        SDL_free(ui_pts);
        ui_pts = NULL;
    }

    if (present_checked(renderer) < 0) {
        goto cleanup;
    }

    print_stage(stage_name, header_units + vertex_units, header_units,
                vertex_units, total_units, percentage);
    result = 0;

cleanup:
    SDL_free(ui_pts);
    if (overlays) {
        for (i = 0; i < overlay_count; ++i) {
            if (overlays[i]) {
                SDL_DestroyTexture(overlays[i]);
            }
        }
        SDL_free(overlays);
    }
    return result;
}

static int workload_e(SDL_Renderer *renderer, SDL_Texture *bg_tex)
{
    static const int sprite_counts[] = { 100, 200, 300, 350, 400 };
    static const int overlay_counts[] = { 15, 30, 45, 60, 75 };
    static const char *stage_names[] = {
        "Stage 1", "Stage 2", "Stage 3", "Stage 4", "Stage 5 (final)"
    };
#ifdef DC_PVR_CAPACITY_HARDWARE_SCREENSHOTS
    static const char *hardware_paths[] = {
        HARDWARE_CAPTURE_PATH("pvr_capacity_stress-hardware-e-stage1.ppm"),
        HARDWARE_CAPTURE_PATH("pvr_capacity_stress-hardware-e-stage2.ppm"),
        HARDWARE_CAPTURE_PATH("pvr_capacity_stress-hardware-e-stage3.ppm"),
        HARDWARE_CAPTURE_PATH("pvr_capacity_stress-hardware-e-stage4.ppm"),
        HARDWARE_CAPTURE_PATH("pvr_capacity_stress-hardware-e-stage5.ppm")
    };
#endif
    int stage;

    begin_workload("Mixed realistic frame");

    for (stage = 0; stage < 5; ++stage) {
        if (compose_stage(renderer, bg_tex, sprite_counts[stage],
                          overlay_counts[stage], stage_names[stage]) < 0) {
            return -1;
        }
#ifdef DC_PVR_CAPACITY_HARDWARE_SCREENSHOTS
        if (CAPTURE_HARDWARE_FRAME(hardware_paths[stage]) < 0) {
            return -1;
        }
#endif
        SDL_Delay(STAGE_HOLD_MS);
        if (recovery_frame(renderer, 255, 100, 200) < 0) {
            return -1;
        }
        if (stage == 4 &&
            CAPTURE_HARDWARE_FRAME(HARDWARE_CAPTURE_PATH(
                "pvr_capacity_stress-hardware-e-recovery.ppm")) < 0) {
            return -1;
        }
    }
    printf("  Workload E complete.\n");
    return 0;
}

/* ----------------------------------------------------------------
 * Workload F: boundary probe (compile-time opt-in only)
 *
 * NOT compiled into ordinary builds. Requires
 * DC_PVR_CAPACITY_BOUNDARY_PROBE. Default maximum is 2800
 * triangles. Stop on the first observable anomaly.
 * ---------------------------------------------------------------- */

#ifdef DC_PVR_CAPACITY_BOUNDARY_PROBE

#ifndef DC_PVR_CAPACITY_BOUNDARY_MAX
#define DC_PVR_CAPACITY_BOUNDARY_MAX 2800
#endif

#if DC_PVR_CAPACITY_BOUNDARY_MAX < 0 || DC_PVR_CAPACITY_BOUNDARY_MAX > 2800
#error DC_PVR_CAPACITY_BOUNDARY_MAX must be between 0 and 2800
#endif

static int workload_f(SDL_Renderer *renderer)
{
    static const int ramp[] = { 2680, 2700, 2720, 2730, 2740,
                                2750, 2775, 2800 };
    static const int ramp_count = 8;
    SDL_Vertex *geom = NULL;
    int *indices = NULL;
    int stage;

    printf("\n");
    printf("##############################################\n");
    printf("#                                            #\n");
    printf("#  DANGER: Workload F is a boundary probe.   #\n");
    printf("#  It pushes the TA/OPB load past the        #\n");
    printf("#  documented safe limit. Console corruption   #\n");
    printf("#  or failure to complete the scene is        #\n");
    printf("#  possible. Recovery, if any, may require    #\n");
    printf("#  pvr_shutdown() plus pvr_init(), but success #\n");
    printf("#  is not guaranteed.                        #\n");
    printf("#                                            #\n");
    printf("#  Do not rely on pvr_wait_ready() or        #\n");
    printf("#  pvr_wait_render_done() return values as   #\n");
    printf("#  overflow indicators. Timeouts are         #\n");
    printf("#  ambiguous and not reliable overflow        #\n");
    printf("#  detectors.                                #\n");
    printf("#                                            #\n");
    printf("#  If the console stops responding or the    #\n");
    printf("#  output is clearly wrong, a console reset  #\n");
    printf("#  may be required. No recovery is promised. #\n");
    printf("#                                            #\n");
    printf("##############################################\n");
    printf("\n");
    printf("  Workload F: boundary probe (DC_PVR_CAPACITY_BOUNDARY_PROBE)\n");
    printf("  Max triangles: %d\n", DC_PVR_CAPACITY_BOUNDARY_MAX);

    for (stage = 0; stage < ramp_count; ++stage) {
        const int n = ramp[stage];
        const int header_units = 1;
        const int vertex_units = 3 * n;
        const int total_units = header_units + vertex_units;
        const double percentage = (double)total_units / (double)BUDGET_UNITS * 100.0;

        if (n > DC_PVR_CAPACITY_BOUNDARY_MAX) {
            printf("  Ramp %d/%d: N=%d exceeds boundary max %d; stopping.\n",
                   stage + 1, ramp_count, n, DC_PVR_CAPACITY_BOUNDARY_MAX);
            break;
        }

        printf("  Ramp %d/%d: N=%d triangles (total=%d, %.1f%%)\n",
               stage + 1, ramp_count, n, total_units, percentage);

        if ((size_t)(n * 3) > SDL_SIZE_MAX / sizeof(SDL_Vertex) ||
            (size_t)(n * 3) > SDL_SIZE_MAX / sizeof(int)) {
            printf("  FAIL: integer overflow in allocation size\n");
            goto fail_cleanup;
        }

        geom = (SDL_Vertex *)SDL_malloc((size_t)(n * 3) * sizeof(SDL_Vertex));
        if (!geom) {
            return fail_message("geom malloc");
        }
        indices = (int *)SDL_malloc((size_t)(n * 3) * sizeof(int));
        if (!indices) {
            SDL_free(geom);
            return fail_message("indices malloc");
        }

        {
            int i;
            const SDL_Color c = make_color(255, 100, 100);

            for (i = 0; i < n; ++i) {
                const int tx = (i * 37 % 600) + 20;
                const int ty = (i * 53 % 420) + 20;
                geom[i * 3 + 0].position.x = (float)tx;
                geom[i * 3 + 0].position.y = (float)ty;
                geom[i * 3 + 0].color = c;
                geom[i * 3 + 0].tex_coord.x = 0.0f;
                geom[i * 3 + 0].tex_coord.y = 0.0f;
                geom[i * 3 + 1].position.x = (float)(tx + 16);
                geom[i * 3 + 1].position.y = (float)(ty + 16);
                geom[i * 3 + 1].color = c;
                geom[i * 3 + 1].tex_coord.x = 0.0f;
                geom[i * 3 + 1].tex_coord.y = 0.0f;
                geom[i * 3 + 2].position.x = (float)(tx + 4);
                geom[i * 3 + 2].position.y = (float)(ty + 28);
                geom[i * 3 + 2].color = c;
                geom[i * 3 + 2].tex_coord.x = 0.0f;
                geom[i * 3 + 2].tex_coord.y = 0.0f;
                indices[i * 3 + 0] = i * 3 + 0;
                indices[i * 3 + 1] = i * 3 + 1;
                indices[i * 3 + 2] = i * 3 + 2;
            }
        }

        if (SDL_RenderGeometry(renderer, NULL, geom, n * 3,
                               indices, n * 3) < 0) {
            goto fail_cleanup;
        }
        if (present_checked(renderer) < 0) {
            goto fail_cleanup;
        }

        SDL_free(geom);
        SDL_free(indices);
        geom = NULL;
        indices = NULL;

        printf("  Ramp %d/%d: N=%d completed (total=%d, %.1f%%)\n",
               stage + 1, ramp_count, n, total_units, percentage);
    }

    printf("  Workload F completed every ramp in the configured maximum.\n");
    printf("  WARNING: console reset may be required if output was wrong.\n");
    return 0;

fail_cleanup:
    if (geom) {
        SDL_free(geom);
    }
    if (indices) {
        SDL_free(indices);
    }
    return -1;
}

#endif /* DC_PVR_CAPACITY_BOUNDARY_PROBE */

/* ----------------------------------------------------------------
 * Initialization and driver selection.
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * Build a small reusable background texture for Workload E.
 * ---------------------------------------------------------------- */

static SDL_Texture *create_bg_texture(SDL_Renderer *renderer)
{
    SDL_Texture *tex = NULL;
    Uint16 pixels[16 * 16];
    int x, y;

    tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                            SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!tex) {
        return NULL;
    }
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            pixels[y * 16 + x] = (Uint16)(((Uint16)((x * 17) & 0x1f) << 11) |
                                          ((Uint16)((y * 13) & 0x3f) << 5) |
                                          (Uint16)((x * 7 + y * 11) & 0x1f));
        }
    }
    if (SDL_UpdateTexture(tex, NULL, pixels, 16 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(tex);
        return NULL;
    }
    return tex;
}

/* ----------------------------------------------------------------
 * Build the small textured quad used by Workloads B and C.
 * ---------------------------------------------------------------- */

static SDL_Texture *create_quad_tex(SDL_Renderer *renderer)
{
    SDL_Texture *tex = NULL;
    Uint16 pixels[8 * 8];
    int x, y;

    tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                            SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!tex) {
        return NULL;
    }
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            pixels[y * 8 + x] = (Uint16)(((Uint16)(x & 0x1f) << 11) |
                                          ((Uint16)(y & 0x3f) << 5) |
                                          (Uint16)((x + y) & 0x1f));
        }
    }
    if (SDL_UpdateTexture(tex, NULL, pixels, 8 * (int)sizeof(Uint16)) < 0) {
        SDL_DestroyTexture(tex);
        return NULL;
    }
    return tex;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *quad_tex = NULL;
    SDL_Texture *bg_tex = NULL;
    const char *video_driver;
    int driver;
    SDL_RendererInfo renderer_info;
    int result = 1;

    (void)argc;
    (void)argv;
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
                      (cont_btn_callback_t)arch_exit);

    if (SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO") != SDL_TRUE) {
        printf("FAIL: could not select SDL_DC_DIRECT_VIDEO\n");
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    video_driver = SDL_GetCurrentVideoDriver();
    if (!video_driver) {
        printf("FAIL: SDL_GetCurrentVideoDriver returned NULL\n");
        goto done;
    }
    printf("Selected video driver: %s (mode SDL_DC_DIRECT_VIDEO)\n",
           video_driver);

    driver = find_pvr_driver();
    if (driver < 0) {
        printf("FAIL: Dreamcast PVR renderer is unavailable\n");
        goto done;
    }
    if (SDL_GetRenderDriverInfo(driver, &renderer_info) < 0) {
        printf("FAIL: SDL_GetRenderDriverInfo: %s\n", SDL_GetError());
        goto done;
    }
    printf("Selected render driver: %s\n", renderer_info.name);
    {
        Uint32 format_index;

        printf("Selected pixel formats:");
        for (format_index = 0; format_index < renderer_info.num_texture_formats;
             ++format_index) {
            printf(" %s", SDL_GetPixelFormatName(
                              renderer_info.texture_formats[format_index]));
        }
        printf("\n");
    }
    printf("Renderer flags: %u\n", (unsigned)renderer_info.flags);

    window = SDL_CreateWindow("PVR capacity stress", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, OUTPUT_W, OUTPUT_H,
                              SDL_WINDOW_SHOWN);
    if (!window) {
        printf("FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
        goto done;
    }
    printf("Window: %dx%d\n", OUTPUT_W, OUTPUT_H);

    renderer = SDL_CreateRenderer(window, driver, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("FAIL: SDL_CreateRenderer: %s\n", SDL_GetError());
        goto done;
    }
    if (SDL_GetRendererInfo(renderer, &renderer_info) < 0) {
        printf("FAIL: SDL_GetRendererInfo: %s\n", SDL_GetError());
        goto done;
    }
    printf("Active renderer: %s\n", renderer_info.name);
    if (!renderer_info.name ||
        SDL_strcmp(renderer_info.name, "Dreamcast PVR") != 0) {
        printf("FAIL: unexpected active renderer\n");
        goto done;
    }

    quad_tex = create_quad_tex(renderer);
    if (!quad_tex) {
        printf("FAIL: create_quad_tex\n");
        goto done;
    }
    bg_tex = create_bg_texture(renderer);
    if (!bg_tex) {
        printf("FAIL: create_bg_tex\n");
        goto done;
    }

    if (workload_a(renderer) < 0) {
        goto done;
    }
    if (workload_b(renderer, quad_tex) < 0) {
        goto done;
    }
    if (workload_c(renderer, quad_tex) < 0) {
        goto done;
    }
    if (workload_d(renderer) < 0) {
        goto done;
    }
    if (workload_e(renderer, bg_tex) < 0) {
        goto done;
    }

#ifdef DC_PVR_CAPACITY_BOUNDARY_PROBE
    if (workload_f(renderer) < 0) {
        goto done;
    }
    printf("\nWorkload F boundary probe completed.\n");
#else
    printf("\nWorkload F is excluded (compile with DC_PVR_CAPACITY_BOUNDARY_PROBE).\n");
#endif

    printf("\nPASS: all ordinary workloads (A-E) completed.\n");
    SDL_Delay(FINAL_HOLD_MS);
    result = 0;

done:
    if (bg_tex) {
        SDL_DestroyTexture(bg_tex);
    }
    if (quad_tex) {
        SDL_DestroyTexture(quad_tex);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
