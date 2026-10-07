/* Host-side differential test: the old and new triangle emit paths must produce the
 * same vertex sequence for every input. Both are extracted verbatim from the source by
 * extract.py. Build and run with run.sh. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef uint8_t Uint8;
#define SDL_memcpy memcpy
#define SDL_floor floor
#define SDL_assert(x) ((void)0)
static bool SDL_SetError(const char *fmt, ...) { (void)fmt; return false; }
#define DC_PVR_CLIP_MAX_VERTICES 7
#define PVR_CMD_VERTEX 0xe0000000u
#define PVR_CMD_VERTEX_EOL 0xf0000000u

typedef struct { float x, y, u, v; Uint32 color; } DC_PVR_Vertex;
typedef struct { int left, top, right, bottom; } DC_PVR_GeometryBounds;
typedef enum { DC_PVR_CLIP_LEFT, DC_PVR_CLIP_RIGHT, DC_PVR_CLIP_TOP, DC_PVR_CLIP_BOTTOM } DC_PVR_ClipPlane;

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
    printf("equivalence (quads): iterations=%ld with_output=%ld fast_path=%ld mismatches=%ld\n", iterations, quad_with_output, quad_fast, quad_mismatches);
    mismatches += quad_mismatches;
    printf("equivalence: iterations=%ld with_output=%ld fast_path_inputs=%ld clipped_or_partial_with_output=%ld mismatches=%ld\n",
           iterations, with_output, fast_candidates, clipped, mismatches);
    return mismatches ? 1 : 0;
}
