# Dreamcast Native PVR Renderer: Pixel-Exact Clipping Research

**Status:** Research corrected and complete. The first full-CPU
Sutherland–Hodgman increment passes Flycast and a physical Dreamcast at
640x480 VGA. PVR user clipping remains only a coarse 32-by-32-tile
optimization candidate.

**Date:** 2026-07-27
**Scope:** SDL clip semantics, PVR user clipping, triangle/rectangle bounds,
attribute interpolation, PVR submission cost, implementation conformance, and
remaining validation risks.

This document deliberately separates source-confirmed facts, mathematical
bounds, and implementation proposals. Implementation and validation status is
recorded separately from those design conclusions.

## Corrected summary

- SDL clip rectangles are relative to the viewport. After the renderer's scale
  conversion, translate the queued rectangle by the queued viewport origin and
  intersect it with the viewport and output bounds.
- SDL integer rectangles are half-open: left and top are included; right
  `x + w` and bottom `y + h` are excluded. A rectangle with `w <= 0` or
  `h <= 0` is empty.
- PVR user clipping masks rasterization by an inclusive rectangle of 32-by-32
  TA tiles. It is not pixel-exact. The reviewed KOS and GLdc sources do not
  justify describing it as rejecting every primitive that crosses the box.
- A convex triangle clipped against the four half-planes of a convex rectangle
  has at most **7 vertices**. A fan therefore produces at most **5 triangles**.
- Emitting those triangles independently submits **15 `pvr_vertex_t`
  commands**. `PVR_CMD_VERTEX_EOL` is a flag in the third vertex command, not
  an additional command.
- KallistiOS defines `sizeof(pvr_vertex_t) == 32` on this toolchain: each
  submitted generic vertex is one 32-byte TA payload. The worst-case vertex
  submission for 1,000 input triangles is therefore **480,000 bytes
  (468.75 KiB)**, not 192 KB.
- If all 1,000 triangles also force distinct polygon headers, add at most 1,000
  32-byte headers: **512,000 bytes (500 KiB)** plus the list terminator. These
  are host-to-TA submission bytes, not a direct measurement of OPB, tile-bin,
  or ISP/TSP memory consumption.
- Packed `0xAARRGGBB` colors must never be interpolated as one integer. Unpack,
  interpolate, round, and clamp A, R, G, and B independently, then repack.
- Seven vertices in each Sutherland–Hodgman ping-pong buffer are sufficient for
  the stated input invariant: one triangle clipped once against four rectangle
  planes. GLdc currently allocates eight, but that is conservative storage, not
  the mathematical maximum.
- CPU clipping should use no positional epsilon that expands the clip region.
  Force intersections exactly onto the relevant integer-valued clip plane and
  initially reject only non-finite or exactly zero signed-area triangles.
- Physical 640x480 VGA testing confirms Flycast's result: geometry ending at
  the half-open right and bottom boundaries shows no next-pixel leakage, and
  independently emitted fan triangles show no visible cracks or
  double-blended seams in the focused patterns.

### Implementation and validation status (2026-07-27)

- `SDL_render_dreamcast_pvr.c` implements the effective-bound calculation,
  four-plane clipping, per-channel ARGB interpolation, seven-element stack
  ping-pong buffers, fan emission, and exact-zero/non-finite rejection
  described here. PVR user clipping remains unused.
- Clear is now emitted as a full-target ordered draw and ignores both viewport
  and clip state, correcting the earlier viewport-sized behavior.
- A cyclic topmost/leftmost fan anchor was added after Flycast exposed
  partition-dependent output for opposite-winding clipped polygons. Cyclic
  rotation preserves signed winding while ensuring reversed input order uses
  the same fan diagonals.
- The strict-warning clipping test and the lifecycle, viewport, and
  vertex-color/blend regressions pass in Flycast. Bright guards show no visible
  right/bottom leakage, left/top and one-pixel cases remain visible, gradient
  fans show no visible seams, and opposite windings match after deterministic
  anchoring.
- The focused test's linker map resolves the renderer and SDL render core from
  the requested local archive. Opt-in and separate opt-out builds pass; the
  opt-out archive has neither the renderer object nor `DC_RenderDriver`.
- Physical Dreamcast testing passes at 640x480 VGA. All three pages match the
  Flycast reference, the one-pixel cases remain visible, the guarded
  right/bottom probe does not leak, opposite windings match, gradients have no
  visible fan seams, painter order is intact, and the program returns zero.
  No clip epsilon or PVR-specific coordinate adjustment was required.

## 1. Reviewed sources

### Confirmed source files

- SDL command creation and clip scaling:
  `src/render/SDL_render.c`
- SDL rectangle predicates and intersection behavior:
  `include/SDL_rect.h`, `src/video/SDL_rect.c`
- SDL software-renderer viewport/clip translation and clear behavior:
  `src/render/software/SDL_render_sw.c`
- SDL OpenGL renderer scissor translation and clear behavior:
  `src/render/opengl/SDL_render_gl.c`
- Current native renderer queue and PVR submission path:
  `src/render/dreamcast/SDL_render_dreamcast_pvr.c` and
  `SDL_render_dreamcast_pvr_data.h`
- KallistiOS PVR command, header, and vertex definitions:
  `$KOS_BASE/kernel/arch/dreamcast/include/dc/pvr.h` and
  `dc/pvr/pvr_header.h`
- KallistiOS submission implementation:
  `$KOS_BASE/kernel/arch/dreamcast/hardware/pvr/pvr_scene.c`
- GLdc tile-scissor command and exact polygon clipping:
  `../GLdc/GL/state.c`, `draw.c`, `private.h`, and
  `platform.h`

The mathematical vertex and triangle limits below are proofs, not claims made
by those source files.

## 2. SDL clip semantics

### 2.1 Confirmed facts: queued coordinate spaces

`SDL_RenderSetClipRect` stores a scaled `SDL_DRect`. `QueueCmdSetClipRect`
floors its `x`, `y`, `w`, and `h` independently into the queued `SDL_Rect`.
The queued rectangle is still relative to the current viewport; SDL's software
renderer adds `viewport->x` and `viewport->y` before intersecting it with the
viewport. SDL's OpenGL renderer independently confirms the coordinate rule: it
passes `viewport->x + rect->x` to `glScissor` (and performs the corresponding Y
conversion for a screen target) while preserving `rect->w` and `rect->h` as
pixel extents.

The current native `QueueGeometry` multiplies vertex positions by `scale_x`
and `scale_y` and stores them without a viewport offset. `RunCommandQueue`
adds the queued viewport origin immediately before PVR emission. Therefore the
proposed clipping path should:

1. copy each queued vertex;
2. add the current viewport origin to its position;
3. translate an enabled clip rectangle by that same viewport origin;
4. intersect the translated clip with the viewport and output bounds; and
5. clip in output coordinates.

This retains the renderer's existing rule that queued vertex data is not
mutated.

### 2.2 Confirmed facts: half-open integer rectangles

`SDL_PointInRect` defines the integer rectangle as

```text
[L, R) x [T, B)

L = rect.x
R = rect.x + rect.w
T = rect.y
B = rect.y + rect.h
```

The exact membership tests are:

```text
x >= L && x < R && y >= T && y < B
```

Thus left and top are inclusive; right and bottom are exclusive. A one-pixel
rectangle `(x, y, 1, 1)` contains integer pixel `(x, y)`, but not `(x + 1, y)`
or `(x, y + 1)`.

`SDL_RectEmpty` treats `w <= 0` or `h <= 0` as empty. The public
`SDL_RenderSetClipRect` accepts zero width or height as an enabled empty clip;
it disables clipping for a null rectangle or for negative dimensions. The
backend must distinguish disabled clipping from enabled-but-empty clipping.

Use checked/wider arithmetic when calculating `R` and `B`; do not allow signed
integer overflow in `x + w` or `y + h`.

### 2.3 Confirmed fact: clear is not clipped

SDL's software renderer explicitly disables the surface clip before executing
`SDL_RENDERCMD_CLEAR`. SDL's OpenGL renderer likewise disables scissor before
`glClear`. The SDL API defines clear over the current rendering target. Clear
ignores both viewport and clip rectangle, but it remains ordered at its
position in the command queue.

The current Dreamcast renderer's viewport-sized clear is an existing semantic
deviation. Pixel-exact clipping work should not extend that deviation by making
clear respect the clip rectangle. A later production change should retain
ordered submission while drawing a full-target clear quad.

## 3. PVR user clipping

### 3.1 Confirmed facts: command and header support

KallistiOS publicly defines:

```c
#define PVR_CMD_USERCLIP 0x20000000

typedef enum pvr_clip_mode {
    PVR_USERCLIP_DISABLE = 0,
    PVR_USERCLIP_INSIDE = 2,
    PVR_USERCLIP_OUTSIDE = 3
} pvr_clip_mode_t;
```

`pvr_poly_hdr_t` is a 32-byte command and exposes `start_x`, `start_y`,
`end_x`, and `end_y` for a user clip area. KOS labels both start fields and end
fields inclusive. A polygon header's `clip_mode` selects disabled, inside, or
outside clipping.

There is no need to write private PVR registers to submit a user-clip command.
A caller can construct the public 32-byte command representation and submit it
through the normal PVR primitive path. KOS does not provide a high-level SDL-
style pixel-rectangle helper.

### 3.2 Confirmed facts: 32-by-32 tile behavior

GLdc documents the minimum user-clip region as one 32-by-32 tile and converts
pixel extents to inclusive tile coordinates using shifts by five:

```text
start_tile_x = x0 >> 5
end_tile_x   = (x1 - 1) >> 5
```

with the corresponding Y conversion. Its `PVRTileClipCommand` stores inclusive
start and end tile coordinates. GLdc describes the PVR as switching rendering
off for tiles outside or inside that rectangle according to the polygon
header's clip mode.

Consequences:

- a clip edge not divisible by 32 cannot be represented exactly;
- the minimum non-empty mask is one 32-by-32 tile;
- end tile coordinates are inclusive, unlike SDL's exclusive pixel `R` and
  `B`; and
- hardware user clipping is a rasterization mask by tile region, not an SDL
  polygon clipper.

The reviewed KOS and GLdc sources do **not** prove that the hardware simply
discards every primitive extending outside the user-clip box. This document
therefore makes no such claim. The behavior needed here is only the
source-supported tile mask behavior.

### 3.3 Conclusion

PVR user clipping cannot implement arbitrary SDL clip rectangles precisely.
It may later mask fully covered 32-by-32 interior tiles to reduce raster work,
but CPU clipping is still required at non-tile-aligned boundaries. Combining
the two adds command-state and boundary complexity, so it is deferred until a
correct CPU baseline is measured.

## 4. GLdc as a reference

### Confirmed facts

Current GLdc has two relevant paths:

- `state.c` emits the coarse PVR tile-clip command and enables the user-clip
  mode in polygon headers.
- `draw.c` runs Sutherland–Hodgman clipping for filled triangles and quads,
  then emits each fan triangle independently with `GPU_CMD_VERTEX_EOL` on its
  third vertex.

GLdc interpolates its color as four separate floating-point `argb[]` channels.
It does not interpolate a packed ARGB integer. GLdc allocates eight vertices in
its clipping scratch arrays; that implementation choice is conservative and
does not establish an eight-vertex mathematical output bound.

GLdc is an algorithmic reference only. The native renderer should keep its own
queued `DC_PVR_Vertex` representation and submit through public KOS/PVR APIs.

## 5. Mathematical bounds

### 5.1 Maximum clipped polygon size: seven vertices

The intersection of two convex polygons with `m` and `n` edges has at most
`m + n` vertices. A nondegenerate triangle has three edges and a rectangle has
four, so their intersection has at most:

```text
3 + 4 = 7 vertices
```

The same bound follows incrementally from Sutherland–Hodgman. Clipping a convex
polygon against one half-plane can add at most one vertex. Starting from three
vertices, the maximum after `k` rectangle planes is `3 + k`; after four planes
it is seven. The bound is tight: convex polygons arising as the intersection
of a triangle and rectangle can alternate surviving source edges and rectangle
edges to form a heptagon.

This replaces the unsupported eight-vertex estimate.

### 5.2 Maximum triangulation and submitted vertices

A convex polygon with `n` vertices fan-triangulates into `n - 2` triangles.
For `n = 7`:

```text
maximum output triangles = 7 - 2 = 5
```

The proposed Dreamcast path emits every fan triangle as an independent
three-command strip:

```text
v0: PVR_CMD_VERTEX
v1: PVR_CMD_VERTEX
v2: PVR_CMD_VERTEX_EOL
```

Therefore:

```text
maximum submitted vertex commands = 5 * 3 = 15
```

There are five EOL-marked vertices, but no extra EOL vertices. The old
6-triangle, 18-vertex-plus-6-EOL, and 24-vertex estimates were incorrect.

### 5.3 Scratch-buffer capacity

For the invariant "one triangle clipped against exactly four rectangle
half-planes," two ping-pong arrays of seven `DC_PVR_Vertex` elements are
sufficient. At pass `k`, neither source nor destination can exceed `3 + k`.

The implementation should still assert the capacity on every append in debug
builds. If the helper is later generalized to arbitrary input polygons or more
planes, its capacity contract must change; silently relying on seven would no
longer be valid.

## 6. Actual PVR submission size and cost

### 6.1 Confirmed command size

KallistiOS defines the generic `pvr_vertex_t` as:

```text
flags, x, y, z, (u, v), argb, oargb
```

These eight 32-bit words total **32 bytes**, and the structure is aligned to 32
bytes. The current native renderer constructs one `pvr_vertex_t` per
`DC_PVR_Vertex` and calls its submission wrapper with `sizeof(vertex)`.
KallistiOS's direct and DMA submission paths operate in 32-byte payload units.

`pvr_poly_hdr_t` is also statically asserted by KOS to be 32 bytes.

### 6.2 Corrected worst-case submission traffic

For 1,000 input triangles, each reaching the seven-vertex/five-triangle bound:

```text
1,000 * 15 vertex commands * 32 bytes
= 480,000 bytes
= 468.75 KiB
```

That is the corrected worst-case **vertex-command traffic**.

Polygon headers are cached by the current backend. If all triangles share
state, header cost is only the applicable cached-header emissions. For a
deliberately pessimistic stream in which every input triangle forces one new
header:

```text
480,000 vertex bytes + (1,000 * 32 header bytes)
= 512,000 bytes
= 500 KiB
```

The list terminator is another 32-byte payload in KOS's immediate path. Scene
and list control costs are separate.

These byte totals must not be equated with the TA object pointer buffers,
per-tile bins, or final PVR rendering memory. Whether a scene fits depends on
the PVR initialization parameters and the geometry's tile coverage, and must
be measured. The earlier 192-KB figure and its claim of safety are removed.

## 7. Proposed exact clipping rules

Everything in this section is an implementation proposal, not current native
renderer behavior.

### 7.1 Effective clip calculation

Maintain three outcomes, not a single boolean:

1. clipping disabled;
2. clipping enabled with a non-empty effective rectangle; or
3. clipping enabled but empty.

For enabled clipping:

```text
translated = queued_clip + (viewport.x, viewport.y)
effective  = translated intersect viewport intersect output_bounds
```

If the intersection is empty, skip all affected draw primitives. If clipping
is disabled, geometry is still constrained by the viewport according to SDL
renderer behavior; represent the viewport/output intersection as the effective
geometry bounds or retain an equivalent viewport path.

Clear is handled separately as a full-target ordered operation and ignores
both viewport and clip.

### 7.2 Half-plane predicates

For effective integer bounds `L`, `R`, `T`, and `B`, classify original and
generated vertices with:

```text
left:   x >= L
top:    y >= T
right:  x <  R
bottom: y <  B
```

No epsilon is added to any bound. In particular, do not use `x <= R + eps` or
`y <= B + eps`; that expands the SDL clip region.

For a crossing edge from `S` to `E`, compute in double precision:

```text
t = d(S) / (d(S) - d(E))
```

where the plane distances are `x-L`, `R-x`, `y-T`, and `B-y`. Clamp `t` to
`[0, 1]` only as protection against floating-point roundoff. After
interpolating attributes, assign the clipped position's relevant coordinate
directly to exactly `L`, `R`, `T`, or `B`, rather than trusting the calculated
coordinate to land on the plane.

Although right/bottom membership is strict, an intersection vertex lies on
`R` or `B`: it is the boundary of the closure of the half-open region. A
pixel-center/top-left-style rasterizer should cover samples below/left of that
edge and not samples at the excluded integer coordinate. Keeping adjacent fan
triangles on the same exact floating-point boundary also avoids introducing
CPU-generated gaps between them.

Integer clip boundaries within Dreamcast output sizes are exactly
representable as IEEE-754 single-precision values. Computing `t` in double and
then pinning the position to the plane prevents arithmetic drift across the
nominal boundary. The reviewed KOS sources do not specify the PVR's
subpixel/top-left edge rules in enough detail to prove final raster behavior,
but the focused 640x480 VGA hardware test confirms the expected integer-edge
coverage without a coordinate adjustment.

Do not nudge right or bottom intersections inward with an arbitrary epsilon or
`nextafter`: that would shrink the clip, can remove coverage from one-pixel
clips, and can create seams. Add such a workaround only if a reproducible PVR
test establishes a hardware-specific need.

### 7.3 Attribute interpolation

Interpolate position, `u`, and `v` along the crossing edge with the same `t`:

```text
value(t) = value0 + (value1 - value0) * t
```

The current renderer stores 2D affine UVs and uses `z = 1`, so this is affine
2D interpolation. No claim of perspective-correct interpolation is needed.

For packed `0xAARRGGBB`, define shifts `sA=24`, `sR=16`, `sG=8`, and `sB=0`.
For each channel `C` independently:

```text
C0 = (argb0 >> sC) & 0xff
C1 = (argb1 >> sC) & 0xff
qC = C0 + (C1 - C0) * clamp(t, 0, 1)
C  = clamp(floor(qC + 0.5), 0, 255)

argb = (A << 24) | (R << 16) | (G << 8) | B
```

Use signed or floating-point subtraction for `C1 - C0`; unsigned subtraction
would underflow for descending channels. This nearest-integer rule is exact and
repeatable. Interpolating the packed 32-bit word is forbidden because carries
and borrows couple unrelated channels.

### 7.4 Degenerate rejection and winding

Sutherland–Hodgman processes edges in their input traversal order. Clipping a
convex polygon against a half-plane preserves that cyclic order and therefore
preserves the winding of every nondegenerate result. Fan triangles
`(p0, pi, p(i+1))` inherit the polygon's winding.

The current native PVR header disables face culling, so a winding error would
not currently remove a triangle, but preserving winding remains required for
future culling and for consistent edge rasterization.

For every fan triangle, calculate signed doubled area in double precision:

```text
area2 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
```

Reject the triangle if any position or `area2` is non-finite, or if
`area2 == 0.0`. Do not use a positional or area epsilon in the first
implementation. An epsilon large enough to be operationally useful can reject
legitimate subpixel triangles and is not needed to enforce clip inclusion.
If hardware testing later finds unstable near-zero primitives, introduce a
documented, scale-aware area threshold based on measured PVR subpixel
precision—not an epsilon in the clip predicates.

Optionally remove adjacent identical vertices, including a duplicate closing
vertex, before fan triangulation. Degenerate rejection is still required after
that cleanup.

## 8. Implemented emission flow

For each queued geometry command:

1. resolve viewport and effective clip state;
2. emit/reuse the appropriate polygon header;
3. process the queued vertices in independent groups of three;
4. copy and viewport-translate each input triangle;
5. reject non-finite input;
6. Sutherland–Hodgman clip it against all four planes;
7. select a deterministic fan anchor by winding-preserving cyclic rotation;
8. fan-triangulate the convex result;
9. reject each zero-signed-area fan triangle; and
10. submit three 32-byte `pvr_vertex_t` commands per survivor, with EOL on the
    third.

All clipping scratch memory remains on the stack. Queue callbacks stay CPU-only
and continue to own/copy SDL inputs; only `RunCommandQueue` performs clipping
and PVR submission.

## 9. Validation requirements

### Source-independent tests

- disabled clip versus enabled empty clip;
- clips wholly and partly outside the viewport;
- nonzero viewport translation;
- one-pixel-wide and one-pixel-high clips;
- triangles touching each edge without crossing it;
- triangles crossing one, two, three, and all four rectangle edges;
- a constructed seven-vertex result and its five emitted fan triangles;
- exact channel interpolation with ascending and descending A/R/G/B values;
- duplicate vertices and zero-area results;
- both input windings, checking that every surviving fan triangle retains the
  input signed-area sign;
- textured and untextured geometry; and
- alternating blend states to retain painter order.

### Flycast and physical-hardware tests

- Compare native output pixel-for-pixel or via captured reference patterns
  against the SDL software renderer at left/top/right/bottom clip boundaries.
- Place bright geometry immediately outside right and bottom boundaries to
  reveal one-pixel leakage.
- Draw adjacent clipped triangles with contrasting interpolation gradients to
  reveal fan seams.
- Repeat at integer, half-pixel, and other subpixel source positions.
- Test tile-aligned and non-tile-aligned clip rectangles at 320x240 and
  640x480.
- Measure submission traffic and actual TA/OPB exhaustion separately with the
  configured PVR buffers; do not infer capacity from the 480,000-byte command
  total.

## 10. Physical-hardware result and remaining coverage

The mathematical clip polygon and SDL half-open bounds are settled. Physical
Dreamcast testing at 640x480 VGA established the increment's required gates:

- excluded right and bottom edges do not visibly leak into the dark guard;
- included left and top coverage and one-pixel-wide/high clips remain visible;
- independent fan triangles show no visible cracks, double-blended seams, or
  winding-dependent partition differences;
- the constructed seven-vertex result and thin cases agree with Flycast; and
- ordered clears and `NONE`/`BLEND` overlaps retain their expected positions.

This validates pixel-exact clipping for the tested 640x480 VGA path. Continue
to use exact planes and no positional epsilon. Testing other video modes,
half-pixel and additional subpixel inputs, and TA-capacity stress remains part
of the broader hardening matrix; any future mode-specific discrepancy must be
corrected from measured evidence rather than by expanding the SDL rectangle.

## Final recommendation

Retain full CPU Sutherland–Hodgman clipping as the validated correctness
baseline. Use seven-element ping-pong buffers under the triangle/four-plane
contract,
per-channel ARGB interpolation, exact integer clip planes, signed-area
degenerate rejection, and independent three-command PVR triangles. Keep PVR
user clipping disabled for this first increment; revisit its 32-by-32 tile mask
only as a measured Phase 6 optimization.
