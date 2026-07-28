# Dreamcast Native PVR Renderer: Point and Line Rendering Research

**Status:** Research, implementation, Flycast validation, and focused physical Dreamcast validation complete.
**Date:** 2026-07-28 (original), 2026-07-28 (corrected review)
**Scope:** SDL point/line semantics, PVR/KOS behavior, representation options, clipping interaction, queue architecture, resource bounds, validation design.

---

## 1. Source-Confirmed SDL Behavior

### 1.1 Point Semantics

**Source:** `src/render/SDL_render.c` lines 2737-2886, `src/render/software/SDL_drawpoint.c`

- `SDL_RenderDrawPoint(x, y)` converts to `SDL_FPoint{x, y}` and calls `SDL_RenderDrawPointsF(&fpoint, 1)`
- `SDL_RenderDrawPointsF(points, count)` queues raw `SDL_FPoint` array via `QueueCmdDrawPoints`
- **Scale behavior:** If `renderer->scale.x != 1.0f || renderer->scale.y != 1.0f`, points are expanded to 1×scale rects via `RenderDrawPointsWithRectsF`. Otherwise, queued as integer-position FPoints.
- **Coordinate convention:** `(x, y)` is the **top-left corner** of the pixel area `[x, x+1) × [y, y+1)`. Confirmed by:
  - Software `SDL_DrawPoint` clip check: `x < clip.x || y < clip.y || x >= clip.x + w || y >= clip.y + h` → skip (half-open bounds)
  - Full-screen clear in current renderer: `(0,0), (output_w,0), (0,output_h), (output_w,output_h)` — `(0,0)` is top-left, `(output_w,output_h)` is bottom-right corner
  - SDL integer rectangles are half-open: left/top included, right/bottom excluded
- **Viewport:** Applied in software RunCommandQueue by adding viewport offset to each point.
- **Clipping:** Half-open bounds. Left/top included, right/bottom excluded.
- **Duplicate points:** Each point is an independent draw. No deduplication.
- **Alpha blending:** Each point writes independently. Overlapping points accumulate via blend mode.

### 1.2 Line Semantics and Method Selection

**Source:** `src/render/SDL_render.c` lines 925-943, 1155, 1220, 2887-3263

#### Line Method Selection

```c
static SDL_RenderLineMethod SDL_GetRenderLineMethod(void)
{
    const char *hint = SDL_GetHint(SDL_HINT_RENDER_LINE_METHOD);
    int method = 0;
    if (hint) method = SDL_atoi(hint);
    switch (method) {
    case 1: return SDL_RENDERLINEMETHOD_POINTS;
    case 2: return SDL_RENDERLINEMETHOD_LINES;
    case 3: return SDL_RENDERLINEMETHOD_GEOMETRY;
    default: return SDL_RENDERLINEMETHOD_POINTS;  // ← DEFAULT
    }
}
```

- **Default:** `SDL_RENDERLINEMETHOD_POINTS` (method=0 → default case)
- **Software renderer override:** Line 1220 forces `SDL_RENDERLINEMETHOD_LINES` for software backend only
- **Our PVR renderer:** Uses default POINTS (not overridden)
- **Environment hint:** `SDL_HINT_RENDER_LINE_METHOD=1|2|3` can force alternative method
- **Installing `QueueDrawLines`:** Does NOT change line_method selection. Only affects behavior if method is explicitly set to LINES.

#### Call Path: `SDL_RenderDrawLine(s)` → Dreamcast Backend (Default POINTS)

```
SDL_RenderDrawLine(x1,y1,x2,y2)
  → SDL_RenderDrawLinesF(points, 2)
    → RenderDrawLinesWithRectsF(renderer, points, 2)  // line_method == POINTS

For each segment (i, i+1):
  A non-final same_x && same_y segment: skip before primitive generation
  Else if same_x (vertical):
    QueueCmdFillRects(rect x=points[i].x, y=min(y1,y2), w=scale_x, h=|y2-y1|+draw_last)
  Else if same_y (horizontal):
    QueueCmdFillRects(rect x=min(x1,x2), y=points[i].y, w=|x2-x1|+draw_last, h=scale_y)
  Else (diagonal):
    RenderDrawLineBresenham(x1,y1,x2,y2, draw_last)
      → generates integer FPoint array (numpixels or numpixels-1 if !draw_last)
      → QueueCmdDrawPoints(fpoints, count)

draw_last logic:
  - Last segment (i == count-2): draw_last = SDL_TRUE unless closed polyline
  - Non-last segments: draw_last = SDL_FALSE
  - Closed polyline (points[0] == points[count-1]): last segment draw_last = SDL_FALSE
```

#### Zero-length segments (current-source correction)

The behavior is conditional, not a blanket skip. In current
`src/render/SDL_render.c`, `RenderDrawLinesWithRectsF` computes `same_x` and
`same_y` at lines 3016-3018. The skip at lines 3024-3027 is inside the
non-final-segment branch. The final segment instead evaluates endpoint
ownership at lines 3020-3023 and then continues to the axis-aligned rectangle
branches at lines 3029-3052.

- An independent `SDL_RenderDrawLine(x, y, x, y)` has one final segment.
  `drew_line` is false, so `draw_last` becomes true. The vertical branch queues
  width `scale_x` and height `(maxY - minY + draw_last) * scale_y`; at unit
  scale, equal endpoints therefore queue a 1x1 rectangle and render one pixel.
- An interior repeated point in `SDL_RenderDrawLines` forms a non-final
  zero-length segment and is skipped at lines 3024-3027.
- A final repeated point in a non-closed connected strip can own the final
  endpoint. When the final point differs from the strip's first point,
  `draw_last` becomes true and the zero-length segment queues the final endpoint
  as a 1x1 rectangle at unit scale.
- In a closed strip, the final point equals the first point. Once an earlier
  segment set `drew_line`, the loop-closing test leaves `draw_last` false. A
  zero-length closing segment therefore queues an empty axis-aligned rectangle,
  which the backend must preserve as empty. It must not be enlarged.

**Key consequence:** Under default POINTS method, **`QueueDrawLines` is never
called**. SDL routes diagonals through Bresenham points and axis-aligned
segments through rect fills. SDL applies `draw_last` endpoint ownership in
both paths before the backend receives either command type.

#### QueueFillRects Fallback

**Source:** `src/render/SDL_render.c` lines 536-597

```c
const int use_rendergeometry = (!renderer->QueueFillRects);
```

- If `QueueFillRects` is NULL, SDL converts rects to geometry:
  - 4 vertices per rect: `(minx,miny), (maxx,miny), (maxx,maxy), (minx,maxy)`
  - 6 indices per rect: `[0,1,2, 0,2,3]` (default rect_index_order)
  - Calls `QueueGeometry`
- **Our renderer:** Has `QueueGeometry`, no `QueueFillRects` → rects go through geometry fallback
- **Assertion:** `SDL_assert(renderer->QueueFillRects != NULL || renderer->QueueGeometry != NULL)` (line 920)

### 1.3 Bresenham Endpoint Suppression

**Source:** `src/render/SDL_render.c` lines 2907-2995

```c
static int RenderDrawLineBresenham(renderer, x1, y1, x2, y2, draw_last)
{
    numpixels = max(deltax, deltay) + 1;
    if (!draw_last) --numpixels;  // ← suppress endpoint
    // generate integer points
    QueueCmdDrawPoints(fpoints, numpixels);
}
```

- `draw_last=SDL_FALSE`: generates `numpixels-1` points (excludes endpoint)
- `draw_last=SDL_TRUE`: generates full `numpixels` points
- **Shared endpoints between consecutive nonzero segments:** `draw_last`
  assigns endpoint ownership so interior shared endpoints are written once.
- **Closed polylines:** Closing point not double-drawn.

### 1.4 Key Behavioral Facts

- **Independent `SDL_RenderDrawLine`:** Both endpoints drawn (count=2, both points in Bresenham output with `draw_last=SDL_TRUE`)
- **Connected strips (`SDL_RenderDrawLines`):** Shared endpoints drawn once (Bresenham excludes for non-last segments)
- **Closed shapes:** Closing point not double-drawn
- **Zero-length segments:** Conditional as documented above: independent and
  endpoint-owning final repeats render one pixel at unit scale; interior
  repeats are skipped; zero-length closing segments remain empty.
- **Color:** Constant per command (set via `SDL_SetRenderDrawColor`)
- **Queue representation:**
  - Points: `SDL_RENDERCMD_DRAW_POINTS` with `SDL_FPoint` array
  - Axis-aligned lines: `SDL_RENDERCMD_FILL_RECTS` (or `SDL_RENDERCMD_GEOMETRY` via fallback)
  - Diagonal lines: `SDL_RENDERCMD_DRAW_POINTS` with Bresenham integer array

---

## 2. Source-Confirmed PVR/KallistiOS Behavior

### 2.1 Vertex Format and Submission

**Source:** `$KOS_BASE/kernel/arch/dreamcast/include/dc/pvr.h` lines 415-438, current renderer lines 450-456

```c
typedef struct pvr_vertex {
    uint32_t flags;        // PVR_CMD_VERTEX (0xe0000000) or PVR_CMD_VERTEX_EOL (0xf0000000)
    float x, y, z;
    uint32_t argb;         // Packed 0xAARRGGBB
    uint32_t oargb;
} pvr_vertex_t;
// sizeof(pvr_vertex_t) == 32 bytes, aligned to 32
```

**Current renderer emission pattern** (lines 496-499):

```c
// Each triangle emits 3 vertex commands:
if (DC_PVR_EmitVertex(a, PVR_CMD_VERTEX) < 0 ||
    DC_PVR_EmitVertex(b, PVR_CMD_VERTEX) < 0 ||
    DC_PVR_EmitVertex(c, PVR_CMD_VERTEX_EOL) < 0) {
    return -1;
}
```

- `PVR_CMD_VERTEX_EOL` is a flag on the 3rd vertex, not an additional command
- **Each independent triangle submits exactly 3 `pvr_vertex_t` commands = 96 bytes**

### 2.2 Quad Emission (Two Independent Triangles)

A quad split into two independent triangles emits **6 vertex commands = 192 bytes**:

```
Triangle 1: v0 (VERTEX), v1 (VERTEX), v2 (VERTEX_EOL)  → 3 commands, 96 bytes
Triangle 2: v0 (VERTEX), v2 (VERTEX), v3 (VERTEX_EOL)  → 3 commands, 96 bytes
Total: 6 commands, 192 bytes
```

**Not a triangle strip.** A strip would require `PVR_CMD_VERTEX` for v0, v1, v2 and `PVR_CMD_VERTEX_EOL` for v3, submitting 4 commands. But our existing clipping path uses independent triangles, so quads must follow the same pattern.

### 2.3 Line Drawing Example

**Source:** `$KOS_BASE/examples/dreamcast/pvr/pvrline/pvrline.c`

- Lines drawn as 4-vertex quads (2 independent triangles, not strip)
- Perpendicular normal: `nx = -dy * inv_mag`, `ny = dx * inv_mag` where `inv_mag = frsqrt(dx²+dy²) * (width * 0.5f)`
- Vertices: `v1±normal`, `v2±normal`
- Uses `pvr_list_prim()` for direct submission
- Supports both `PVR_LIST_OP_POLY` and `PVR_LIST_TR_POLY`

### 2.4 Point Drawing Example

**Source:** `$KOS_BASE/examples/dreamcast/pvr/pvrmark/pvrmark.c`

- Points drawn as 4-vertex quads: `(x-size, y+size), (x+size, y+size), (x+size, y-size), (x-size, y-size)`
- Two triangles forming a square centered on (x,y)
- Size varies per point

### 2.5 Polygon Lists and Header Cache

- `PVR_LIST_TR_POLY`: Translucent list (our renderer, autosort-disabled for painter order)
- `PVR_LIST_OP_POLY`: Opaque list (deferred to Phase 6)
- Header cache key: texture pointer + blend mode + texture_env
- For untextured points/lines: texture=NULL, cache key is blend mode only

---

## 3. Mathematical Conclusions

### 3.1 Point Representation

**Recommendation:** One 1×1 quad per point, split into two independent triangles.

**Vertex layout** (top-left corner at `(x, y)`, covering `[x, x+1) × [y, y+1)`):

```
v0: (x,   y)   → top-left
v1: (x+1, y)   → top-right
v2: (x+1, y+1) → bottom-right
v3: (x,   y+1) → bottom-left
```

**Two independent triangles** (matching existing clipping path):

```
Triangle 1: (v0, v1, v2)  → (x,y), (x+1,y), (x+1,y+1)
Triangle 2: (v0, v2, v3)  → (x,y), (x+1,y+1), (x,y+1)
```

**Winding verification:**
- Triangle 1: `(1,0) × (1,1)` cross = 1*1 - 0*1 = 1 (positive)
- Triangle 2: `(1,1) × (0,1)` cross = 1*1 - 1*0 = 1 (positive)
- Both positive winding (consistent with PVR_CULLING_NONE)

**Alternative rejected:** Triangle strip `(v0, v1, v2, v3)` with EOL on v3. Would submit 4 commands instead of 6, but breaks consistency with existing clipping path and requires separate emission logic.

### 3.2 Line Representation

**Two viable options:**

#### Option A: Per-segment quads via perpendicular normal

**Rationale:**
- Matches KOS `pvrline` example pattern
- Each segment independent (preserves SDL command order)
- Works with existing clipping (same path as points/rects)
- Diagonal thickness consistent (1 pixel perpendicular to segment)

**Vertex layout for segment `(x1,y1)→(x2,y2)`:**

```
dx = x2 - x1
dy = y2 - y1
magnitude = sqrtf(dx*dx + dy*dy)

if magnitude == 0: emit as point quad (zero-length)

inv_mag = 1.0f / magnitude
scale = inv_mag * 0.5f  // half-pixel width
nx = -dy * scale
ny = dx * scale

v0: (x1 + nx, y1 + ny)  → left of direction
v1: (x2 + nx, y2 + ny)  → left of direction
v2: (x2 - nx, y2 - ny)  → right of direction
v3: (x1 - nx, y1 - ny)  → right of direction
```

**Two independent triangles:**

```
Triangle 1: (v0, v1, v2)
Triangle 2: (v0, v2, v3)
```

**Endpoint coverage verification:**

- **Horizontal** `(0,0)→(10,0)`: dx=10, dy=0, nx=0, ny=0.5
  - v0=(0, 0.5), v1=(10, 0.5), v2=(10, -0.5), v3=(0, -0.5)
  - Covers y ∈ [-0.5, 0.5], x ∈ [0, 10]. Includes pixels (0,0) and (10,0). ✓

- **Vertical** `(0,0)→(0,10)`: dx=0, dy=10, nx=-0.5, ny=0
  - v0=(-0.5, 0), v1=(-0.5, 10), v2=(0.5, 10), v3=(0.5, 0)
  - Covers x ∈ [-0.5, 0.5], y ∈ [0, 10]. Includes pixels (0,0) and (0,10). ✓

- **45° diagonal** `(0,0)→(10,10)`: dx=10, dy=10, magnitude≈14.14, scale≈0.0354
  - nx≈-0.354, ny≈0.354
  - v0≈(-0.354, 0.354), v1≈(9.646, 10.354), v2≈(10.354, 9.646), v3≈(0.354, -0.354)
  - Covers diagonal. Includes pixels (0,0) and (10,10). ✓

- **One-pixel segment** `(0,0)→(0,0)`: magnitude=0 → emit as point quad. ✓

- **Reversed order** `(10,0)→(0,0)`: dx=-10, dy=0, nx=0, ny=-0.5
  - v0=(10, -0.5), v1=(0, -0.5), v2=(0, 0.5), v3=(10, 0.5)
  - Same coverage as forward order. ✓

**Issue with Option A for connected strips:** If we implement `QueueDrawLines` for LINES method, SDL queues all endpoints (no suppression). A perpendicular-quad per segment would double-draw shared endpoints.

**Mitigation:** Under default POINTS method, SDL suppresses shared endpoints via Bresenham. We never see double-endpoints. For LINES method (if user sets hint=2), we'd need endpoint suppression logic. **Recommendation: implement for default POINTS method only, defer LINES method to later.**

#### Option B: Match SDL POINTS method exactly

**Rationale:**
- Axis-aligned segments → rects (already handled by SDL's `QueueCmdFillRects`)
- Diagonal segments → Bresenham points (already handled by SDL's `QueueCmdDrawPoints`)
- No endpoint suppression logic needed in our backend

**Implementation:**
- `QueueDrawPoints`: store FPoint as DC_PVR_Vertex
- `QueueFillRects`: preserve each `SDL_FRect` origin and extent in renderer-owned floating-point storage
- RunCommandQueue: expand points to quads, rects to quads

**Advantage:** Zero endpoint logic. Matches SDL behavior exactly.

**Disadvantage:** Axis-aligned lines go through rect path (same as Option A).

**Decision:** **Option B is the correct implementation.** It leverages SDL's existing Bresenham and endpoint suppression. We only need to render the queued primitives (points and rects).

### 3.3 Diagonal Thickness

**Option B (via Bresenham):** Diagonal lines are rasterized as integer points by SDL. Each point becomes a 1×1 quad. The Bresenham algorithm ensures consistent 1-pixel width perpendicular to the segment direction.

**PVR rasterization:** 1×1 quads rasterize to exactly 1 pixel. No thickness variation.

**Validation required:** Confirm no cracks or gaps at pixel boundaries for shallow angles (e.g., slope 1/10).

---

## 4. Implementation Recommendations

### 4.1 Queue Callbacks

**Required callbacks:**
- `QueueDrawPoints` — queue `SDL_FPoint` array as `DC_PVR_Vertex` with u=v=0, color=packed ARGB
- `QueueFillRects` — queue every `SDL_FRect` without truncating its floating-point origin or extent, and capture the command color

**Why not `QueueDrawLines`?** Under default POINTS method, SDL never calls `QueueDrawLines`. Lines are routed through Bresenham (diagonals → points) and rect fills (axis-aligned). Installing `QueueDrawLines` only helps if user explicitly sets `SDL_HINT_RENDER_LINE_METHOD=2`.

**Implementation:**

```c
static int DC_PVR_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                   const SDL_FPoint *points, int count)
{
    DC_PVR_Vertex *vertices = (DC_PVR_Vertex *)SDL_AllocateRenderVertices(
        renderer, count * sizeof(DC_PVR_Vertex), 0, &cmd->data.draw.first);
    if (!vertices) return -1;

    cmd->data.draw.count = count;
    for (int i = 0; i < count; i++) {
        vertices[i].x = points[i].x;
        vertices[i].y = points[i].y;
        vertices[i].color = DC_PVR_PackColor(&renderer->color);
        vertices[i].u = 0.0f;
        vertices[i].v = 0.0f;
    }
    return 0;
}

Queue the rectangle as floating-point `x`, `y`, `w`, and `h` plus packed
command color. Do not consult later renderer color state. Do not force zero or
negative extents to one: SDL deliberately produces an empty rectangle for a
zero-length closing segment that does not own the final endpoint.
```

**Register in CreateRenderer:**

```c
renderer->QueueDrawPoints = DC_PVR_QueueDrawPoints;
renderer->QueueFillRects = DC_PVR_QueueFillRects;
```

### 4.2 RunCommandQueue Expansion

**New cases in switch:**

```c
case SDL_RENDERCMD_DRAW_POINTS:
{
    const DC_PVR_Vertex *queued = (const DC_PVR_Vertex *)
        ((const Uint8 *)vertices + cmd->data.draw.first);
    DC_PVR_GeometryBounds bounds;
    DC_PVR_GeometryBoundsState bounds_state;
    size_t i;

    bounds_state = DC_PVR_CalcEffectiveGeometryBounds(data, &bounds);
    if (bounds_state == DC_PVR_GEOMETRY_BOUNDS_EMPTY) break;

    for (i = 0; i < cmd->data.draw.count; i++) {
        DC_PVR_Vertex quad[4];
        const DC_PVR_Vertex *p = &queued[i];

        // Apply viewport translation once
        float px = p->x, py = p->y;
        if (data->has_viewport) {
            px += data->view_x;
            py += data->view_y;
        }

        // 1x1 quad: top-left at (px, py)
        quad[0] = (DC_PVR_Vertex){ px,     py,     0, 0, p->color };
        quad[1] = (DC_PVR_Vertex){ px + 1, py,     0, 0, p->color };
        quad[2] = (DC_PVR_Vertex){ px + 1, py + 1, 0, 0, p->color };
        quad[3] = (DC_PVR_Vertex){ px,     py + 1, 0, 0, p->color };

        // Emit the header lazily for the first intersecting quad, then route
        // explicit (v0,v1,v2) and (v0,v2,v3) triangles through clipping.
        if (DC_PVR_EmitClippedQuad(quad, &bounds) < 0) return -1;
    }
    break;
}

case SDL_RENDERCMD_FILL_RECTS:
{
    /* `queued` is renderer-owned floating-point x/y/w/h plus packed color. */
    DC_PVR_GeometryBounds bounds;
    DC_PVR_GeometryBoundsState bounds_state;
    size_t i;

    bounds_state = DC_PVR_CalcEffectiveGeometryBounds(data, &bounds);
    if (bounds_state == DC_PVR_GEOMETRY_BOUNDS_EMPTY) break;

    for (i = 0; i < cmd->data.draw.count; i++) {
        DC_PVR_Vertex quad[4];
        const DC_PVR_Vertex *r = &queued[i]; /* u/v retain w/h */

        // Apply viewport translation once
        float rx = r->x, ry = r->y;
        if (data->has_viewport) {
            rx += data->view_x;
            ry += data->view_y;
        }

        // Rect: (rx, ry) to (rx+w, ry+h)
        quad[0] = (DC_PVR_Vertex){ rx,      ry,      0, 0, r->color };
        quad[1] = (DC_PVR_Vertex){ rx+r->u, ry,      0, 0, r->color };
        quad[2] = (DC_PVR_Vertex){ rx+r->u, ry+r->v, 0, 0, r->color };
        quad[3] = (DC_PVR_Vertex){ rx,      ry+r->v, 0, 0, r->color };

        // Emit the header lazily, then submit (v0,v1,v2) and (v0,v2,v3).
        if (DC_PVR_EmitClippedQuad(quad, &bounds) < 0) return -1;
    }
    break;
}
```

**Color for FILL_RECTS:** Queue creation captures `cmd->data.draw.r/g/b/a`.
The callback packs that color into every renderer-owned rectangle record so
execution never depends on later renderer color state.

```c
SDL_Color color = { cmd->data.draw.r, cmd->data.draw.g,
                    cmd->data.draw.b, cmd->data.draw.a };
queued[i].color = DC_PVR_PackColor(&color);
```

**Empty rect handling:** preserve SDL's floating-point values exactly where
practical. Emission rejects non-finite or non-positive extents. The one-pixel
independent zero-length case already arrives with positive `scale_x` and
`scale_y`; empty closing cases must remain empty.

### 4.3 Viewport Translation

Apply viewport offset **once** in emission path, not in queue callback:

- Queue callbacks store raw input coordinates (immutable)
- Emission path adds `data->view_x` and `data->view_y` to positions
- Consistent with existing geometry emission (lines 770-776)

### 4.4 Header Cache

Points and rects use untextured color headers. Header cache key: blend mode only (no texture).

```c
// DC_PVR_EmitHeader(renderer, NULL, blend)
// texture=NULL → pvr_poly_cxt_col(&context, PVR_LIST_TR_POLY)
// texture_env: default REPLACE (set by pvr_poly_cxt_col)
// Cache key: blend mode
```

---

## 5. Emulator-Only Assumptions

**Assumption 1:** PVR rasterization of 1×1 quads produces exactly 1 pixel.
**Validation:** Flycast + DC hardware test with isolated points.

**Assumption 2:** Bresenham integer points, expanded to 1×1 quads, produce consistent 1-pixel-wide diagonals.
**Validation:** Compare against SDL software renderer output for shallow angles.

**Assumption 3:** No PVR-specific coordinate offset required (PVR uses same pixel grid as SDL).
**Validation:** Confirmed by existing clipping research (no epsilon needed).

**Assumption 4:** Shared-endpoint behavior is correct because SDL suppresses endpoints under POINTS method.
**Validation:** Visual comparison with SDL software renderer for connected strips.

---

## 6. Physical-Hardware Validation Requirements

### 6.1 Endpoint Inclusion

- `SDL_RenderDrawPoint(x, y)` draws exactly one pixel at `(x, y)` (top-left of `[x,x+1)×[y,y+1)`)
- `SDL_RenderDrawLine(x1, y1, x2, y2)` draws both endpoints
- Points on clip boundaries: left/top included, right/bottom excluded

### 6.2 Connected Strip Endpoints

- `SDL_RenderDrawLines` with 3+ points: interior shared endpoints drawn once
- Closed strips: closing point not double-drawn
- **Visual check:** No brightness accumulation at shared endpoints with `SDL_BLENDMODE_BLEND`

### 6.3 Diagonal Thickness and Continuity

- Diagonal lines should be 1 pixel wide (perpendicular to segment direction)
- No cracks or gaps at pixel boundaries
- No thickness variation between horizontal/vertical/diagonal
- **Specific tests:** slope 1/10, slope 10/1, slope 1/1, slope -1/1

### 6.4 Right/Bottom Leakage

- Lines ending at half-open clip boundaries should not leak to next pixel
- Use existing clipping path (no epsilon)
- **Test:** bright guard region exposing right/bottom leakage

### 6.5 One-Pixel Clip Behavior

- `SDL_RenderDrawPoint(x, y)` with clip `(x, y, 1, 1)` should draw 1 pixel
- `SDL_RenderDrawPoint(x, y)` with clip `(x+1, y, 1, 1)` should draw nothing (point outside)
- `SDL_RenderDrawLine(x, y, x, y)` with clip `(x, y, 1, 1)` draws exactly one pixel at unit scale

### 6.6 Crack/Overlap in Generated Triangles

- Quad split into two triangles should show no visible seam
- Use deterministic fan anchor (existing clipping path provides this)
- **Test:** contrasting gradient pattern across quad boundary

### 6.7 Reversed Endpoint Ordering

- `SDL_RenderDrawLine(x1, y1, x2, y2)` and `SDL_RenderDrawLine(x2, y2, x1, y1)` should produce identical output
- For independent lines: both endpoints included regardless of order
- For connected strips: SDL handles ordering via Bresenham direction

### 6.8 Agreement with Reference Backend

- Compare against SDL software renderer output (Flycast)
- Visual match required for all test cases
- **Test:** side-by-side comparison in test output

---

## 7. Resource Bounds

### 7.1 Queued CPU Memory

- **Per point:** one `DC_PVR_Vertex` queue record (five 32-bit fields on this toolchain)
- **Per rect:** one floating-point x/y/w/h plus packed-color queue record; the current implementation reuses `DC_PVR_Vertex`

### 7.2 PVR Vertex-Command Traffic

**Each independent triangle submits 3 `pvr_vertex_t` commands = 96 bytes.**

- **Per point:** 2 triangles × 3 commands = **6 commands, 192 bytes**
- **Per rect:** 2 triangles × 3 commands = **6 commands, 192 bytes**
- **Connected strip of N points (diagonal):**
  - SDL Bresenham generates ~N points (endpoint suppression reduces by ~N-2)
  - Our backend: ~2 points × 6 commands = **12 commands, 384 bytes** (for a 2-point segment)
  - For N-point strip: ~N points × 6 commands = **6N commands, 192N bytes**

### 7.3 Polygon-Header Traffic

- **Per point:** 1 header (if blend changes)
- **Per rect:** 1 header (if blend changes)
- **Header caching:** Same blend+no-texture headers deduplicated

### 7.4 Worst-Case Examples

- **1,000 points:** 20,000 bytes queued with the current `DC_PVR_Vertex`, 6,000 vertex commands (192,000 bytes PVR traffic), and cached headers only when state requires them
- **1,000 independent line segments (diagonal):** ~2,000 Bresenham points queued, 12,000 vertex commands (384 KB PVR traffic)
- **1,000-point connected strip (diagonal):** ~1,000 Bresenham points queued, 6,000 vertex commands (192 KB PVR traffic)
- **Axis-aligned lines:** Same as rects (192 bytes per 1-pixel-wide rect)

### 7.5 TA/OPB Capacity

- **Unmeasured.** Must benchmark on Flycast + DC hardware with stress test (10,000+ points/lines).
- Command bytes ≠ OPB memory. PVR tile bins, ISP/TSP, and frame buffer usage require hardware profiling.

---

## 8. Validation Design

### 8.1 Test Structure

**Location:** `test/dreamcast/test/pvr_points_lines/`

**Pages:**
1. **Points:** isolated, adjacent, duplicate blended, clip boundaries, one-pixel clips
2. **Lines:** horizontal, vertical, diagonal, independent zero-length,
   interior repeated, final repeated, connected strips, and closed outlines
3. **Clipping:** each boundary, multiple boundaries, nonzero viewport, ordered clears

### 8.2 Automated Assertions

- Return codes from all SDL API calls
- Test completion (no crashes)
- Console PASS/FAIL lines

### 8.3 Visual Inspection Required

- One-pixel point coverage
- Line thickness consistency
- Diagonal continuity
- Shared endpoint brightness (should not accumulate)
- Zero-length/repeated-point results agree with the SDL software renderer:
  independent and endpoint-owning final repeats draw once, interior repeats
  are skipped, and empty closing repeats are not enlarged
- Right/bottom leakage
- Triangle seam visibility
- Agreement with Flycast reference

### 8.4 Flycast Workflow

```bash
cd test/dreamcast/test/pvr_points_lines && make -f Makefile.dc
flatpak run org.flycast.Flycast "$PWD/pvr_points_lines.elf"
# Capture during hold interval (12,000 ms)
spectacle -b -n -a -o /tmp/pvr-points-lines-flycast.png
```

### 8.5 Physical Dreamcast Checklist

- [ ] Point at (0,0) visible, point at (639,479) visible
- [ ] One-pixel clip `(320,240,1,1)` draws exactly 1 pixel
- [ ] Horizontal line y=240 from x=0 to x=639 continuous, no gaps
- [ ] Vertical line x=320 from y=0 to y=479 continuous
- [ ] 45° diagonal from (0,0) to (639,639) 1 pixel wide
- [ ] Shallow diagonal (slope 1/10) no thickness variation
- [ ] Connected strip with shared endpoints no brightness accumulation
- [ ] Independent zero-length line draws one pixel
- [ ] Interior repeated point, final repeated point, and zero-length closed-strip closing segment match the software renderer in NONE and BLEND modes
- [ ] Closed outline (square) no double-drawn corners
- [ ] Clip rect `(100,100,200,200)` excludes lines outside
- [ ] Right/bottom boundary no leakage (bright guard probe)
- [ ] `SDL_BLENDMODE_BLEND` overlapping lines accumulate correctly
- [ ] Nonzero viewport `(320,0,320,480)` translates coordinates correctly
- [ ] Reversed line endpoints identical output
- [ ] Match Flycast reference image (visual inspection)

---

## 9. Implementation Recommendation

**Implemented increment:** `QueueDrawPoints` and `QueueFillRects` expand points
to 1×1 quads and positive rectangles to filled quads in `RunCommandQueue`
through the existing clipping path.

**Rationale:**
- Leverages SDL's existing Bresenham and endpoint suppression (default POINTS method)
- Smallest safe change (add 2 callbacks, 2 emission loops, 2 switch cases)
- Matches PVR patterns (independent triangles, not strips)
- Works with existing clipping (no new clip logic)
- Preserves painter order (independent quads)
- No endpoint suppression logic needed in our backend

**Validation gate:** the focused Flycast and physical Dreamcast checklists pass
at 640x480 VGA. Wider mode and stress coverage remains Phase 7 hardening.

**Do not:**
- Implement `QueueDrawLines` (not called under default POINTS method)
- Add endpoint suppression logic (SDL handles it)
- Add OPAQUE list routing (defer to Phase 6 optimization)
- Change header cache logic (reuse existing)
- Modify existing geometry/clipping path

**Estimated complexity:** ~100 lines of new code in `SDL_render_dreamcast_pvr.c`.

**Not recommended:**
- Perpendicular-normal line quads (Option A) — requires endpoint suppression logic for LINES method, adds complexity without benefit under default POINTS
- Triangle strip emission — breaks consistency with existing clipping path
- `kos_frsqrt` for initial implementation — use `sqrtf` for deterministic correctness

---

## 10. Remaining Hardening Risks

1. **TA/OPB capacity:** Unmeasured for point/line stress. Must benchmark before production use.
2. **Other video modes:** The focused gate covers 640x480 VGA; 320x240,
   PAL/NTSC timing variants, and broader viewport-offset ranges remain Phase 7.
3. **Header batching efficiency:** Points and rects share untextured headers and
   render correctly; performance under frequent state changes remains unmeasured.

---

## 11. Deliverables

- ✅ Research document: `src/render/dreamcast/PVR_POINTS_LINES_RESEARCH.md` (corrected)
- ✅ `QueueDrawPoints` and `QueueFillRects`, with no `QueueDrawLines`
- ✅ `SDL_RENDERCMD_DRAW_POINTS` and `SDL_RENDERCMD_FILL_RECTS` execution
- ✅ Test: `test/dreamcast/test/pvr_points_lines/`
- ✅ Strict-warning builds, explicit-local-archive map, and Flycast validation
- ✅ Physical Dreamcast validation at 640x480 VGA

### Implementation and validation (2026-07-28)

- Both callbacks allocate renderer-owned `DC_PVR_Vertex` records. Points store
  exact `SDL_FPoint` coordinates, deterministic zero UVs, and packed command
  color. Rectangles store floating-point x/y/w/h (w/h in u/v) and packed
  command color. Neither callback translates, clips, or submits PVR commands.
- `RunCommandQueue` handles both commands by applying the viewport origin once,
  constructing half-open quads, and emitting explicit `(v0,v1,v2)` and
  `(v0,v2,v3)` triangles through the existing CPU clipping function. Empty and
  non-finite rectangles are rejected; no extent is enlarged. Headers are
  emitted lazily for the first drawable quad and use the captured blend mode.
- Each unclipped point or rectangle emits two independent triangles: six
  32-byte `pvr_vertex_t` commands, or 192 bytes of vertex-command traffic.
- The opt-in static SDL build passes. Lifecycle, viewport,
  vertex-color/blend, clipping, and points/lines tests compile and link with
  `-Wall -Wextra -Werror` against the explicit local archive. The points/lines
  map resolves both `SDL_render.c.obj` and
  `SDL_render_dreamcast_pvr.c.obj` from that archive. A separate opt-out build
  contains neither the renderer object nor `DC_RenderDriver`.
- All five tests pass their runtime/API checks in Flycast. Direct inspection of
  `/tmp/pvr-points-lines-page1.png`, `page2.png`, and `page3.png` confirms
  one-pixel corner/isolated/adjacent points; expected NONE and BLEND duplicate
  results; included left/top and excluded right/bottom clip edges; continuous
  horizontal, vertical, 45-degree, shallow, steep, and reversed lines; one-time
  endpoint ownership for connected, repeated, and closed patterns; correct
  viewport translation and clip changes; ordered clears; exact magenta
  right/bottom guards with no outside leakage; and no visible two-triangle
  cracks.
- Physical Dreamcast validation on 2026-07-27 passes at 640x480 VGA. The three
  photographed pages match the Flycast reference and the test reports
  `Program returned 0`.
  Points at both framebuffer corners and the one-pixel clip are visible; clip
  left/top inclusion and right/bottom exclusion are intact; `NONE` and `BLEND`
  duplicate-point results have the expected relative intensities; all axis,
  45-degree, shallow, steep, and reversed lines are continuous; connected,
  repeated-point, and closed patterns show no brighter shared endpoints; the
  independent zero-length markers are present; viewport translation, ordered
  clears, and clip changes match the reference; cyan/magenta boundary guards
  do not leak; and no two-triangle cracks are visible. This completes the
  focused points/default-lines increment. Broader modes and TA/OPB stress stay
  in Phase 7.

---

**End of research.** Focused implementation and validation complete.
