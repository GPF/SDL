/* Host-side differential test: the old and new triangle emit paths must produce the
 * same vertex sequence for every input. Both are extracted verbatim from the source by
 * extract.py. Build and run with run.sh. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef uint8_t Uint8;
typedef uint16_t Uint16;
#define SDL_memcpy memcpy
#define SDL_floor floor
#define SDL_assert(x) ((void)0)
#define SDL_SIZE_MAX SIZE_MAX
#define SDL_BLENDMODE_NONE 0
#define SDL_BLENDMODE_BLEND 1
#define SDL_TEXTUREACCESS_TARGET 1
#define SDL_RENDERCMD_COPY 1
#define SDL_RENDERCMD_GEOMETRY 2
#define SDL_TEXTURE_ADDRESS_CLAMP 0
#define SDL_VIDEO_RENDER_DREAMCAST_PVR 1
static char last_error[160];
static bool SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, ap);
    va_end(ap);
    return false;
}
static bool SDL_InvalidParamError(const char *name) { return SDL_SetError("invalid:%s", name); }
#define DC_PVR_CLIP_MAX_VERTICES 7
#define PVR_CMD_VERTEX 0xe0000000u
#define PVR_CMD_VERTEX_EOL 0xf0000000u

typedef struct { float x, y, u, v; Uint32 color; } DC_PVR_Vertex;
typedef struct { int left, top, right, bottom; } DC_PVR_GeometryBounds;
typedef enum { DC_PVR_CLIP_LEFT, DC_PVR_CLIP_RIGHT, DC_PVR_CLIP_TOP, DC_PVR_CLIP_BOTTOM } DC_PVR_ClipPlane;
typedef struct { float x, y, w, h; } SDL_FRect;
typedef struct { float r, g, b, a; } SDL_FColor;
typedef struct { float x, y; } SDL_FPoint;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef int SDL_TextureAccess;
typedef int SDL_BlendMode;
typedef int SDL_ScaleMode;
typedef int SDL_TextureAddressMode;
typedef struct DC_PVR_TextureData {
    int logical_w, logical_h, backing_w, backing_h;
    SDL_TextureAccess access;
    bool current_valid;
} DC_PVR_TextureData;
typedef struct SDL_Texture {
    void *internal;
    int w, h;
    SDL_FColor color;
} SDL_Texture;
typedef struct { struct { float x, y; } current_scale; } SDL_RenderViewState;
typedef struct SDL_RenderCommand {
    int command;
    union {
        struct {
            size_t first, count;
            float color_scale;
            SDL_BlendMode blend;
            SDL_Texture *texture;
            SDL_ScaleMode texture_scale_mode;
            SDL_TextureAddressMode texture_address_mode_u, texture_address_mode_v;
        } draw;
    } data;
} SDL_RenderCommand;
typedef struct { SDL_Texture *current_target; } DC_PVR_RenderData;
typedef struct SDL_Renderer {
    void *internal;
    SDL_RenderViewState *view;
    Uint8 vertex_data[1024];
    size_t vertex_data_used;
} SDL_Renderer;
static void *SDL_AllocateRenderVertices(SDL_Renderer *renderer, size_t numbytes,
                                        size_t alignment, size_t *offset)
{
    size_t start = renderer->vertex_data_used;
    if (alignment && (start & (alignment - 1))) {
        start += alignment - (start & (alignment - 1));
    }
    if (start + numbytes > sizeof(renderer->vertex_data)) {
        return NULL;
    }
    *offset = start;
    renderer->vertex_data_used = start + numbytes;
    return renderer->vertex_data + start;
}

typedef struct { float x, y, u, v; Uint32 color, flags; } Emitted;
static Emitted seq[64];
static int seq_n;

#define DEFINE_SIDE(PREFIX) \
    static bool PREFIX##EmitVertex(const DC_PVR_Vertex *s, Uint32 flags) \
    { Emitted e = { s->x, s->y, s->u, s->v, s->color, flags }; if (seq_n < 64) seq[seq_n++] = e; return true; }

#define PREFIX_OLD OLD_
#define PREFIX_NEW NEW_

#include "old_funcs.inc"
#include "new_funcs.inc"

static uint32_t rng_state = 12345u;
static uint32_t rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }
static float frnd(void) { return (float)(rnd() & 0xffff) / 65536.0f; }

static float copy_random_float(void)
{
    switch (rnd() % 48) {
    case 0: return NAN;
    case 1: return INFINITY;
    case 2: return -INFINITY;
    case 3: return -0.0f;
    default: return (float)((int)(rnd() % 2400) - 800) * frnd();
    }
}

static int inside_bias;

static float coord(int axis_lo, int axis_hi)
{
    /* Half of the iterations use only in-bounds coordinates so the fast path is
     * exercised heavily; the rest mix in edges, outside values and huge values. */
    static const int inside_kinds[6] = { 0, 4, 7, 9, 9, 3 };
    const int kind = inside_bias ? inside_kinds[rnd() % 6] : (int)(rnd() % 12);
    const float span = (float)(axis_hi - axis_lo);
    switch (kind) {
    case 0: return (float)axis_lo;                       /* exactly on the low edge */
    case 1: return (float)axis_hi;                       /* exactly on the high edge */
    case 2: return (float)axis_lo - 0.001f;
    case 3: return (float)axis_hi - 0.001f;
    case 4: return (float)(axis_lo + (int)(rnd() % (unsigned)(axis_hi - axis_lo)));      /* integer inside */
    case 5: return (float)axis_lo - span * frnd();       /* outside low */
    case 6: return (float)axis_hi + span * frnd();       /* outside high */
    case 7: return (float)axis_lo + 0.5f;
    case 8: return 1.0e30f * (frnd() - 0.5f);            /* huge */
    default: return (float)axis_lo + span * frnd();      /* fractional inside */
    }
}

int main(int argc, char **argv)
{
    const long iterations = argc > 1 ? atol(argv[1]) : 2000000;
    long mismatches = 0, with_output = 0, fast_candidates = 0, clipped = 0;

    for (long it = 0; it < iterations; ++it) {
        DC_PVR_GeometryBounds b;
        DC_PVR_Vertex t[3];
        Emitted old_seq[64], new_seq[64];
        int old_n, new_n;

        inside_bias = (it & 1);
        b.left = (int)(rnd() % 40);
        b.top = (int)(rnd() % 40);
        b.right = b.left + 1 + (int)(rnd() % 640);
        b.bottom = b.top + 1 + (int)(rnd() % 480);
        for (int i = 0; i < 3; ++i) {
            t[i].x = coord(b.left, b.right);
            t[i].y = coord(b.top, b.bottom);
            t[i].u = frnd() * 64.0f;
            t[i].v = frnd() * 64.0f;
            t[i].color = rnd() * 2654435761u;
        }
        switch (rnd() % 16) { /* degenerate and special shapes */
        case 0: t[2] = t[0]; break;
        case 1: t[1] = t[0]; break;
        case 2: t[2].x = t[0].x; t[1].x = t[0].x; break;       /* vertical sliver */
        case 3: t[2].y = t[0].y; t[1].y = t[0].y; break;       /* horizontal sliver */
        case 4: t[1].y = t[0].y; break;                        /* tie on y: x breaks it */
        case 5: t[0].x = NAN; break;
        case 6: t[1].y = INFINITY; break;
        case 7: t[2].u = NAN; break;
        default: break;
        }

        seq_n = 0; (void)OLD_EmitClippedTriangle(t, &b); old_n = seq_n; memcpy(old_seq, seq, sizeof(Emitted) * (size_t)old_n);
        seq_n = 0; (void)NEW_EmitClippedTriangle(t, &b); new_n = seq_n; memcpy(new_seq, seq, sizeof(Emitted) * (size_t)new_n);

        if (old_n) with_output++;
        if (NEW_TriangleInsideBounds(t, &b) && NEW_VertexIsFinite(&t[0]) && NEW_VertexIsFinite(&t[1]) && NEW_VertexIsFinite(&t[2])) fast_candidates++; else if (old_n) clipped++;

        if (old_n != new_n || memcmp(old_seq, new_seq, sizeof(Emitted) * (size_t)old_n) != 0) {
            if (mismatches < 5) {
                printf("MISMATCH it=%ld bounds=[%d,%d,%d,%d] old_n=%d new_n=%d\n", it, b.left, b.top, b.right, b.bottom, old_n, new_n);
                for (int i = 0; i < 3; ++i) printf("  v%d = (%g,%g) uv(%g,%g) c=%08x\n", i, t[i].x, t[i].y, t[i].u, t[i].v, t[i].color);
            }
            mismatches++;
        }
    }
    /* Quads as RunGeometryCommand builds them: an axis-aligned rectangle from x, y,
     * width, height with u = v = 0 and a per-primitive colour. */
    long quad_mismatches = 0, quad_fast = 0, quad_with_output = 0;
    for (long it = 0; it < iterations; ++it) {
        DC_PVR_GeometryBounds b;
        DC_PVR_Vertex q[4];
        Emitted old_seq[64], new_seq[64];
        int old_n, new_n;
        float x, y, w, h;

        inside_bias = (it & 1);
        b.left = (int)(rnd() % 40);
        b.top = (int)(rnd() % 40);
        b.right = b.left + 1 + (int)(rnd() % 640);
        b.bottom = b.top + 1 + (int)(rnd() % 480);
        x = coord(b.left, b.right);
        y = coord(b.top, b.bottom);
        switch (rnd() % 8) {
        case 0: w = 1.0f; h = 1.0f; break;                  /* a point */
        case 1: w = 1.0e-10f; h = 1.0f + frnd(); break;     /* width lost to rounding */
        case 2: w = 1.0f + frnd(); h = 1.0e-10f; break;
        case 3: w = (float)(b.right - b.left); h = (float)(b.bottom - b.top); break; /* whole bounds */
        case 4: w = 1.0e30f; h = 1.0f; break;
        case 5: w = NAN; h = 1.0f; break;
        default: w = 0.5f + (float)(rnd() % 64) * frnd(); h = 0.5f + (float)(rnd() % 64) * frnd(); break;
        }
        {
            const Uint32 color = rnd() * 2654435761u;
            for (int k = 0; k < 4; ++k) { q[k].u = 0.0f; q[k].v = 0.0f; q[k].color = color; }
            q[0].x = x;     q[0].y = y;
            q[1].x = x + w; q[1].y = y;
            q[2].x = x + w; q[2].y = y + h;
            q[3].x = x;     q[3].y = y + h;
        }
        seq_n = 0; (void)OLD_EmitClippedQuad(q, &b); old_n = seq_n; memcpy(old_seq, seq, sizeof(Emitted) * (size_t)old_n);
        seq_n = 0; (void)NEW_EmitClippedQuad(q, &b); new_n = seq_n; memcpy(new_seq, seq, sizeof(Emitted) * (size_t)new_n);
        if (old_n) quad_with_output++;
        if (NEW_RectQuadFastPath(q, &b)) quad_fast++;
        if (old_n != new_n || memcmp(old_seq, new_seq, sizeof(Emitted) * (size_t)old_n) != 0) {
            if (quad_mismatches < 5) {
                printf("QUAD MISMATCH it=%ld bounds=[%d,%d,%d,%d] x=%g y=%g w=%g h=%g old_n=%d new_n=%d\n", it, b.left, b.top, b.right, b.bottom, x, y, w, h, old_n, new_n);
            }
            quad_mismatches++;
        }
    }

    long copy_mismatches = 0, copy_fast = 0, copy_fallback = 0, copy_emission_mismatches = 0;
    static const float copy_scales[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 1.5f, 0.7f, 1.1f, 3.25f,
                                         0.0f, -2.0f, NAN, INFINITY, -INFINITY };
    for (long it = 0; it < iterations; ++it) {
        SDL_RenderViewState view = { { copy_scales[rnd() % (sizeof(copy_scales) / sizeof(copy_scales[0]))],
                                       copy_scales[rnd() % (sizeof(copy_scales) / sizeof(copy_scales[0]))] } };
        DC_PVR_RenderData old_data = { NULL }, new_data = { NULL };
        SDL_Renderer old_renderer = { 0 }, new_renderer = { 0 };
        DC_PVR_TextureData old_texture_data, new_texture_data;
        SDL_Texture old_texture, new_texture;
        SDL_RenderCommand old_cmd = { 0 }, new_cmd = { 0 };
        SDL_FRect src, dst, scaled_dst;
        float xy[8], uv[8];
        const Uint32 indices[6] = { 0, 1, 2, 0, 2, 3 };
        const DC_PVR_Vertex *old_vertices;
        DC_PVR_Vertex new_quad[4];
        DC_PVR_GeometryBounds bounds;
        bool old_ok, new_ok;
        char old_error[sizeof(last_error)];
        int texture_w, texture_h, backing_w, backing_h;

        if ((rnd() & 1u) == 0) {
            texture_w = 1 << (1 + rnd() % 8);
            texture_h = 1 << (1 + rnd() % 8);
        } else {
            texture_w = 3 + (int)(rnd() % 251);
            texture_h = 3 + (int)(rnd() % 251);
        }
        backing_w = 1;
        while (backing_w < texture_w) backing_w <<= 1;
        backing_h = 1;
        while (backing_h < texture_h) backing_h <<= 1;
        old_texture_data.logical_w = new_texture_data.logical_w = texture_w;
        old_texture_data.logical_h = new_texture_data.logical_h = texture_h;
        old_texture_data.backing_w = new_texture_data.backing_w = backing_w;
        old_texture_data.backing_h = new_texture_data.backing_h = backing_h;
        if ((it % 257) == 0) old_texture_data.logical_w = new_texture_data.logical_w = 0;
        if ((it % 257) == 1) old_texture_data.backing_h = new_texture_data.backing_h = 0;
        old_texture_data.access = new_texture_data.access = (rnd() & 1u) ? SDL_TEXTUREACCESS_TARGET : 0;
        old_texture_data.current_valid = new_texture_data.current_valid = (rnd() % 16u) != 0;
        old_texture.internal = &old_texture_data;
        new_texture.internal = &new_texture_data;
        old_texture.w = new_texture.w = texture_w;
        old_texture.h = new_texture.h = texture_h;
        old_texture.color.r = new_texture.color.r = copy_random_float();
        old_texture.color.g = new_texture.color.g = copy_random_float();
        old_texture.color.b = new_texture.color.b = copy_random_float();
        old_texture.color.a = new_texture.color.a = copy_random_float();
        old_renderer.internal = &old_data;
        new_renderer.internal = &new_data;
        old_renderer.view = new_renderer.view = &view;
        if (old_texture_data.access == SDL_TEXTUREACCESS_TARGET && (rnd() % 128u) == 0) {
            old_data.current_target = &old_texture;
            new_data.current_target = &new_texture;
        }
        old_cmd.command = SDL_RENDERCMD_GEOMETRY;
        new_cmd.command = SDL_RENDERCMD_COPY;
        old_cmd.data.draw.blend = new_cmd.data.draw.blend = SDL_BLENDMODE_BLEND;
        old_cmd.data.draw.color_scale = new_cmd.data.draw.color_scale = copy_random_float();
        old_cmd.data.draw.texture = &old_texture;
        new_cmd.data.draw.texture = &new_texture;
        src.x = copy_random_float(); src.y = copy_random_float();
        src.w = copy_random_float(); src.h = copy_random_float();
        dst.x = copy_random_float(); dst.y = copy_random_float();
        dst.w = copy_random_float(); dst.h = copy_random_float();
        scaled_dst.x = dst.x * view.current_scale.x;
        scaled_dst.y = dst.y * view.current_scale.y;
        scaled_dst.w = dst.w * view.current_scale.x;
        scaled_dst.h = dst.h * view.current_scale.y;

        xy[0] = dst.x; xy[1] = dst.y;
        xy[2] = dst.x + dst.w; xy[3] = dst.y;
        xy[4] = dst.x + dst.w; xy[5] = dst.y + dst.h;
        xy[6] = dst.x; xy[7] = dst.y + dst.h;
        uv[0] = src.x / texture_w; uv[1] = src.y / texture_h;
        uv[2] = (src.x + src.w) / texture_w; uv[3] = uv[1];
        uv[4] = uv[2]; uv[5] = (src.y + src.h) / texture_h;
        uv[6] = uv[0]; uv[7] = uv[5];

        last_error[0] = '\0';
        old_ok = OLD_QueueGeometry(&old_renderer, &old_cmd, &old_texture,
                                   xy, 2 * (int)sizeof(float), &old_texture.color, 0,
                                   uv, 2 * (int)sizeof(float), 4, indices, 6, 4,
                                   view.current_scale.x, view.current_scale.y);
        snprintf(old_error, sizeof(old_error), "%s", last_error);
        last_error[0] = '\0';
        new_ok = NEW_QueueCopy(&new_renderer, &new_cmd, &new_texture, &src,
                               &scaled_dst, &dst);
        if (old_ok != new_ok || strcmp(old_error, last_error) != 0) {
            copy_mismatches++;
            continue;
        }
        if (!old_ok) {
            continue;
        }
        old_vertices = (const DC_PVR_Vertex *)(old_renderer.vertex_data + old_cmd.data.draw.first);
        if (new_cmd.data.draw.count == 4) {
            const DC_PVR_Vertex *queued = (const DC_PVR_Vertex *)(new_renderer.vertex_data + new_cmd.data.draw.first);
            memcpy(new_quad, queued, sizeof(new_quad));
            copy_fast++;
        } else if (new_cmd.data.draw.count == 6) {
            const DC_PVR_Vertex *queued = (const DC_PVR_Vertex *)(new_renderer.vertex_data + new_cmd.data.draw.first);
            new_quad[0] = queued[0]; new_quad[1] = queued[1];
            new_quad[2] = queued[2]; new_quad[3] = queued[5];
            copy_fallback++;
        } else {
            copy_mismatches++;
            continue;
        }
        for (int i = 0; i < 6; ++i) {
            if (memcmp(&old_vertices[i], &new_quad[indices[i]], sizeof(DC_PVR_Vertex)) != 0) {
                copy_mismatches++;
                break;
            }
        }

        bounds.left = (int)(rnd() % 40);
        bounds.top = (int)(rnd() % 40);
        bounds.right = bounds.left + 1 + (int)(rnd() % 640);
        bounds.bottom = bounds.top + 1 + (int)(rnd() % 480);
        {
            Emitted old_seq[64], new_seq[64];
            int old_n, new_n;
            seq_n = 0;
            for (int i = 0; i < 2; ++i) {
                DC_PVR_Vertex tri[3] = { old_vertices[i * 3], old_vertices[i * 3 + 1], old_vertices[i * 3 + 2] };
                (void)OLD_EmitClippedTriangle(tri, &bounds);
            }
            old_n = seq_n;
            memcpy(old_seq, seq, sizeof(Emitted) * (size_t)old_n);
            seq_n = 0; (void)NEW_EmitClippedQuad(new_quad, &bounds);
            new_n = seq_n;
            memcpy(new_seq, seq, sizeof(Emitted) * (size_t)new_n);
            if (old_n != new_n || memcmp(old_seq, new_seq, sizeof(Emitted) * (size_t)old_n) != 0) {
                if (copy_emission_mismatches < 5) {
                    printf("COPY EMIT MISMATCH it=%ld bounds=[%d,%d,%d,%d] old_n=%d new_n=%d\\n",
                           it, bounds.left, bounds.top, bounds.right, bounds.bottom,
                           old_n, new_n);
                }
                copy_emission_mismatches++;
            }
        }
    }
    printf("equivalence (QueueCopy): iterations=%ld fast=%ld fallback=%ld record_mismatches=%ld emitted_mismatches=%ld\n",
           iterations, copy_fast, copy_fallback, copy_mismatches, copy_emission_mismatches);
    mismatches += copy_mismatches + copy_emission_mismatches;
    printf("equivalence (quads): iterations=%ld with_output=%ld fast_path=%ld mismatches=%ld\n", iterations, quad_with_output, quad_fast, quad_mismatches);
    mismatches += quad_mismatches;
    printf("equivalence: iterations=%ld with_output=%ld fast_path_inputs=%ld clipped_or_partial_with_output=%ld mismatches=%ld\n",
           iterations, with_output, fast_candidates, clipped, mismatches);
    return mismatches ? 1 : 0;
}
