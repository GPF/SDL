/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  Native Dreamcast PVR renderer, Phase 0: command ingestion and primitive
  emission. Queue callbacks are CPU-only ingestion; only the emission path
  touches the PVR.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_DREAMCAST_PVR

#include "SDL_render_dreamcast_pvr.h"

#include <math.h>

#define DC_PVR_CLIP_MAX_VERTICES 7

typedef struct DC_PVR_GeometryBounds
{
    int left;
    int top;
    int right;
    int bottom;
} DC_PVR_GeometryBounds;

typedef enum DC_PVR_GeometryBoundsState
{
    DC_PVR_GEOMETRY_BOUNDS_VIEWPORT,
    DC_PVR_GEOMETRY_BOUNDS_CLIP,
    DC_PVR_GEOMETRY_BOUNDS_EMPTY
} DC_PVR_GeometryBoundsState;

typedef enum DC_PVR_ClipPlane
{
    DC_PVR_CLIP_LEFT,
    DC_PVR_CLIP_RIGHT,
    DC_PVR_CLIP_TOP,
    DC_PVR_CLIP_BOTTOM
} DC_PVR_ClipPlane;

/* Single shared float-to-byte policy. See the contract in the header. */
static Uint8 DC_PVR_FloatChannelToByte(float value)
{
    if (!(value > 0.0f)) {
        return 0;
    }
    if (value >= 1.0f) {
        return 255;
    }
    return (Uint8)(value * 255.0f + 0.5f);
}

Uint32 DC_PVR_PackColor(const SDL_FColor *color, float color_scale)
{
    /* SDL3 applies color_scale to RGB only; alpha is never scaled. */
    const Uint8 a = DC_PVR_FloatChannelToByte(color->a);
    const Uint8 r = DC_PVR_FloatChannelToByte(color->r * color_scale);
    const Uint8 g = DC_PVR_FloatChannelToByte(color->g * color_scale);
    const Uint8 b = DC_PVR_FloatChannelToByte(color->b * color_scale);

    return ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

void DC_PVR_TranslateToFramebuffer(const DC_PVR_RenderData *data, float *x, float *y)
{
    *x += (float)data->pixel_viewport.x;
    *y += (float)data->pixel_viewport.y;
}

static DC_PVR_GeometryBoundsState DC_PVR_CalcEffectiveGeometryBounds(
    const DC_PVR_RenderData *data, DC_PVR_GeometryBounds *bounds)
{
    Sint64 left = 0;
    Sint64 top = 0;
    Sint64 right = data->render_w;
    Sint64 bottom = data->render_h;
    const Sint64 viewport_x = data->pixel_viewport.x;
    const Sint64 viewport_y = data->pixel_viewport.y;
    Sint64 viewport_right;
    Sint64 viewport_bottom;

    if (data->pixel_viewport.w <= 0 || data->pixel_viewport.h <= 0) {
        return DC_PVR_GEOMETRY_BOUNDS_EMPTY;
    }
    viewport_right = viewport_x + (Sint64)data->pixel_viewport.w;
    viewport_bottom = viewport_y + (Sint64)data->pixel_viewport.h;
    if (viewport_x > left) {
        left = viewport_x;
    }
    if (viewport_y > top) {
        top = viewport_y;
    }
    if (viewport_right < right) {
        right = viewport_right;
    }
    if (viewport_bottom < bottom) {
        bottom = viewport_bottom;
    }

    if (right <= left || bottom <= top) {
        return DC_PVR_GEOMETRY_BOUNDS_EMPTY;
    }

    if (data->clipping_enabled) {
        Sint64 clip_left;
        Sint64 clip_top;
        Sint64 clip_right;
        Sint64 clip_bottom;

        if (data->pixel_clip_rect.w <= 0 || data->pixel_clip_rect.h <= 0) {
            return DC_PVR_GEOMETRY_BOUNDS_EMPTY;
        }
        /* pixel_clip_rect is viewport-relative, so it is offset by the pixel
         * viewport origin before intersecting. */
        clip_left = viewport_x + (Sint64)data->pixel_clip_rect.x;
        clip_top = viewport_y + (Sint64)data->pixel_clip_rect.y;
        clip_right = clip_left + (Sint64)data->pixel_clip_rect.w;
        clip_bottom = clip_top + (Sint64)data->pixel_clip_rect.h;
        if (clip_left > left) {
            left = clip_left;
        }
        if (clip_top > top) {
            top = clip_top;
        }
        if (clip_right < right) {
            right = clip_right;
        }
        if (clip_bottom < bottom) {
            bottom = clip_bottom;
        }
        if (right <= left || bottom <= top) {
            return DC_PVR_GEOMETRY_BOUNDS_EMPTY;
        }
    }

    bounds->left = (int)left;
    bounds->top = (int)top;
    bounds->right = (int)right;
    bounds->bottom = (int)bottom;
    return data->clipping_enabled ? DC_PVR_GEOMETRY_BOUNDS_CLIP
                                  : DC_PVR_GEOMETRY_BOUNDS_VIEWPORT;
}

static Uint32 DC_PVR_InterpolateColor(Uint32 color0, Uint32 color1, double t)
{
    Uint32 result = 0;
    int shift;

    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }

    for (shift = 0; shift <= 24; shift += 8) {
        const int channel0 = (int)((color0 >> shift) & 0xffu);
        const int channel1 = (int)((color1 >> shift) & 0xffu);
        const double interpolated = channel0 + (channel1 - channel0) * t;
        int channel = (int)SDL_floor(interpolated + 0.5);

        if (channel < 0) {
            channel = 0;
        } else if (channel > 255) {
            channel = 255;
        }
        result |= (Uint32)channel << shift;
    }
    return result;
}

static void DC_PVR_InterpolateVertex(DC_PVR_Vertex *result,
                                     const DC_PVR_Vertex *start,
                                     const DC_PVR_Vertex *end, double t)
{
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }

    result->x = (float)((double)start->x + ((double)end->x - start->x) * t);
    result->y = (float)((double)start->y + ((double)end->y - start->y) * t);
    result->u = (float)((double)start->u + ((double)end->u - start->u) * t);
    result->v = (float)((double)start->v + ((double)end->v - start->v) * t);
    result->color = DC_PVR_InterpolateColor(start->color, end->color, t);
}

static double DC_PVR_ClipPlaneDistance(const DC_PVR_Vertex *vertex,
                                       DC_PVR_ClipPlane plane,
                                       const DC_PVR_GeometryBounds *bounds)
{
    switch (plane) {
    case DC_PVR_CLIP_LEFT:
        return (double)vertex->x - bounds->left;
    case DC_PVR_CLIP_RIGHT:
        return bounds->right - (double)vertex->x;
    case DC_PVR_CLIP_TOP:
        return (double)vertex->y - bounds->top;
    default:
        return bounds->bottom - (double)vertex->y;
    }
}

static bool DC_PVR_ClipPlaneContains(const DC_PVR_Vertex *vertex,
                                     DC_PVR_ClipPlane plane,
                                     const DC_PVR_GeometryBounds *bounds)
{
    switch (plane) {
    case DC_PVR_CLIP_LEFT:
        return vertex->x >= (float)bounds->left;
    case DC_PVR_CLIP_RIGHT:
        return vertex->x < (float)bounds->right;
    case DC_PVR_CLIP_TOP:
        return vertex->y >= (float)bounds->top;
    default:
        return vertex->y < (float)bounds->bottom;
    }
}

static bool DC_PVR_AppendClippedVertex(DC_PVR_Vertex *vertices, int *count,
                                       const DC_PVR_Vertex *vertex)
{
    SDL_assert(*count < DC_PVR_CLIP_MAX_VERTICES);
    if (*count >= DC_PVR_CLIP_MAX_VERTICES) {
        return SDL_SetError("Dreamcast PVR clipping exceeded the seven-vertex bound");
    }
    vertices[*count] = *vertex;
    ++(*count);
    return true;
}

/* CPU clipping, not PVR tile clipping: SDL clipping is pixel-exact and the
 * PVR would only bin geometry, which is not the same thing. */
static int DC_PVR_ClipAgainstPlane(const DC_PVR_Vertex *source, int source_count,
                                   DC_PVR_Vertex *destination,
                                   DC_PVR_ClipPlane plane,
                                   const DC_PVR_GeometryBounds *bounds)
{
    int destination_count = 0;
    int i;

    for (i = 0; i < source_count; ++i) {
        const DC_PVR_Vertex *start = &source[i];
        const DC_PVR_Vertex *end = &source[(i + 1) % source_count];
        const bool start_inside = DC_PVR_ClipPlaneContains(start, plane, bounds);
        const bool end_inside = DC_PVR_ClipPlaneContains(end, plane, bounds);

        if (start_inside != end_inside) {
            const double start_distance = DC_PVR_ClipPlaneDistance(start, plane, bounds);
            const double end_distance = DC_PVR_ClipPlaneDistance(end, plane, bounds);
            double t = start_distance / (start_distance - end_distance);
            DC_PVR_Vertex intersection;

            if (t < 0.0) {
                t = 0.0;
            } else if (t > 1.0) {
                t = 1.0;
            }
            DC_PVR_InterpolateVertex(&intersection, start, end, t);
            if (plane == DC_PVR_CLIP_LEFT) {
                intersection.x = (float)bounds->left;
            } else if (plane == DC_PVR_CLIP_RIGHT) {
                intersection.x = (float)bounds->right;
            } else if (plane == DC_PVR_CLIP_TOP) {
                intersection.y = (float)bounds->top;
            } else {
                intersection.y = (float)bounds->bottom;
            }
            if (!DC_PVR_AppendClippedVertex(destination, &destination_count, &intersection)) {
                return -1;
            }
        }
        if (end_inside) {
            if (!DC_PVR_AppendClippedVertex(destination, &destination_count, end)) {
                return -1;
            }
        }
    }
    return destination_count;
}

static int DC_PVR_ClipTriangle(const DC_PVR_Vertex triangle[3],
                               const DC_PVR_GeometryBounds *bounds,
                               DC_PVR_Vertex buffers[2][DC_PVR_CLIP_MAX_VERTICES],
                               DC_PVR_Vertex **result)
{
    int source_buffer = 0;
    int count = 3;
    int plane;

    buffers[0][0] = triangle[0];
    buffers[0][1] = triangle[1];
    buffers[0][2] = triangle[2];

    for (plane = DC_PVR_CLIP_LEFT; plane <= DC_PVR_CLIP_BOTTOM; ++plane) {
        const int destination_buffer = 1 - source_buffer;

        count = DC_PVR_ClipAgainstPlane(buffers[source_buffer], count,
                                        buffers[destination_buffer],
                                        (DC_PVR_ClipPlane)plane, bounds);
        if (count <= 0) {
            *result = buffers[destination_buffer];
            return count;
        }
        source_buffer = destination_buffer;
    }

    *result = buffers[source_buffer];
    return count;
}

/* KOS release flags include -ffast-math, which permits the compiler to fold
 * isfinite() to true. Check IEEE-754 exponent bits so malformed public
 * geometry is still rejected in the actual Dreamcast build. */
static bool DC_PVR_FloatIsFinite(float value)
{
    Uint32 bits;

    SDL_memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

static bool DC_PVR_DoubleIsFinite(double value)
{
    Uint64 bits;

    SDL_memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}

static bool DC_PVR_VertexIsFinite(const DC_PVR_Vertex *vertex)
{
    return (DC_PVR_FloatIsFinite(vertex->x) &&
            DC_PVR_FloatIsFinite(vertex->y) &&
            DC_PVR_FloatIsFinite(vertex->u) &&
            DC_PVR_FloatIsFinite(vertex->v));
}

static bool DC_PVR_ValidateQueuedBlend(SDL_RenderCommand *cmd)
{
    if (cmd->data.draw.blend != SDL_BLENDMODE_NONE &&
        cmd->data.draw.blend != SDL_BLENDMODE_BLEND) {
        return SDL_SetError("Unsupported Dreamcast PVR blend mode in Phase 0");
    }
    return true;
}

bool DC_PVR_Submit(const void *data, size_t size)
{
    if (pvr_prim(data, size) < 0) {
        return SDL_SetError("Dreamcast PVR rejected a render primitive");
    }
    return true;
}

static bool DC_PVR_EmitVertex(const DC_PVR_Vertex *source, Uint32 flags)
{
    pvr_vertex_t vertex;

    SDL_zero(vertex);
    vertex.flags = flags;
    vertex.x = source->x;
    vertex.y = source->y;
    vertex.z = 1.0f;
    vertex.u = source->u;
    vertex.v = source->v;
    vertex.argb = source->color;
    return DC_PVR_Submit(&vertex, sizeof(vertex));
}

static bool DC_PVR_EmitClippedTriangle(const DC_PVR_Vertex triangle[3],
                                       const DC_PVR_GeometryBounds *bounds)
{
    DC_PVR_Vertex buffers[2][DC_PVR_CLIP_MAX_VERTICES];
    DC_PVR_Vertex *clipped;
    int clipped_count;
    int fan_anchor = 0;
    int i;

    for (i = 0; i < 3; ++i) {
        if (!DC_PVR_VertexIsFinite(&triangle[i])) {
            return true;
        }
    }

    clipped_count = DC_PVR_ClipTriangle(triangle, bounds, buffers, &clipped);
    if (clipped_count < 0) {
        return false;
    }

    /* A cyclic rotation preserves winding. Choosing the same geometric fan
     * anchor for either input winding also gives the PVR the same diagonal
     * partition, avoiding winding-dependent interpolation across a clipped
     * polygon. */
    for (i = 1; i < clipped_count; ++i) {
        if (clipped[i].y < clipped[fan_anchor].y ||
            (clipped[i].y == clipped[fan_anchor].y &&
             clipped[i].x < clipped[fan_anchor].x)) {
            fan_anchor = i;
        }
    }

    for (i = 1; i + 1 < clipped_count; ++i) {
        const DC_PVR_Vertex *a = &clipped[fan_anchor];
        const DC_PVR_Vertex *b = &clipped[(fan_anchor + i) % clipped_count];
        const DC_PVR_Vertex *c = &clipped[(fan_anchor + i + 1) % clipped_count];
        const double area = ((double)b->x - a->x) * ((double)c->y - a->y) -
                            ((double)b->y - a->y) * ((double)c->x - a->x);

        if (!DC_PVR_VertexIsFinite(a) || !DC_PVR_VertexIsFinite(b) ||
            !DC_PVR_VertexIsFinite(c) || !DC_PVR_DoubleIsFinite(area) ||
            area == 0.0) {
            continue;
        }
        if (!DC_PVR_EmitVertex(a, PVR_CMD_VERTEX) ||
            !DC_PVR_EmitVertex(b, PVR_CMD_VERTEX) ||
            !DC_PVR_EmitVertex(c, PVR_CMD_VERTEX_EOL)) {
            return false;
        }
    }
    return true;
}

bool DC_PVR_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                            const SDL_FPoint *points, int count)
{
    DC_PVR_Vertex *vertices;

    if (!DC_PVR_ValidateQueuedBlend(cmd)) {
        return false;
    }
    if (count < 0 || (count > 0 && !points) ||
        (size_t)count > SDL_SIZE_MAX / sizeof(*vertices)) {
        return SDL_InvalidParamError("points");
    }
    vertices = (DC_PVR_Vertex *)SDL_AllocateRenderVertices(
        renderer, (size_t)count * sizeof(*vertices), 0, &cmd->data.draw.first);
    if (!vertices) {
        return false;
    }

    /* Core only reaches this callback when current_scale is exactly 1.0, so
     * the incoming coordinates are already viewport-relative pixels. */
    cmd->data.draw.count = (size_t)count;
    for (int i = 0; i < count; ++i) {
        vertices[i].x = points[i].x;
        vertices[i].y = points[i].y;
        vertices[i].u = 0.0f;
        vertices[i].v = 0.0f;
        vertices[i].color = DC_PVR_PackColor(&cmd->data.draw.color,
                                             cmd->data.draw.color_scale);
    }
    return true;
}

bool DC_PVR_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                           const SDL_FPoint *points, int count)
{
    /* Phase 0 has no PVR line primitive. SDL3's default line method renders
     * lines as fill rects, which reach QueueGeometry and work correctly.
     * This callback is only reached when an application explicitly selects
     * SDL_HINT_RENDER_LINE_METHOD=2, so reject at the queue boundary instead of
     * accepting the queue and failing later at emission. */
    (void)renderer;
    (void)cmd;

    if (count < 0 || (count > 0 && !points)) {
        return SDL_InvalidParamError("points");
    }
    return SDL_SetError("Dreamcast PVR does not implement the line primitive in Phase 0");
}

bool DC_PVR_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                           const SDL_FRect *rects, int count)
{
    DC_PVR_Vertex *vertices;

    if (!DC_PVR_ValidateQueuedBlend(cmd)) {
        return false;
    }
    if (count < 0 || (count > 0 && !rects) ||
        (size_t)count > SDL_SIZE_MAX / sizeof(*vertices)) {
        return SDL_InvalidParamError("rects");
    }
    for (int i = 0; i < count; ++i) {
        if (!DC_PVR_FloatIsFinite(rects[i].w)) {
            return SDL_SetError("Dreamcast PVR rectangle has a non-finite width");
        }
        if (!DC_PVR_FloatIsFinite(rects[i].h)) {
            return SDL_SetError("Dreamcast PVR rectangle has a non-finite height");
        }
    }
    vertices = (DC_PVR_Vertex *)SDL_AllocateRenderVertices(
        renderer, (size_t)count * sizeof(*vertices), 0, &cmd->data.draw.first);
    if (!vertices) {
        return false;
    }

    /* Core already applied current_scale to these rectangles. */
    cmd->data.draw.count = (size_t)count;
    for (int i = 0; i < count; ++i) {
        vertices[i].x = rects[i].x;
        vertices[i].y = rects[i].y;
        /* Rectangle commands temporarily store width and height in u and v. */
        vertices[i].u = rects[i].w;
        vertices[i].v = rects[i].h;
        vertices[i].color = DC_PVR_PackColor(&cmd->data.draw.color,
                                             cmd->data.draw.color_scale);
    }
    return true;
}

bool DC_PVR_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                          SDL_Texture *texture, const float *xy, int xy_stride,
                          const SDL_FColor *color, int color_stride,
                          const float *uv, int uv_stride, int num_vertices,
                          const void *indices, int num_indices, int size_indices,
                          float scale_x, float scale_y)
{
    DC_PVR_TextureData *texturedata = texture ? (DC_PVR_TextureData *)texture->internal : NULL;
    const int count = indices ? num_indices : num_vertices;
    DC_PVR_Vertex *vertices;

    if (!DC_PVR_ValidateQueuedBlend(cmd)) {
        return false;
    }
#if SDL_VIDEO_RENDER_DREAMCAST_PVR_TARGETS
    if (texture && texture == ((DC_PVR_RenderData *)renderer->internal)->current_target) {
        return SDL_SetError("Dreamcast PVR does not support sampling the active render target");
    }
#endif
    if (num_vertices < 0 || num_indices < 0 || count < 0 ||
        (count > 0 && (!xy || !color)) ||
        (texture && (!texturedata || !uv))) {
        return SDL_InvalidParamError("geometry");
    }
    if (xy_stride < (int)(2 * sizeof(float)) ||
        (color_stride != 0 && color_stride < (int)sizeof(SDL_FColor)) ||
        (texture && uv_stride < (int)(2 * sizeof(float)))) {
        return SDL_SetError("Dreamcast PVR geometry has an invalid vertex stride");
    }
    if (indices) {
        if (size_indices != 1 && size_indices != 2 && size_indices != 4) {
            return SDL_SetError("Dreamcast PVR geometry has an invalid index size");
        }
    } else if (size_indices != 0) {
        return SDL_SetError("Dreamcast PVR unindexed geometry has an index size");
    }
    if ((count % 3) != 0) {
        return SDL_SetError("Dreamcast PVR geometry is not an independent triangle list");
    }
    if (!DC_PVR_FloatIsFinite(scale_x) || !DC_PVR_FloatIsFinite(scale_y)) {
        return SDL_SetError("Dreamcast PVR geometry has a non-finite scale");
    }
    if ((size_t)count > SDL_SIZE_MAX / sizeof(*vertices)) {
        return SDL_SetError("Dreamcast PVR geometry vertex allocation overflow");
    }
    if (count == 0) {
        cmd->data.draw.count = 0;
        return true;
    }

    vertices = (DC_PVR_Vertex *)SDL_AllocateRenderVertices(renderer,
                                                           (size_t)count * sizeof(*vertices),
                                                           0, &cmd->data.draw.first);
    if (!vertices) {
        return false;
    }

    cmd->data.draw.count = count;
    for (int i = 0; i < count; ++i) {
        Uint32 source_index = (Uint32)i;
        size_t xy_offset;
        size_t color_offset;
        size_t uv_offset = 0;
        float position[2];
        SDL_FColor vertex_color;

        if (indices) {
            if (size_indices == 4) {
                SDL_memcpy(&source_index,
                           (const Uint8 *)indices + (size_t)i * sizeof(Uint32),
                           sizeof(source_index));
            } else if (size_indices == 2) {
                Uint16 index16;

                SDL_memcpy(&index16,
                           (const Uint8 *)indices + (size_t)i * sizeof(Uint16),
                           sizeof(index16));
                source_index = index16;
            } else {
                source_index = ((const Uint8 *)indices)[i];
            }
        }
        if (source_index >= (Uint32)num_vertices) {
            return SDL_SetError("Dreamcast PVR geometry index is out of range");
        }
        if ((size_t)source_index > SDL_SIZE_MAX / (size_t)xy_stride ||
            (color_stride != 0 &&
             (size_t)source_index > SDL_SIZE_MAX / (size_t)color_stride) ||
            (texture &&
             (size_t)source_index > SDL_SIZE_MAX / (size_t)uv_stride)) {
            return SDL_SetError("Dreamcast PVR geometry vertex offset overflow");
        }
        xy_offset = (size_t)source_index * (size_t)xy_stride;
        color_offset = (size_t)source_index * (size_t)color_stride;
        if (texture) {
            uv_offset = (size_t)source_index * (size_t)uv_stride;
        }
        SDL_memcpy(position, (const Uint8 *)xy + xy_offset, sizeof(position));
        SDL_memcpy(&vertex_color, (const Uint8 *)color + color_offset, sizeof(vertex_color));

        /* Queue-time scaling: incoming coordinates are logical, and the pixel
         * viewport origin is added later at emission. */
        vertices[i].x = position[0] * scale_x;
        vertices[i].y = position[1] * scale_y;
        if (!DC_PVR_FloatIsFinite(position[0]) ||
            !DC_PVR_FloatIsFinite(position[1]) ||
            !DC_PVR_FloatIsFinite(vertices[i].x) ||
            !DC_PVR_FloatIsFinite(vertices[i].y)) {
            return SDL_SetError("Dreamcast PVR geometry has a non-finite position");
        }
        vertices[i].color = DC_PVR_PackColor(&vertex_color, cmd->data.draw.color_scale);
        vertices[i].u = 0.0f;
        vertices[i].v = 0.0f;

        if (texturedata) {
            float source_uv[2];
            float u_max;
            float v_max;

            if (texturedata->logical_w <= 0 || texturedata->logical_h <= 0 ||
                texturedata->backing_w <= 0 || texturedata->backing_h <= 0) {
                return SDL_SetError("Dreamcast PVR geometry has invalid texture dimensions");
            }
            SDL_memcpy(source_uv, (const Uint8 *)uv + uv_offset, sizeof(source_uv));
#if SDL_VIDEO_RENDER_DREAMCAST_PVR_TARGETS
            if (texturedata->access == SDL_TEXTUREACCESS_TARGET) {
                if (!texturedata->current_valid) {
                    return SDL_SetError("Dreamcast PVR render target has no completed contents");
                }
                u_max = (texturedata->logical_w <= 1) ? 0.0f :
                        (float)(texturedata->logical_w - 1) / (float)texturedata->backing_w;
                v_max = (texturedata->logical_h <= 1) ? 0.0f :
                        (float)(texturedata->logical_h - 1) / (float)texturedata->backing_h;
            } else
#endif
            {
                u_max = (float)texturedata->logical_w / (float)texturedata->backing_w;
                v_max = (float)texturedata->logical_h / (float)texturedata->backing_h;
            }
            vertices[i].u = source_uv[0] * u_max;
            vertices[i].v = source_uv[1] * v_max;
            if (!DC_PVR_FloatIsFinite(source_uv[0]) ||
                !DC_PVR_FloatIsFinite(source_uv[1]) ||
                !DC_PVR_FloatIsFinite(vertices[i].u) ||
                !DC_PVR_FloatIsFinite(vertices[i].v)) {
                return SDL_SetError("Dreamcast PVR geometry has a non-finite texture coordinate");
            }
        }
    }
    return true;
}

/* SDL3 documents that SDL_RenderClear() clears the entire rendering target and
 * ignores the viewport and the clip rectangle, so the clear quad is the full
 * output and uses no coordinate conversion. */
bool DC_PVR_EmitClear(SDL_Renderer *renderer, const SDL_RenderCommand *cmd)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;
    DC_PVR_Vertex vertices[4];
    const Uint32 packed = DC_PVR_PackColor(&cmd->data.color.color,
                                           cmd->data.color.color_scale);

    SDL_zeroa(vertices);
    for (int i = 0; i < 4; ++i) {
        vertices[i].color = packed;
    }
    vertices[0].x = 0.0f;
    vertices[0].y = 0.0f;
    vertices[1].x = (float)data->render_w;
    vertices[1].y = 0.0f;
    vertices[2].x = 0.0f;
    vertices[2].y = (float)data->render_h;
    vertices[3].x = (float)data->render_w;
    vertices[3].y = (float)data->render_h;

    if (!DC_PVR_EmitHeader(renderer, NULL, SDL_BLENDMODE_NONE, SDL_SCALEMODE_NEAREST,
                           data->active_list_type)) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!DC_PVR_EmitVertex(&vertices[i], (i == 3) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX)) {
            return false;
        }
    }
    return true;
}

static bool DC_PVR_QuadIntersectsBounds(const DC_PVR_Vertex quad[4],
                                        const DC_PVR_GeometryBounds *bounds)
{
    return (quad[0].x < (float)bounds->right && quad[0].y < (float)bounds->bottom &&
            quad[2].x > (float)bounds->left && quad[2].y > (float)bounds->top);
}

static bool DC_PVR_EmitClippedQuad(const DC_PVR_Vertex quad[4],
                                   const DC_PVR_GeometryBounds *bounds)
{
    DC_PVR_Vertex triangle[3];

    triangle[0] = quad[0];
    triangle[1] = quad[1];
    triangle[2] = quad[2];
    if (!DC_PVR_EmitClippedTriangle(triangle, bounds)) {
        return false;
    }
    triangle[1] = quad[2];
    triangle[2] = quad[3];
    return DC_PVR_EmitClippedTriangle(triangle, bounds);
}

bool DC_PVR_RunGeometryCommand(SDL_Renderer *renderer,
                               const SDL_RenderCommand *cmd, const void *vertices)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->internal;

    if (cmd->command == SDL_RENDERCMD_GEOMETRY) {
        const DC_PVR_Vertex *queued = (const DC_PVR_Vertex *)((const Uint8 *)vertices + cmd->data.draw.first);
        DC_PVR_GeometryBounds bounds;

        if (DC_PVR_CalcEffectiveGeometryBounds(data, &bounds) == DC_PVR_GEOMETRY_BOUNDS_EMPTY) {
            return true;
        }
        if ((cmd->data.draw.count % 3) != 0) {
            return SDL_SetError("Dreamcast PVR geometry is not an independent triangle list");
        }
        if (!DC_PVR_EmitHeader(renderer, cmd->data.draw.texture, cmd->data.draw.blend,
                               cmd->data.draw.texture_scale_mode, data->active_list_type)) {
            return false;
        }
        for (size_t i = 0; i < cmd->data.draw.count; i += 3) {
            DC_PVR_Vertex triangle[3];

            for (int vertex_index = 0; vertex_index < 3; ++vertex_index) {
                triangle[vertex_index] = queued[i + vertex_index];
                DC_PVR_TranslateToFramebuffer(data, &triangle[vertex_index].x,
                                              &triangle[vertex_index].y);
            }
            if (!DC_PVR_EmitClippedTriangle(triangle, &bounds)) {
                return false;
            }
        }
        return true;
    }

    if (cmd->command == SDL_RENDERCMD_DRAW_POINTS ||
        cmd->command == SDL_RENDERCMD_FILL_RECTS) {
        const DC_PVR_Vertex *queued = (const DC_PVR_Vertex *)((const Uint8 *)vertices + cmd->data.draw.first);
        DC_PVR_GeometryBounds bounds;
        bool header_emitted = false;

        if (DC_PVR_CalcEffectiveGeometryBounds(data, &bounds) == DC_PVR_GEOMETRY_BOUNDS_EMPTY) {
            return true;
        }
        for (size_t i = 0; i < cmd->data.draw.count; ++i) {
            const float width = (cmd->command == SDL_RENDERCMD_DRAW_POINTS) ? 1.0f : queued[i].u;
            const float height = (cmd->command == SDL_RENDERCMD_DRAW_POINTS) ? 1.0f : queued[i].v;
            DC_PVR_Vertex quad[4];
            float x = queued[i].x;
            float y = queued[i].y;

            if (!DC_PVR_FloatIsFinite(x) || !DC_PVR_FloatIsFinite(y) ||
                !DC_PVR_FloatIsFinite(width) ||
                !DC_PVR_FloatIsFinite(height) ||
                width <= 0.0f || height <= 0.0f) {
                continue;
            }
            DC_PVR_TranslateToFramebuffer(data, &x, &y);
            if (!DC_PVR_FloatIsFinite(x) || !DC_PVR_FloatIsFinite(y) ||
                !DC_PVR_FloatIsFinite(x + width) ||
                !DC_PVR_FloatIsFinite(y + height)) {
                continue;
            }
            for (int vertex_index = 0; vertex_index < 4; ++vertex_index) {
                quad[vertex_index] = queued[i];
                quad[vertex_index].u = 0.0f;
                quad[vertex_index].v = 0.0f;
            }
            quad[0].x = x;
            quad[0].y = y;
            quad[1].x = x + width;
            quad[1].y = y;
            quad[2].x = x + width;
            quad[2].y = y + height;
            quad[3].x = x;
            quad[3].y = y + height;
            if (!DC_PVR_QuadIntersectsBounds(quad, &bounds)) {
                continue;
            }
            if (!header_emitted) {
                if (!DC_PVR_EmitHeader(renderer, NULL, cmd->data.draw.blend,
                                       SDL_SCALEMODE_NEAREST, data->active_list_type)) {
                    return false;
                }
                header_emitted = true;
            }
            if (!DC_PVR_EmitClippedQuad(quad, &bounds)) {
                return false;
            }
        }
        return true;
    }

    return SDL_SetError("Dreamcast PVR geometry helper received an unsupported command");
}

#endif /* SDL_VIDEO_RENDER_DREAMCAST_PVR */
