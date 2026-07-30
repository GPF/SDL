# Dreamcast Native PVR Renderer Plan

## Direction

Build the Dreamcast renderer as a native KallistiOS/PVR backend. Use the SDL
OpenGL renderer and GLdc as behavioral and algorithmic references, but do not
route this backend through OpenGL or depend on GLdc private internals.
Functionality and algorithms from the GLdc implementation may be adapted into
the native PVR renderer when useful, provided the resulting code uses public
KallistiOS/PVR interfaces and preserves the native backend's independent
ownership and command architecture.

The native renderer should provide value through lower overhead, explicit VRAM
management, and direct support for Dreamcast texture formats. If the objective
is only to get accelerated `SDL_Renderer` support working quickly, improving
the existing GLdc-backed OpenGL renderer remains the shorter path.

Treat the current `src/render/dreamcast/SDL_render_dreamcast_pvr.c` as the
validated Phase 0 baseline, not as a nearly complete driver. Preserve its
queue and scene ownership rules while extending it in later phases.

**Runtime constraints:**
- Only one video driver and one renderer exist per program.
- No runtime switching between renderers or video drivers.
- Video driver selected at window creation via `SDL_HINT_DC_VIDEO_MODE`.
- Renderer selected at creation time, fixed for lifetime.
- PVR ownership follows the renderer: if the native PVR renderer is created,
it owns PVR init/shutdown. No shared state, no conflicts.

**Build-time isolation:**
- Any code that changes default SDL2 behavior must be protected by a Dreamcast
  build-time guard. Dreamcast-specific behavior must not affect builds for
  other platforms or configurations where the relevant Dreamcast feature is
  disabled.

### Unified Dreamcast library architecture

The normal Dreamcast build enables both OpenGL and the native PVR renderer and
produces one renderer-capable `libSDL2.a`. That archive contains the
GLdc-backed SDL OpenGL renderer and the independent native Dreamcast PVR
renderer. `SDL_HINT_DC_VIDEO_MODE` selects the video path before window
creation, and SDL renderer selection chooses `opengl` or `Dreamcast PVR` when
the renderer is created. Applications do not replace or switch SDL archives
to select a renderer. `libSDL2main.a` is only SDL's normal entry-point helper,
not a renderer-specific SDL library variant.

Final Dreamcast applications intentionally link `-lGL` with the unified build.
That link dependency does not route native renderer drawing through OpenGL:
the native PVR renderer objects submit through public KallistiOS/PVR APIs and
contain no unresolved `gl*` calls. Separate renderer archives such as
`libSDL2_gl.a` are not part of the design.

Configuring `SDL_OPENGL=OFF` is a supported feature-stripping choice that
removes SDL OpenGL renderer and context functionality from the archive. It
does not redefine the native renderer architecture; the normal unified build
keeps `SDL_OPENGL=ON`.

## Problems inherited from the original prototype

The Phase 0 implementation corrects the lifecycle, command-submission,
advertising, callback-signature, and baseline texture issues below. Keep this
list as regression context for later work rather than as a description of the
current Phase 0 code.

- Several renderer callback signatures do not match the current
  `SDL_sysrender.h`, including `QueueCopyEx`, `QueueGeometry`, and
  `SetTextureScaleMode`.
- Queue callbacks submit PVR commands immediately. SDL queue callbacks must
  copy or convert their inputs into renderer-owned command data; GPU submission
  belongs in `RunCommandQueue`.
- PVR scenes and lists are begun from unrelated callbacks without a single
  lifecycle owner.
- Advertised pixel formats do not match the implemented texture conversions.
- `UpdateTexture` ignores the requested rectangle and source pitch.
- Streaming locks expose VRAM directly instead of using managed CPU staging
  storage and an explicit upload operation.
- Texture driver data contains only a VRAM pointer, so format, backing size,
  filtering, pitch, access, and dirty state cannot be retained.
- Texture destruction passes the same allocation to both `pvr_mem_free` and
  `SDL_free`.
- Clear, clipping, geometry, rotation, and render-target execution are stubs.
- `SDL_RENDERER_TARGETTEXTURE` is advertised before target textures work.
- The software framebuffer, GLdc, and native renderer can independently
  initialize or shut down PVR. Ownership must be unified.

## Architecture

Organize the backend into three layers:

1. **SDL command ingestion**

   Queue callbacks convert SDL inputs into a compact native vertex format with
   `SDL_AllocateRenderVertices`. Prefer implementing `QueueGeometry` and
   leaving `QueueCopy` and `QueueCopyEx` unset, allowing SDL to translate copy,
   rotation, and flip operations into geometry.

2. **PVR state translation**

   `RunCommandQueue` tracks the viewport, clip rectangle, texture, filtering,
   modulation, and blend mode. It emits a compiled polygon header only when
   relevant state changes.

3. **PVR scene ownership**

   One renderer-level scene manager owns scene begin/finish, polygon-list
   begin/finish, render-target transitions, synchronization, and presentation.
   Texture and queue callbacks must not independently begin scenes.

The renderer data should track at least:

- whether it owns PVR initialization;
- whether a scene and polygon list are active;
- the current screen or texture target;
- output dimensions and current draw state;
- vsync state; and
- synchronization or upload state needed by pending textures.

Each texture should track at least:

- CPU staging and VRAM allocations;
- SDL and PVR formats;
- logical and backing dimensions;
- CPU and VRAM pitch;
- allocation and upload sizes;
- UV scale;
- nearest or linear filtering;
- static, streaming, or target access;
- dirty/upload state; and
- twiddled, VQ, or strided layout flags.

For the first correct implementation, evaluate submitting all SDL drawing
through the transparent polygon list with autosorting disabled, including
`SDL_BLENDMODE_NONE`. This is the simplest prospective way to retain SDL's
strict painter order. Routing opaque and transparent commands through separate
PVR lists is a later optimization because list processing order can conflict
with SDL command order.

## Phase 0: feasibility spike

Implement the smallest native path necessary to demonstrate:

- checked PVR initialization and teardown (renderer-owned);
- one scene per presented frame (begin → lists → finish → sync);
- clear (full-screen, unclipped);
- solid rectangles (untextured fills);
- one RGB565/ARGB1555 texture (static, POT backing, nearest filter);
- nearest-neighbor textured copies; and
- correct command ordering.

**Validation test:** alternate opaque and alpha-blended overlapping rectangles,
then draw a textured quad overlapping them. Confirm painter order matches SDL
software renderer.

**Build:** add `-DSDL_VIDEO_RENDER_DREAMCAST_PVR=ON` CMake option (disabled by default).

Validate on physical Dreamcast hardware and retain a hardware screenshot for
each visually distinct page or workload. Measure TA/list capacity with a few
thousand sprites.

If correct ordering cannot be maintained or performance is unacceptable, stop
the native implementation and focus on the GLdc-backed renderer.

**Reference:** `src/render/dreamcast/PHASE0_SKETCH.md` retains its historical
filename but now records the implemented Phase 0 contract and validation
results.

### Phase 0 validation status

- The native renderer builds through the opt-in Dreamcast CMake configuration.
- Physical Dreamcast hardware using 640x480 VGA displays the stable validation
  scene: a blue rectangle with a red RGB565 textured rectangle partially
  covering it.
- The lifecycle test completes 32 renderer create/destroy iterations, including
  rejected target-texture creation, fixed-vsync validation, presented frames,
  and flushed-but-not-presented destruction. A renderer created after the
  stress loop still renders the stable validation scene correctly.
- Brief lines or blanking during the create/destroy stress loop are expected
  because each iteration deliberately shuts PVR down and clears/reconfigures
  video memory. They are not the visual correctness gate; the stable frame
  displayed while the final renderer remains alive is the gate.
- TA/list-capacity measurement with several thousand sprites remains pending.

### Phase 2.1: viewport translation — done

- Viewport offset is tracked in `DC_PVR_RenderData` (`view_x`, `view_y`,
  `view_w`, `view_h`, `has_viewport`, plus `clip_rect` and `has_cliprect`
  as captured state, clipping itself is later).
- `RunCommandQueue` captures `SDL_RENDERCMD_SETVIEWPORT` and
  `SDL_RENDERCMD_SETCLIPRECT` commands from the SDL command chain.
- Geometry vertices are translated by the viewport offset at emit time in
  `RunCommandQueue`. Queued vertex data is not mutated; each vertex is copied
  into a local `DC_PVR_Vertex` before the offset is applied, preserving the
  Phase 0 queue semantics.
- `DC_PVR_EmitClear` fills the viewport rectangle when one is active,
  otherwise the full output dimensions. Order is preserved: a clear appearing
  after other draws still renders in submission order.
- `QueueGeometry` is unchanged. `QueueSetViewport` and `QueueSetDrawColor`
  remain no-ops; state is picked up from the command chain at execution time.
- Pixel-exact clipping is not yet applied (tracked as captured state only).
- Points, lines, untextured triangles, and blend-mode capability checks are
  deferred.

**Build:** SDL2 builds with `cd build-scripts && ./dreamcast.sh build` and
installs to `kos/addons`. The viewport test builds with
`cd test/dreamcast/test/pvr_viewport && make -f Makefile.dc`. No compiler
warnings from the renderer source.

**Hardware test:** passed on real DC (640x480 VGA). `pvr_viewport.bin` shows
black outside the viewport, a green filled rectangle inside the 300x200 area
at (50,30), a red filled rectangle at SDL-relative (60,50), and a green
RGB565 textured quad at SDL-relative (200,100). Painter order and viewport
offset are correct.

**Remaining Phase 2 scope:** points as 1x1 quads and lines as
quads/triangles. CPU-clipped arbitrary triangle geometry is implemented in the
Phase 2.3 increment below.

### Phase 2.2: vertex color and baseline blending — done

- `QueueGeometry` copies every input `SDL_Color` into renderer-owned vertex
  data as exact PVR packed ARGB (`0xAARRGGBB`). Both textured and untextured
  geometry submit that command-owned color through `pvr_vertex_t.argb`, with
  PVR Gouraud shading and vertex alpha explicitly enabled for RGB and alpha
  interpolation.
- Texture copies receive SDL texture color and alpha modulation through the
  colors SDL places in their generated geometry. `SDL_RenderGeometry` follows
  SDL's documented behavior and uses its per-vertex colors while ignoring
  texture-level modulation. Textured headers explicitly select
  `PVR_TXRENV_MODULATEALPHA`, whose KallistiOS definition multiplies sampled
  and vertex ARGB; `PVR_TXRENV_MODULATE` is insufficient because it does not
  multiply vertex alpha. The inverted KallistiOS texture-alpha disable flag is
  explicitly kept false so texture alpha remains enabled.
- Header-cache identity includes the texture pointer, SDL blend mode, and PVR
  texture-environment mode. Texture environment is set explicitly rather than
  inherited implicitly from the polygon-context constructor.
- `SupportsBlendMode` accepts exactly `SDL_BLENDMODE_NONE` and
  `SDL_BLENDMODE_BLEND`. In opt-in Dreamcast PVR builds, SDL's blend capability
  check consults an installed backend callback even for predefined modes, so
  unsupported standard modes fail at their public setter instead of reaching
  `RunCommandQueue` and being approximated.
- `NONE` maps to PVR source `ONE`, destination `ZERO`. `BLEND` maps to source
  `SRCALPHA`, destination `INVSRCALPHA`. Both remain in the single
  autosort-disabled transparent list with depth comparison `ALWAYS` and depth
  writes disabled, preserving painter order.
- `ADD`, `MOD`, `MUL`, and unsupported custom modes are rejected. `ADD` and
  `MOD` remain deferred: SDL specifies separate color and alpha factors while
  the current single-pass PVR context exposes one shared source/destination
  pair, and no exact mapping has yet passed physical-hardware tests.
- Queue callbacks remain CPU-only. Later renderer or texture state changes do
  not alter colors or blend modes already captured by queued draw commands.
  Only `RunCommandQueue` submits headers and vertices; `RenderPresent` remains
  lifecycle-only. DMA, clipping, other polygon lists, and scene ownership are
  unchanged.

`test/dreamcast/test/pvr_vertex_color_blend` covers an RGB-interpolated
triangle, alpha interpolation, textured per-vertex ARGB modulation, texture
copy color/alpha modulation, alternating `NONE`/`BLEND` painter order, public
rejection of unsupported draw and texture modes, and a duplicate of the visual
cases inside the nonzero viewport `(320,0 320x480)`.

**Build results:** The opt-in Dreamcast static library builds successfully.
The lifecycle, viewport, and vertex-color/blend tests compile with
`-Wall -Wextra -Werror` and link against that library. A separate opt-out
Dreamcast configuration also builds successfully; it produces neither a
native PVR renderer object nor the `DC_RenderDriver` archive symbol. The PVR
test Makefiles name `$(SDL2_LIBDIR)/libSDL2.a` and `libSDL2main.a` explicitly:
the KOS compiler wrapper prepends its installed addon-library directory ahead
of user `-L` options, which otherwise makes an `SDL2_LIBDIR` override silently
link the installed archive. A linker map confirms that the rebuilt test uses
both renderer objects from the requested local archive.

**Runtime results:** The previously validated Phase 0 lifecycle and viewport
hardware results remain the baseline. An initial physical-hardware launch of
the new test linked the older installed SDL archive because of the KOS library
search ordering described above; its API precheck reported that `ADD` was
accepted and exited before queuing a frame, so the blank display was expected
and was not a renderer visual result.

After rebuilding with the requested archive explicitly, the corrected test
passes on physical Dreamcast hardware. The full-output and `(320,0 320x480)`
viewport panels are visually identical; RGB and alpha interpolate smoothly
across their triangles; textured per-vertex ARGB and texture-copy color/alpha
modulation are correct; and alternating `NONE`/`BLEND` overlaps retain painter
order. Reaching the visual scene also confirms the API precheck accepted `NONE`/`BLEND` and
rejected `ADD`, `MOD`, `MUL`, the custom subtract mode, and texture `ADD`.
No Phase 2.2 cases remain pending physical-hardware validation.

### Phase 2.3: first CPU pixel-clipping increment — done

- `RunCommandQueue` calculates geometry bounds from output bounds, the current
  viewport, and (when enabled) the viewport-relative clip rectangle translated
  into output coordinates. Disabled clipping still intersects geometry with
  the effective viewport and output. Enabled empty clips, empty viewports, and
  intersections outside the output reject the command's geometry without PVR
  submission.
- Every independent input triangle is clipped in CPU code against the four
  half-open planes `x >= left`, `y >= top`, `x < right`, and `y < bottom`.
  The implementation uses two seven-element stack buffers, has debug and
  release bounds checks, and does not use heap scratch storage, an epsilon, or
  PVR user clipping.
- Intersection `t` is calculated in double precision and clamped only to
  `[0,1]` for floating-point protection. Position and UV use the same `t`;
  the clipped coordinate is then pinned to its integer-valued plane. A, R, G,
  and B are unpacked, independently interpolated and rounded with
  `floor(qC + 0.5)`, clamped to `[0,255]`, and repacked. Packed colors are never
  interpolated as one integer.
- The clipped convex polygon has at most seven vertices and is emitted as at
  most five independent fan triangles. A deterministic topmost/leftmost fan
  anchor is selected by cyclic rotation, which preserves signed input winding
  while giving reversed input order the same diagonal partition. Each triangle
  is three 32-byte `pvr_vertex_t` commands with EOL on the third command.
- Each fan triangle is rejected when any position/UV or its double-precision
  signed doubled area is non-finite, or when its area is exactly zero. There is
  no area epsilon in this increment.
- Clear was corrected from the earlier viewport-sized behavior to an ordered
  full-target quad. It ignores viewport and clip state but remains at its exact
  command-queue position.
- The established invariants remain intact: queue callbacks are CPU-only,
  queued vertices are immutable, viewport translation is applied to local
  copies at emission, only `RunCommandQueue` submits commands, and the single
  autosort-disabled transparent list and existing `NONE`/`BLEND` mappings are
  unchanged.

`test/dreamcast/test/pvr_clipping` is a three-page focused test for disabled,
empty, changed, translated, partial, and outside clips; one-pixel dimensions;
all four clip planes and the seven-vertex bound; UV/RGBA interpolation;
opposite windings; outside and zero-area triangles; ordered blending; and
clears before, between, and after clipped draws. Bright guard geometry exposes
right/bottom leakage and contrasting gradients expose fan seams. The lifecycle,
viewport, vertex-color/blend, and clipping tests all compile with
`-Wall -Wextra -Werror` against explicit local archives. The clipping linker
map resolves both `SDL_render.c.obj` and `SDL_render_dreamcast_pvr.c.obj` from
the requested local `build-scripts/dcbuild/libSDL2.a`.

**Automated validation (2026-07-27):** The opt-in static build succeeds. A
separate opt-out build in `/tmp/sdl2-dc-optout` succeeds with the option OFF,
no renderer object, and no `DC_RenderDriver` archive symbol.

Diagnostic inspection found that opposite windings initially produced
different textured results because their cyclic polygon order selected
different fan anchors and therefore different diagonal partitions. Selecting
one deterministic geometric anchor fixed the discrepancy without reversing
the polygon or changing its signed winding.

**Physical-hardware validation (2026-07-27):** Passed on a real Dreamcast at
640x480 VGA. All three clipping pages match their documented expectations and
the program returns zero. Page 1 retains the included left/top coverage and
both one-pixel white strips; the magenta right/bottom leakage probe remains fully
inside its dark guard; disabled, empty, changed, partial, and outside clips
produce the expected blocks; and no orange remains after the final full-target
clear. Page 2 shows all four edge-crossing triangles, the multi-edge and
seven-vertex results, and smooth RGB/alpha gradients without visible cracks,
fan seams, or double-blended shared edges. Page 3 shows matching checkerboard
and modulation for both input windings, empty outside/zero-area guards, and the
expected ordered `NONE`/`BLEND` overlaps. No PVR-specific coordinate offset or
clip epsilon was required.

This completes the first pixel-exact clipping increment for the validated
640x480 VGA path. The broader 320x240, PAL/NTSC, subpixel-position, and stress
matrix remains Phase 7 hardening rather than a blocker for this increment.

## Agent physical-hardware validation workflow

All Dreamcast runtime validation must run on physical Dreamcast hardware. Every
validation must include at least one captured image of the actual hardware
output for each visually distinct test page or workload.
Prefer a raw framebuffer capture written through the hardware `/pc` path (for
example, with `vid_screen_shot`) when the test supports it. Otherwise ask the
onsite operator to photograph the physical display and attach the image.

Inspect the hardware capture directly and compare it first with the visual
expectations derived from the test source and its documented contract. Do not
run an emulator as a validation or diagnostic substitute. Record the hardware
image path and the concrete visual observations. Serial output, stage-completion
messages, and a PASS line do not establish visual correctness by themselves.

If a test advances too quickly to capture or inspect, coordinate with the
onsite operator before launch and use an existing pause or observation mode.
When the task permits a test-only correction, extending display hold times is
acceptable, but do not infer a visual pass from the brief output.

Do not manually interrupt or terminate `kos-tool`/`dc-load` while the uploaded
ELF is still running. An external Ctrl-C during program execution can crash the
physical Dreamcast. Let the test exit normally and wait for `Program returned`.
When a hardware diagnostic needs to return early, add an explicit in-program
`arch_exit()` path so control returns safely to the `dc-load-ip` loader before
ending the host command. If the console becomes unresponsive, ask the onsite
operator to restart the Dreamcast rather than repeatedly interrupting or
relaunching it.

## Phase 1: lifecycle and build integration

- Add an explicit Dreamcast PVR CMake option (`SDL_VIDEO_RENDER_DREAMCAST_PVR`),
  initially disabled by default.
- Add the renderer source explicitly (`src/render/dreamcast/*.c`) instead of
  broadening the video-source glob.
- Correct every callback signature against the current SDL branch (add
  `scale_x`/`scale_y` to `QueueCopyEx` and `QueueGeometry`).
- Renderer owns PVR: checked `pvr_init()` with direct submission and DMA
  disabled on create, then `pvr_shutdown()` on destroy only if initialization
  succeeded. Do not call `pvr_dma_init()` or use PVR DMA before the texture
  optimization phase.
- Reject incompatible window or context combinations with useful SDL errors.
- Check every PVR initialization result.
- Implement `SetVSync`.
- Advertise only tested capabilities (drop `SDL_RENDERER_TARGETTEXTURE` until
  Phase 5).
- Move from immediate PVR submission in queue callbacks to proper data queueing
  for `RunCommandQueue`.

Acceptance gate: the renderer can be created and destroyed repeatedly without
double initialization, leaked VRAM, or breaking later software or GL renderer
creation.

## Phase 2: command queue and untextured primitives

Implement:

- viewport translation (implemented in Phase 2.0 increment);
- exact clip rectangles (CPU triangle clipping implemented and validated at
  640x480 VGA);
- ordered clear operations (corrected to full-target SDL semantics);
- fill rectangles;
- points expanded to 1-by-1 quads;
- lines expanded to quads or triangles;
- arbitrary untextured triangle geometry;
- vertex colors and alpha; and
- blend-mode capability checks.

PVR user clipping is tile-granular and is not sufficient by itself for SDL's
pixel-exact clip rectangles. The correctness path uses full CPU polygon
clipping. A tile-assisted hybrid remains a later measured optimization, and
GLdc's scissor polygon clipping remains a useful reference.

Treat clear as an ordered draw when it appears after another command.
`pvr_set_bg_color` may optimize only a leading, unclipped, full-target clear.

Acceptance gate: geometry, viewport, point/line edge cases, multiple clears,
and overlapping draw-order tests render correctly.

**Clipping research status (2026-07-27):** Research and the first CPU
Sutherland–Hodgman implementation increment are complete. The hybrid
tile-plus-boundary strategy is deferred as a Phase 6 optimization. See
`src/render/dreamcast/PVR_CLIPPING_RESEARCH.md` for the design, bounds, and
physical-hardware validation results. The focused 640x480 VGA
pixel-exact gate passes; other modes and the wider subpixel/stress matrix remain
Phase 7 hardening.

**Points/lines research status (2026-07-28, source-corrected):** Point and line
semantics research complete. Full document at
`src/render/dreamcast/PVR_POINTS_LINES_RESEARCH.md`. SDL behavior confirmed
through source review: points queue as `SDL_FPoint` arrays; lines use the
`SDL_RENDERLINEMETHOD_POINTS` default (set by `SDL_GetRenderLineMethod`, not
overridden by installing backend callbacks), which rasterizes axis-aligned
segments to rects and diagonals through Bresenham with shared-endpoint
ownership (`draw_last`). Zero-length behavior is conditional: an independent
zero-length line owns its final endpoint and queues a 1x1 rect at unit scale;
an interior repeated segment is skipped; a final repeated point in a nonclosed
strip may queue its endpoint once; and a zero-length closing segment in a
closed strip has `draw_last == false` and remains an empty rect. SDL's
`QueueCmdFillRects` falls back to geometry
expansion (4 vertices, 6 indices per rect) when `QueueFillRects` is NULL,
but implementing `QueueFillRects` directly is more efficient. Implementation
recommendation: add `QueueDrawPoints` and `QueueFillRects` callbacks, expand
each point to a 1×1 quad and each rect to a filled quad in `RunCommandQueue`
as two independent triangles per quad (6 `pvr_vertex_t` commands, 192 bytes),
reuse the existing clipped-triangle path, apply viewport translation only in
emission, preserve floating-point rect extents, and never enlarge empty rects.
SDL owns endpoint inclusion before backend queueing; no endpoint logic is
needed in the backend. The focused test separately covers independent,
interior-repeat, final-repeat, and closed-strip zero-length cases in `NONE` and
`BLEND` against the SDL software renderer. TA/OPB capacity for point/line
stress remains unmeasured. The implementation and validation status is
recorded below.

### Phase 2.4: points and default POINTS-method lines — done

- Registered `QueueDrawPoints` and `QueueFillRects`; `QueueDrawLines` remains
  unset. Both callbacks are CPU-only, allocate renderer-owned command data,
  capture packed draw color, preserve floating-point inputs, and perform no
  viewport translation, clipping, or PVR submission.
- Added `SDL_RENDERCMD_DRAW_POINTS` and `SDL_RENDERCMD_FILL_RECTS` handling in
  `RunCommandQueue`. It translates by the active viewport exactly once, expands
  each point or positive rect to a half-open quad, and routes `(v0,v1,v2)` and
  `(v0,v2,v3)` through the existing clipped-triangle path. Non-finite and empty
  rects remain empty. Headers are emitted lazily for drawable commands.
- Each unclipped point or rect submits two triangles, six 32-byte
  `pvr_vertex_t` commands, and 192 bytes of vertex-command traffic. Clipping,
  blend support, clear semantics, painter order, list routing, and header-cache
  identity are unchanged.
- `test/dreamcast/test/pvr_points_lines` provides three pages for points;
  default public-API lines and exact zero-length endpoint ownership; and
  viewport/clipping/ordered-clear behavior. It includes independent,
  interior-repeat, final-repeat, and closed-strip cases with BLEND probes.
- The opt-in SDL build and all five renderer tests pass strict-warning builds
  against the explicit local archive. The new map resolves `SDL_render.c.obj`
  and `SDL_render_dreamcast_pvr.c.obj` from that archive. The separate opt-out
  archive has neither the renderer object nor `DC_RenderDriver`.
- **Physical-hardware validation (2026-07-27):** Passed on a real Dreamcast at
  640x480 VGA. All three photographed pages match their documented expectations
  and the program returns zero. Page 1 shows the framebuffer-corner points,
  isolated and adjacent points, the four included clip-edge points, excluded
  right/bottom probes, the one-pixel clip, the expected `NONE`/`BLEND`
  duplicate intensities, and the post-clear markers. Page 2 shows continuous
  horizontal, vertical, 45-degree, shallow, steep, and reversed lines; the
  independent zero-length markers; connected and repeated-point patterns; and
  a closed strip without brighter shared endpoints or corners. Page 3 shows
  correct nonzero-viewport translation, continuous multi-boundary clipped
  lines, visible one-pixel clip results, exact cyan and magenta guards with no
  right/bottom leakage, ordered clip changes, and no residual pre-clear orange.
  No cracks are visible along either triangle diagonal. This completes the
  focused points/default-lines increment for the validated 640x480 VGA path.
  Wider video-mode and TA/OPB stress coverage remains Phase 7 hardening.

## Phase 3: baseline texture support

Initially advertise only native formats that are genuinely supported:

- `SDL_PIXELFORMAT_RGB565`;
- `SDL_PIXELFORMAT_ARGB1555`; and
- `SDL_PIXELFORMAT_ARGB4444`.

Allow SDL to create conversion/native textures for other normal input formats.
Do not advertise RGB888, BGR888, or YUY2 until real implementations exist.

Begin with non-twiddled 16-bit storage and implement:

- power-of-two backing dimensions where necessary;
- logical-to-backing UV scaling;
- nearest and bilinear filtering stored per texture;
- color and alpha modulation;
- correct source-pitch and partial-rectangle updates;
- CPU staging allocations for streaming textures; and
- upload during `UpdateTexture` or `UnlockTexture`.

Use synchronous `pvr_txr_load` first. Add DMA only after correctness, including
its alignment, cache-flush, and transfer-size requirements.

Acceptance gate: static and streaming textures, subrectangle updates, NPOT
logical sizes, filtering, modulation, rotation, and flips all work.

### Phase 3.1: baseline native textures — focused increment done

- The renderer is split into lifecycle/dispatch, geometry, and texture
  subsystems. The opt-in build contains all three native PVR objects; the
  opt-out build contains none and exports no `DC_RenderDriver`.
- The backend advertises exactly RGB565, ARGB1555, and ARGB4444. Static and
  streaming textures use zero-initialized full-POT CPU staging with matching
  nontwiddled 16-bit VRAM allocations. Target textures remain rejected.
- Full and partial updates honor caller pitch and backing pitch, while full and
  partial streaming locks return a pointer into staging with the full backing
  pitch. Updates and unlocks synchronously upload the complete backing through
  `pvr_txr_load`; DMA and partial VRAM uploads remain deferred.
- Nearest and bilinear filtering are supported and included in polygon-header
  cache identity. RGB565 disables sampled texture alpha while retaining vertex
  alpha; ARGB1555 and ARGB4444 retain sampled alpha. Existing modulation,
  `NONE`/`BLEND`, clipping, and painter-order behavior is preserved.
- `test/dreamcast/test/pvr_textures` covers the three formats, static and
  streaming paths, partial updates and locks, padded/invalid source pitch,
  NPOT logical dimensions, filtering, modulation, RGB565 alpha behavior,
  target/filter rejection, and painter order. It builds with
  `-Wall -Wextra -Werror` against the explicit local archive.

**Physical-hardware validation (2026-07-27):** Passed on a real Dreamcast at
640x480 VGA. The ELF was uploaded over the Broadband Adapter with
`kos-tool -t 192.168.0.128 -x pvr_textures.elf -m .`. A temporary build with
`PVR_TEXTURE_HARDWARE_SCREENSHOT` called
`vid_screen_shot("/pc/pvr_textures-hardware.ppm")`; KOS successfully wrote the
raw 640x480 framebuffer to
`test/dreamcast/test/pvr_textures/pvr_textures-hardware.ppm` with SHA-256
`96ac33fc4320a1f4e17556615f026848bc34fd589c9b587bb64126e5d1ed2c94`.
The test reached its PASS line, returned zero, and reported no SDL, PVR, KOS,
dc-load, or transfer errors. Direct inspection confirms the RGB565 NPOT border
and partial update without POT-padding leakage; the streaming checker and
partial yellow lock update; ARGB1555 transparent cutouts; the ARGB4444 alpha
gradient; sharp nearest and smooth bilinear filtering; color/alpha modulation;
opaque RGB565 red texels; and blue/translucent-texture/green painter order.
The hardware framebuffer matches the documented visual expectations. The
ordinary non-capturing ELF was rebuilt afterward without
`PVR_TEXTURE_HARDWARE_SCREENSHOT`.

Remaining Phase 3 validation includes a broader texture-dimension and
edge-update matrix, repeated create/destroy/update/lock stress, and
VRAM-exhaustion rollback. Other video modes and performance/capacity
measurements remain Phase 7 hardening.

### Phase 3.2: source rectangles, scaling, rotation, and flips — done

- `SDL_RenderCopy`/`SDL_RenderCopyF` and the nontrivial
  `SDL_RenderCopyEx`/`SDL_RenderCopyExF` path fall back through SDL geometry
  because the backend leaves `QueueCopy` and `QueueCopyEx` unset. SDL supplies
  four vertices and six 32-bit indices, with logical source-rectangle UVs,
  destination-position scaling, flips applied to destination axes, and default
  or destination-relative explicit centers. Positive SDL angles appear
  clockwise in y-down render coordinates. Public geometry may additionally be
  unindexed or use 8-, 16-, or 32-bit indices.
- `QueueGeometry` now validates geometry pointers, counts, strides, triangle
  divisibility, index size/range, scale/allocation arithmetic, and finite
  position/UV results. It safely expands every supported index form into
  renderer-owned independent triangles. Queue callbacks remain CPU-only and
  only `RunCommandQueue` submits PVR commands.
- Because the Dreamcast toolchain uses `-ffast-math`, compiler assumptions can
  eliminate ordinary `isfinite()` checks. Bitwise IEEE-754 exponent tests now
  protect queued geometry and the existing clipping/point/rect paths under the
  actual KOS flags.
- Independent Phase 3.2 source review confirmed two queue-validation gaps.
  `QueueFillRects` can receive floating-point widths and heights directly from
  `SDL_RenderFillRectsF`, so it now rejects non-finite dimensions with the
  `-ffast-math`-safe helper while finite zero/negative dimensions remain empty
  geometry. `SDL_RenderGeometryRaw` can also forward `indices == NULL` with a
  positive `num_indices` (normalizing `size_indices` to zero), so
  `QueueGeometry` now rejects that malformed combination without rejecting the
  valid unindexed `NULL/0/0` form.
- `test/dreamcast/test/pvr_copy_ex` provides three pages: source rectangles,
  logical edges, one-pixel strips, scaling, and filtering; all flips and
  0/90/180/270/45-degree rotations with default/explicit centers and cropped
  combinations; and mirrored viewport panels for clipping, modulation/alpha,
  and painter order. It also exercises unindexed and 8/16/32-bit public
  geometry plus required invalid-input failures. Its API prechecks now also
  cover non-finite rectangle width/height, continued success of finite empty
  rectangles and valid unindexed geometry, and null indices with a positive
  index count.
- A fresh unified SDL build and a PVR-disabled build pass, and all existing
  native-PVR tests rebuild with strict warnings against the explicit local
  archive.

**Physical-hardware validation (2026-07-28):** Passed on a real Dreamcast at
640x480 VGA. The capture ELF was uploaded over BBA with
`kos-tool -t 192.168.0.128 -x test/dreamcast/test/pvr_copy_ex/pvr_copy_ex.elf
-m .`. It wrote three PPMs, printed PASS, returned zero, and returned control
normally to `dc-load-ip`. Direct inspection shows page 1
has correct source/scaling/filter behavior and clean logical backing edges;
page 2 has all flip combinations, clockwise rotations, correct default/corner
centers, and cropped combinations; page 3 has identical viewport panels,
exact clip guards, modulation/alpha, and painter order. No boundary or padding
leakage, cracks, seams, or missing triangles are visible. The 640x480 lasting
references and SHA-256 values are:

- `test/dreamcast/test/pvr_copy_ex/pvr_copy_ex-hardware-page1.ppm` —
  `120aa843cd10c259f5c67ed852fab6eee08fd22a882bbba09edf1f9da3f56c93`
- `test/dreamcast/test/pvr_copy_ex/pvr_copy_ex-hardware-page2.ppm` —
  `c4580265159a1dc529fd45427a98d87a007ed6569530f37161cc229308b9d528`
- `test/dreamcast/test/pvr_copy_ex/pvr_copy_ex-hardware-page3.ppm` —
  `787660b80b92933242117868f327626a532639c40a9909cd69e812e6665dcb66`

The ordinary ELF was rebuilt after capture. It contains no
`vid_screen_shot` reference, hardware `/pc` path, or capture define.
The later source-validation corrections are queue-time rejection only and do
not change valid command storage or PVR submission. The unified archive and
focused strict-warning renderer/test builds were repeated; no physical-hardware
rerun was needed, and the three existing hardware PPMs and SHA-256 values
remain unchanged.

### Phase 3.3: texture lifecycle stress and VRAM recovery — done

- Source and SDL-core revalidation confirmed the researched ownership model.
  Driver data, CPU staging, and VRAM each have one cleanup path; failed create
  leaves `texture->driverdata` unset; locked destruction clears state; active-
  scene destruction defers cleanup until synchronization; the header cache is
  invalidated before free; SDL destroys renderer-associated textures before
  the backend renderer; and failed PVR initialization does not perform invalid
  frees. No lifecycle or rollback defect was found, so normal ownership code
  was not redesigned.
- A test-only `DC_PVR_TEXTURE_FAILURE_INJECTION` guard adds private stage and
  countdown selection through `SDL_DC_PVR_TEST_ALLOC_STAGE` and
  `SDL_DC_PVR_TEST_ALLOC_COUNTDOWN`. It can fail the Nth driver-data, CPU-
  staging, or VRAM allocation immediately before that native allocation.
  Every injected `SDL_CreateTexture` failure identifies its stage and is
  followed by successful normal create/update/draw/destroy use. Ordinary
  builds compile out the state, branches, hints, and injected-error strings.
- `test/dreamcast/test/pvr_texture_stress` runs 50 static cycles, 20 streaming
  cycles, 12 simultaneous mixed-lifetime textures, four locked destroys,
  a texture draw followed by SDL's pre-destruction queue flush and synchronized
  cleanup, renderer teardown with five associated live textures, two renderer
  recreations, deterministic failures at all three allocation stages, and
  VRAM exhaustion/recovery. Final balanced accounting is 180 successful
  creates/destroys, 5 expected failures, 192 updates, 115 draws, 84 locks, 80
  unlocks, 4 locked destroys, and 20 unlock-without-lock no-ops.
- Exhaustion uses uniform 256x256 RGB565 textures with 131,072 VRAM bytes per
  allocation and a safety cap of 64. Physical hardware reached 45 initial
  allocations (5,898,240 represented bytes), freed 23, recovered 23, and then
  reported the expected VRAM allocation failure again.
  Three separately retained textures remained drawable, and normal texture
  creation plus renderer recreation succeeded afterward. Exact allocator-byte
  restoration was not measured because the standard KOS build lacks
  `PVR_KM_DBG`; successful uniform reallocation is the recovery evidence.
- Fresh unified, injection-enabled, and PVR-disabled builds pass. The changed
  renderer unit and focused test pass `-Wall -Wextra -Werror`, all existing
  native-PVR tests rebuild against the explicit ordinary local archive, and
  the opt-out archive contains no native-PVR objects or symbols. The ordinary
  archive and ELF contain no active injection or capture support.
- Real 640x480 VGA hardware reaches PASS with the expected counts. KOS prints
  its generic out-of-PVR-memory/heap-probably-corrupted diagnostic at
  the two intentional exhaustion points; no other lifecycle warning appears.
  Hardware returned zero to `dc-load-ip`. The final 640x480 framebuffer is
  `test/dreamcast/test/pvr_texture_stress/pvr_texture_stress-hardware.ppm`,
  SHA-256
  `ea26ac35a8664399f01fb8e64bd505de16b013b783a3449717986eca26996c7a`.
  The three format-specific gradient panels have intact red/green/blue borders
  with no stale tiles, corruption, blanking, or seams.

### Phase 3.1 code review (2026-07-28, independent audit)

An independent, non-modifying code review of the Phase 3.1 implementation
followed AGENTS.md, PVR_TEXTURE_RESEARCH.md, and the test README as
authoritative references. Eleven review priorities were evaluated: source
split/linkage, SDL core changes, texture creation/ownership, pixel
formats/PVR state, UpdateTexture, Lock/UnlockTexture, filtering/header cache,
command architecture, build policy, test quality, and repository hygiene.

**Verdict:** The Phase 3.1 implementation is architecturally sound and
behaviorally correct for its documented scope. One HIGH defect requires a fix
before merge; two MEDIUM items are tracked for later phases.

**HIGH — `IsSupportedBlendMode` contract change in `src/render/SDL_render.c`**

**Status: FIXED (2026-07-28).** See "Resolution" below.

The SDL_render.c change routes ALL blend mode checks to the backend callback
(`renderer->SupportsBlendMode`) whenever it is installed, inside an
`#if SDL_VIDEO_RENDER_DREAMCAST_PVR` guard. The guard restricts execution to
opt-in DC PVR builds. The guard does **not** restrict dispatch to the PVR
renderer: on this SDL branch, GLES, GLES2, GL, D3D9, D3D11, D3D12, Metal, and
VITA_GXM all install `SupportsBlendMode`. Those backends happen to return
`SDL_TRUE` for the standard blend modes, so no regression exists today. But the
change demotes SDL's documented "required blend modes" guarantee (NONE/BLEND/
ADD/MOD/MUL) to a soft convention: a future backend that installs
`SupportsBlendMode` but does not handle NONE/BLEND correctly will silently
reject "required" modes.

**Why the initially documented replacement was insufficient:** The proposed
fix kept `#if SDL_VIDEO_RENDER_DREAMCAST_PVR` around the required-mode arm,
which restricts the build, not the dispatch target. Every renderer with a
callback still routed through its callback for NONE/BLEND/ADD/MOD/MUL in a DC
PVR build. The contract-demotion problem persisted for the required modes; only the custom-mode arm was actually fixed.

**Resolution:** Only consult the PVR callback when the renderer's identity
matches the native PVR renderer. A centralized constant (`DC_PVR_RENDERER_NAME`
in `src/render/dreamcast/SDL_render_dreamcast_pvr.h`) is the single source of
truth for the advertised name. The fix compares `renderer->info.name` against
that constant and routes the required-mode check through the callback only
when they match. Non-PVR renderers keep the SDL guarantee for required modes.
Custom modes continue to consult any installed callback regardless of renderer.

```c
static SDL_bool IsSupportedBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    switch (blendMode) {
    case SDL_BLENDMODE_NONE:
    case SDL_BLENDMODE_BLEND:
    case SDL_BLENDMODE_ADD:
    case SDL_BLENDMODE_MOD:
    case SDL_BLENDMODE_MUL:
#if SDL_VIDEO_RENDER_DREAMCAST_PVR
        if (renderer->info.name &&
            SDL_strcmp(renderer->info.name, DC_PVR_RENDERER_NAME) == 0 &&
            renderer->SupportsBlendMode) {
            return renderer->SupportsBlendMode(renderer, blendMode);
        }
#endif
        return SDL_TRUE;

    default:
        return renderer->SupportsBlendMode && renderer->SupportsBlendMode(renderer, blendMode);
    }
}
```

**Files changed:**
- `src/render/SDL_render.c` — `IsSupportedBlendMode` (required-mode arm now
  identity-gated); `SDL_SetTextureScaleMode` guard now uses `DC_PVR_RENDERER_NAME`
  instead of the raw literal.
- `src/render/dreamcast/SDL_render_dreamcast_pvr.h` — added `DC_PVR_RENDERER_NAME`
  constant and a comment documenting the name-dependency contract.
- `src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c` — removed unreachable
  `!data` branch from `DC_PVR_SetTextureScaleMode`.

**Validation:**
- Opt-in Dreamcast SDL build: clean (no warnings from changed files).
- Opt-out Dreamcast SDL build: clean (no `DC_RenderDriver` or `DC_PVR_*` symbols).
- `pvr_viewport`, `pvr_clipping`, `pvr_points_lines` build: clean.
- Physical hardware revalidation: unnecessary. The correction only changes the
  blend-mode API precheck path; PVR header generation and drawing behavior
  are unchanged.

**MEDIUM — `DC_PVR_UpdateTexture` uploads full POT backing on every partial update**

The implementation copies the requested rectangle into staging, then calls
`pvr_txr_load(data->pixels, data->vram, data->allocation_size)` — the full
allocation — regardless of which rectangle was updated. A 1×1 partial update
still transfers `backing_w × backing_h × 2` bytes. This is documented as
Phase 0/3.1 intentional behavior. No fix required in the focused copy/copy-ex
increment; partial uploads remain a later texture-optimization task.

**MEDIUM — OpenGL diff should be separated**

`src/render/opengl/SDL_render_gl.c`, `SDL_dreamcastopengl.c`, `SDL_dreamcastvideo.c`,
and `test/testutils.c` changes are unrelated to Phase 3.1. Split them into a
distinct commit (or revert) before PVR merge.

**LOW — `DC_PVR_SetTextureScaleMode` error path is silent**

`SDL_SetError` is called when `data` is NULL but the function is `void` and the
caller reports success. The `!data` branch is unreachable (driverdata is set in
`CreateTexture` and NULLed in `DestroyTexture`, which no-ops the scale mode
callback). Drop the branch or convert the function to `int`.

**LOW — `SDL_SetTextureScaleMode` guard uses string compare on `renderer->info.name`**

The guard compares against the advertised renderer name "Dreamcast PVR". If
the name string changes, the guard silently stops working. Add a comment noting
the name dependency.

**Test gaps (tracked, not blocking)**

- No 320×240 or PAL/NTSC mode coverage (Phase 7 hardening)
- Safe below-boundary TA command-capacity stress now passes at 81.6-84.6% on
  640x480 VGA hardware with retained framebuffer captures. Deliberate boundary
  probing and non-VGA TA/OPB characterization remain Phase 7 hardening.

**Repository hygiene**

- Rebuilt `.bin` / `.elf` / `.log` / `.img` files in `test/dreamcast/test/` are
  build artifacts and should be gitignored or excluded from the PVR patch.
- `AGENTS.md` grows by ~850 lines; consider moving detailed phase histories to
  separate files under `src/render/dreamcast/`.

**Unresolved questions**

- `pvr_txr_load` alignment: KOS header says `count` must be a multiple of 32.
  The implementation passes `backing_w × backing_h × 2`, which is always ≥ 128
  and a multiple of 32 for POT dimensions ≥ 8. No action required.
- `pvr_txr_load` cache flush: KOS synchronous load path does not require
  explicit flushing. Verified by physical-hardware passes.
- UV scaling for partial `RenderCopy`: SDL computes UVs normalized to
  `texture->w`/`h` (logical). Backend rescales to `logical/backing`. For a
  partial source rect `srcrect={5,3,4,3}` on a 17×9 logical texture backed
  32×16: UVs become `{5/32, 3/16, 9/32, 6/16}` — correct sampling of the
  logical partial within the POT backing.

### Phase 3.4: wider texture dimensions and logical edges — done

- Added `test/dreamcast/test/pvr_texture_matrix`, a four-page public-API test
  with 28 sequential logical sizes covering representative axes from 1 through
  513, POT transitions, one-pixel axes, asymmetric shapes, 320x240, 321x241,
  and a 513x257 logical texture with 1024x512 backing. Every native format is
  exercised across multiple transitions with static and streaming access.
- The test verifies expected streaming backing pitch, tight/larger/odd source
  pitch, corners, rows, columns, interior and logical-edge updates, partial
  locks, repeated updates, invalid SDL/core and backend cases, filtering,
  alpha, and update/draw ordering. Textures are sequentially destroyed so the
  matrix does not duplicate the Phase 3.3 exhaustion test.
- The matrix exposed a real renderer ordering defect: SDL's texture flush had
  submitted earlier geometry into an active PVR scene, but an immediate update
  overwrote the same VRAM before that scene consumed it. The renderer now marks
  allocations referenced by submitted texture headers. Updating or locking a
  referenced allocation synchronously uploads the complete POT backing to
  replacement VRAM, invalidates the header cache, and defers the old allocation
  until presentation synchronization. Unreferenced allocations upload in
  place. This is not partial-upload support.
- The unified SDL build and all eight prior native-PVR tests rebuild with
  strict test warnings against the explicit local archive. Both
  `GL_RenderDriver` and `DC_RenderDriver` are present; native renderer objects
  have no unresolved `gl*` calls. A fresh opt-out archive has no native-PVR
  object or symbol. Its warnings are pre-existing in excluded Dreamcast
  video/GLdc files.
- Physical 640x480 VGA hardware passes the same counts, captures four distinct
  640x480 PPM pages, and returns zero normally to `dc-load-ip`. Direct
  inspection confirms correct logical corners, one-pixel dimensions,
  right/bottom edges, NPOT padding isolation, pitch and partial-lock behavior,
  old/new update ordering, nearest/bilinear filtering, and ARGB1555/ARGB4444
  alpha. Capture paths and SHA-256 values are recorded in the focused README.
  The ordinary ELF was rebuilt without capture references.
- Full-backing synchronous uploads remain intentional. Partial uploads, DMA,
  custom store-queue row transfers, and read-modify-write logic remain
  deferred. Non-VGA modes and broader capacity/performance profiling remain
  later hardening.

## Phase 4: geometry and blending completeness

- Handle unindexed geometry and 8-, 16-, and 32-bit indices.
- Convert triangle lists to valid PVR strips, ending each independent primitive
  with `PVR_CMD_VERTEX_EOL`.
- Interpolate position, UV, and vertex color when clipping polygons.
- Map only blend modes representable by PVR source and destination factors.
- Validate `NONE`, `BLEND`, `ADD`, and `MUL` independently; keep `MOD`
  rejected until an exact implementation exists.
- Reject unsupported equations or separate-alpha combinations through
  `SupportsBlendMode`; do not silently approximate them.

Acceptance gate: `testgeometry`, `testscale`, `testrendercopyex`, and sprite
stress tests closely match the OpenGL renderer.

### Phase 4.1: RGB565 screen ADD/MUL blending — done

- `SDL_BLENDMODE_ADD` maps to `PVR_BLEND_SRCALPHA / PVR_BLEND_ONE` and
  `SDL_BLENDMODE_MUL` maps to
  `PVR_BLEND_DESTCOLOR / PVR_BLEND_INVSRCALPHA`. `SupportsBlendMode` accepts
  both alongside `NONE` and `BLEND`; `MOD` and unsupported custom equations
  remain rejected.
- The mappings are validated only for the current RGB565 screen destination.
  ADD is not exact for stored alpha, and neither result extends to render
  targets until Phase 5 supplies a focused target test.
- `test/dreamcast/test/pvr_blend_add_mul` presents and captures four independent
  pages covering CPU RGB565 reference pairs, alpha endpoints and saturation,
  textured modulation/filtering, blend-state cache transitions, nonzero
  viewport translation, genuine overlapping painter order, and per-vertex
  textured/untextured geometry.
- **Physical-hardware validation (2026-07-29):** Passed on a real Dreamcast at
  640x480 VGA. All four raw framebuffer captures were directly inspected; CPU
  and native reference samples agreed exactly or within one RGB565 channel
  step. Geometry, guards, viewport placement, filtering, modulation, gradients,
  and overlap order were intact with no corruption, missing primitive, seam,
  freeze, reset, failed capture, or stale state. The program printed `PASS`,
  returned zero, and returned control to `dc-load-ip`. Capture paths, hashes,
  and the serial log are recorded in the focused README.

## Phase 5: render-to-texture

Do not advertise `SDL_RENDERER_TARGETTEXTURE` before this phase passes.

Use `pvr_scene_begin_rtt`, not deprecated `pvr_scene_begin_txr`. The current KOS
implementation expects 16-bit targets, nonzero dimensions, a backing stride at
least as wide as the render area, and a stride divisible by four pixels.

A target switch must:

1. prevalidate the new target before changing usable backend state;
2. flush and finish the active scene;
3. record the new destination without beginning an empty scene;
4. begin the next screen or RTT scene lazily from command execution;
5. consume SDL's restored target dimensions, viewport, and clipping state;
6. handle target texture orientation consistently; and
7. synchronize at presentation/resource-release boundaries, adding a target
   switch stall only if physical evidence requires one.

Use GLdc's framebuffer implementation as the local reference for RTT scene
transitions, backing dimensions, and orientation.

Acceptance gate: screen/target switching, sampling a completed target,
non-screen-sized targets, target viewport/scissor, and repeated target
recreation work on physical hardware with retained screenshots.

### Phase 5.1: guarded RTT prototype — rejected on hardware

- A private `DC_PVR_RENDER_TARGET_PROTOTYPE` build implements RGB565 target
  allocation, lazy target-aware scene ownership, target sampling, feedback
  rejection, and prototype-only capability advertisement. The ordinary build
  remains isolated and does not advertise target textures.
- The corrected ten-page test passes 10 API checks and returns zero on a real
  Dreamcast at 640x480 VGA, but raw hardware captures reject the simple scene
  design. Screen S1 is lost across screen -> RTT -> screen, target T1 is lost
  after target reselection, and target T1 is also lost when
  `SDL_RenderPresent` finishes a scene while that target remains selected.
  Page 9 independently shows earlier screen content disappearing after later
  RTT lifecycle transitions.
- The NPOT guard capture also shows periodic holes along the sampled target's
  extreme bottom/right edges. Diagnose this separately from destination
  persistence; possible sources include exact UV endpoints, wrap behavior,
  RTT padding/stride, or raster edge ownership.
- There was no freeze, reset, timeout, crash, or failed API recovery. The
  authoritative failure is missing prior destination contents and incomplete
  extreme-edge presentation in the physical framebuffer captures.
- Do not promote or commit the prototype as completed Phase 5 functionality.
  A bounded physical microprobe has since validated an offscreen logical-screen
  plus current/next target seed architecture. At 640x480 the two POT-backed
  screen allocations consume about 2 MiB, leaving 3,845,800 bytes of the
  current PVR texture heap; a completed preservation scene averages 6.680 ms.
  The same probe confirms that enlarged target sampling must map its maximum UV
  to `(logical - 1) / backing`, not `logical / backing`; 1:1 controls prove KOS
  clipping and RTT edge writes are intact. Full results are in
  `PVR_RENDER_TARGET_PRESERVATION_RESEARCH.md`.
- A guarded second coding pass is now feasible, but it must implement logical
  screen preservation and target preservation together. Keep ordinary target
  capability disabled until the integrated original persistence pages, edge
  matrix, repeated-copy fidelity, allocation rollback, and physical screenshot
  gates pass.

The serial log and all ten raw captures are under
`test/dreamcast/test/pvr_render_target/`. Full evidence and reviewed prototype
corrections are recorded in `src/render/dreamcast/PVR_RENDER_TARGET_RESEARCH.md`.

### Phase 5.2: guarded preservation prototype — hardware validated

- The second private `DC_PVR_RENDER_TARGET_PROTOTYPE` pass adds paired RGB565
  allocations for each target and a paired POT-backed logical screen. Every
  successful RTT scene commits centrally by finishing its list and scene,
  swapping current/next, and marking the new current valid. Resumed
  destinations seed current into next before later SDL commands. Physical
  presentation copies the completed logical screen without changing its
  ownership.
- Preservation seeds use proper 32-byte `pvr_vertex_t` commands and the
  physically validated maximum UV `(logical - 1) / backing`. Prototype-owned
  screen VRAM is released before synchronized PVR shutdown; failed recovery
  shutdown deliberately abandons allocator pointers rather than freeing them
  after the allocator is gone. Target allocation rollback frees the first
  allocation if the second step fails.
- The final 12-page physical run passed on a real Dreamcast at 640x480 VGA.
  It reported 10 API checks with zero failures, captured every raw framebuffer,
  returned zero to `dc-load-ip`, and showed no freeze, reset, timeout, crash,
  missing geometry, corruption, or failed recovery. Screen persistence,
  A/B target switching, target reselection, target-selected
  `SDL_RenderPresent`, NONE/BLEND/ADD/MUL, filtering/modulation, feedback
  rejection, selected-target destruction, NPOT borders, repeated preservation,
  and allocator lifecycle recovery all rendered as expected.
- The authoritative final serial log is
  `test/dreamcast/test/pvr_render_target/hardware-20260729-150808.log`; retained
  raw captures are `pvr_render_target_phase5b_page1.ppm` through
  `pvr_render_target_phase5b_page12.ppm` under the same test directory. The
  earlier `150242` failure and `150436` test-layout run remain preserved as
  diagnostic evidence.
- This validates the guarded prototype design. Keep ordinary-build capability
  isolation until the project explicitly chooses promotion and completes any
  desired wider VRAM-pressure, video-mode, and performance gates.
- The bounded VRAM-pressure follow-up also passes on physical hardware. With
  the paired logical screen already allocated, `pvr_mem_available()` reports
  3,845,800 bytes. Fourteen paired 256x256 targets succeed; allocation 15
  fails cleanly with an exact partial-allocation rollback. Freeing all targets
  restores the exact baseline. Ten repeated exhaust/free cycles show no
  accumulating loss, 24 mixed-size allocations survive reverse destruction,
  and a subsequent target renders normally. The final recovery screenshot is
  intact and the program returns zero. Evidence is under
  `test/dreamcast/test/pvr_render_target_stress/`.
- The focused performance comparison also completed on physical 640x480 VGA
  hardware. Against the ordinary renderer, the guarded prototype changes
  screen-only mean frame time from 16,678 to 22,236 us for 100 rectangles,
  16,678 to 33,356 us for 200 texture copies, and 33,361 to 50,039 us for the
  400-draw workload. Effective rates fall from 59.956 to 44.970 FPS, 59.956
  to 29.978 FPS, and 29.974 to 19.984 FPS respectively. Clear-only remains
  effectively unchanged. Available VRAM falls from 5,943,040 to 3,845,800
  bytes, a 2,097,240-byte reduction. Both reference captures are visually
  correct and both programs return zero normally.
- This performance gate rejects promotion of the current always-on
  logical-screen preservation architecture. Keep
  `DC_PVR_RENDER_TARGET_PROTOTYPE` and ordinary capability isolation. A future
  design must avoid imposing the paired logical-screen RTT cost on ordinary
  screen-only rendering, or define an explicit opt-in policy. Authoritative
  logs, captures, workload definitions, and limitations are under
`test/dreamcast/test/pvr_render_target_performance/`.

### Phase 5.3: guarded ordered RTT implementation — hardware validated

- A private `DC_PVR_ORDERED_RENDER_TARGETS` build implements the lower-cost
  Dreamcast ordering contract: all texture-target work precedes the final
  physical-screen pass. The screen renders directly to the framebuffer; this
  path allocates no logical-screen current/next pair and performs no final
  logical-screen copy. Individual RGB565 targets retain paired current/next
  allocations and the validated preservation seed/swap behavior.
- The experimental `SDL_HINT_DC_PVR_RENDER_TARGETS` creation-time opt-in is
  disabled by default. Opted-in renderer instances install the target
  callbacks and report `SDL_RENDERER_TARGETTEXTURE`. The static driver
  description intentionally remains unchanged, so automatic driver selection
  requested with `SDL_RENDERER_TARGETTEXTURE` cannot discover the experimental
  instance capability; applications must select the Dreamcast PVR renderer
  explicitly during this guarded increment.
- A Dreamcast-only pre-transition callback in `SDL_SetRenderTarget` flushes
  queued work, validates ordering, and prepares the backend transition before
  SDL mutates its target, viewport, or clip state. After the first executed
  screen clear or nonempty screen draw command, a non-NULL target switch fails
  with a precise SDL error until a successful screen-selected present. Flushes
  and state-only commands are not frame boundaries or screen render intent.
- Ordered mode begins PVR scenes lazily at the first clear or drawable command.
  This permits target -> screen state-only -> target transitions without an
  empty physical scene. A target-selected present finishes target work, sets
  and logs an error, does not present or reset ordering, and remains recoverable
  by selecting the screen and completing a normal screen frame.
- `test/dreamcast/test/pvr_ordered_render_target` is the five-page focused gate
  for allowed A/B ordering, preservation, clear/geometry/flush rejection,
  batched and non-batched recovery, target-selected present, selected-target
  destruction, incomplete-frame destruction, exact paired allocation cost,
  and injected second-allocation rollback. The performance comparison also has
  an ordered build for ordinary screen-only and RTT workloads.
- Static Dreamcast builds pass for ordered, ordinary, full-preservation
  prototype, and native-renderer opt-out configurations. The focused test and
  ordered performance test compile with `-Wall -Wextra -Werror`.
- **Physical-hardware validation (2026-07-29):** Passed on a real 640x480 VGA
  Dreamcast. The focused test reported 23 checks and zero failures, captured
  all five pages, and returned zero. All ordering rejection, recovery,
  target-preservation, selected-present, lifecycle, and allocation checks
  passed. The ordered renderer retained 5,943,040 bytes after creation; its
  target pair consumed 65,624 bytes including KOS allocator metadata, and the
  injected failed allocation showed no payload leak.
- The ordinary and ordered performance ELFs both returned zero and captured
  their reference screens. Both reported 5,943,040 bytes available. Screen-only
  means for A-D were respectively 16,679/16,678/16,678/33,361 us in both
  builds, with ordered target workloads E/F at 16,679/16,678 us. The ordered
  reference adds only its expected sampled-target panel; shared screen content
  matches the ordinary capture. This validates the low-cost path for the
  tested 640x480 VGA mode. Wider video-mode, VRAM-pressure, and stress coverage
  remain Phase 7 hardening, and the mode remains experimental rather than
  default-enabled.

## Phase 6: Dreamcast-specific optimization

After baseline SDL behavior is stable, add and benchmark:

- direct `.dt` loading;
- twiddled textures;
- VQ-compressed textures;
- mipmapped `.dt` assets where SDL semantics permit;
- strided NPOT textures;
- DMA uploads;
- opaque and punch-through list routing; and
- state-header and draw-call batching.

Strided textures require special care because KOS exposes one global stride
width for all strided textures used in a frame. Textures with different stride
widths cannot be batched naively. Power-of-two padding should remain the safe
default.

### Phase 6.1: direct `.dt`, twiddled, VQ, and mipmapped textures — done

- The Dreamcast-only `SDL_LoadDreamcastTexture_RW()` path now validates the
  `DcTx` container once and dispatches preformatted payloads to either renderer
  through a callback compiled only under `__DREAMCAST__`. Shared SDL changes
  are guarded; non-Dreamcast renderer layout and texture allocation remain
  unchanged after preprocessing.
- The native renderer directly uploads twiddled RGB565, ARGB1555, and ARGB4444
  payloads, full and reduced-codebook VQ, and square VQ mip chains. It retains
  the true allocation base for destruction while applying the KOS reduced-VQ
  sample-address adjustment. Normal SDL textures remain nontwiddled.
- The GLdc-backed renderer retains loader support. Its Dreamcast-only callback
  expands reduced codebooks to GLdc's conventional 2 KiB layout and preserves
  mip-aware minification when SDL nearest or linear scale mode is selected.
  Loader-created preformatted textures are static and immutable.
- `test/dreamcast/test/pvr_dt_texture` generates all six valid assets with the
  active KOS `pvrtex`, covers malformed headers and payloads, performs twenty
  load/destroy cycles, and builds native and OpenGL ELFs with strict warnings.
- **Physical-hardware validation (2026-07-29):** Both 640x480 VGA runs passed
  15 checks with zero failures, captured unique raw framebuffers, returned
  zero, and returned normally to dc-load-ip. Direct comparison shows all six
  panels intact and corresponding across native PVR and GLdc: three twiddled
  uncompressed formats, full VQ, reduced-codebook VQ, and VQ mipmaps. Full
  design, limitations, commands, logs, capture paths, and hashes are in
  `src/render/dreamcast/PVR_DT_TEXTURE_RESEARCH.md` and the test README.

Strided `.dt` and partial `.dt` layouts, palettes, bump maps, YUV, DMA, list
routing, and batching remain later work. The renderer-created RGB565 strided
increment is documented in Phase 6.3 below. This increment does not promote
the guarded render-target prototype or change renderer capability advertising.

### Phase 6.2: strided NPOT texture research and microprobe — done

- KOS exposes one frame-global `PVR_TEXTURE_MODULO` value through
  `pvr_txr_set_stride()`/`pvr_txr_get_stride()`. Legal widths are multiples of
  32 through 992. Strided RGB565 textures use `PVR_TXRFMT_X32_STRIDE`, retain
  POT header dimensions, and allocate/upload `stride * backing_height * 2`.
- `test/dreamcast/test/pvr_strided_texture_microprobe` is a standalone public
  KOS/PVR probe with no SDL, OpenGL, or GLdc dependency. Its ten bounded pages
  cover nearest/bilinear sampling, ordinary POT coexistence, same- and
  different-stride pairs, opaque/transparent list boundaries, consecutive
  frames, padded-pitch updates, logical edge probes, and exact VRAM recovery.
- **Physical-hardware validation (2026-07-29):** Passed on a real Dreamcast at
  640x480 VGA. All programmed strides read back exactly, ten unique raw PPM
  captures succeeded, VRAM returned to the post-allocator-warmup baseline of
  5,789,416 bytes, PVR shut down cleanly, and dc-load reported return zero.
- Same-stride textures render correctly together. Ordinary POT textures render
  correctly alongside strided textures and ignore the global stride register.
  A narrower logical texture can share a wider physical stride when allocation,
  upload pitch, and UV extent remain distinct.
- Different strided widths cannot coexist in one frame. Page 5 changes from
  640 to 384 within one list; page 6 changes across opaque and transparent
  list boundaries in one scene. Both captures show the earlier 640-pitch panel
  corrupted and the final 384-pitch panel correct. A list boundary does not
  latch an independent stride. Changing stride between completed frames works.
- Implementation policy is one active strided width per frame/scene,
  same-stride batching, and ordinary POT fallback for conflicting strided
  widths. Strided `.dt`, DMA, the full 32–992 width range, other video modes,
  and capacity/stress coverage remain later work; other renderer-created
  16-bit formats were completed by Phase 6.3.
- The source-backed design, hardware findings, capture paths, and limitations
  are recorded in `src/render/dreamcast/PVR_STRIDED_TEXTURE_RESEARCH.md` and
  the microprobe README. No SDL renderer functionality changes in this phase.

### Phase 6.3: fixed-class strided renderer integration — done

- The guarded native PVR renderer selects an immutable layout at texture
  creation for ordinary static and streaming RGB565, ARGB1555, and ARGB4444
  textures. A renderer-wide
  stride class is derived from the output width; textures that cannot use it
  safely or economically remain POT-backed automatically. No duplicate
  strided/POT copies are retained.
- Strided textures use `PVR_TXRFMT_X32_STRIDE`, the selected row pitch, and
  POT header dimensions. Existing full/partial updates, streaming locks,
  replacement VRAM, deferred destruction, logical/POT UV scaling, and POT
  coexistence are preserved. Render-target, `.dt`, twiddled, VQ, and
  mipmapped textures remain on their existing paths.
- The frame stride is programmed and read back before the first strided
  header, remains stable through the presented frame, and resets only after a
  successful `SDL_RenderPresent`. PVR list boundaries and flushes do not reset
  it.
- `test/dreamcast/test/pvr_strided_textures` covers fixed-class static and
  streaming textures, padded-pitch updates, POT coexistence, and capture
  output. The strict ordinary and screenshot builds pass.
- **Physical-hardware validation (2026-07-30):** Passed on a real 640x480
  VGA Dreamcast. The screenshot ELF returned zero, wrote
  `/pc/pvr_strided_textures-phase6_2.ppm`, and direct inspection showed intact
  strided panels, the yellow partial update, and an undistorted POT checker.
  The validated capture hash is
  `ab508f5857657c03aab712041aeb2472428bd8f783c3c08851d36f1c6e0877b3`.
- The implementation, design boundary, allocation equations, and remaining
  limitations are recorded in
  `src/render/dreamcast/PVR_STRIDED_TEXTURE_INTEGRATION_RESEARCH.md`.
- **Alpha-format hardware validation (2026-07-30):** ARGB1555 and ARGB4444
  static and streaming textures passed blend, modulation, mixed-scene, and
  fixed-class streaming-pitch checks on real 640x480 VGA hardware. Six raw
  PPMs and normal lifecycle completion are retained in
  `test/dreamcast/test/pvr_strided_textures/`.
- Natural per-texture stride selection, strided `.dt`, DMA, wider video modes,
  and capacity/stress coverage remain Phase 7 or later work.

## Phase 7: hardening and default enablement

- Test VRAM exhaustion and allocation rollback.
- Verify renderer creation failure leaves the video subsystem usable.
- Stress repeated texture creation, destruction, locking, and updating.
- Test 320x240 and 640x480 modes and PAL, NTSC, and VGA where available.
- Record frame time, upload time, VRAM use, polygon count, and list overflow.
- Compare the native renderer with GLdc using many small sprites, a full-screen
  streaming texture, an alpha-heavy UI, geometry-heavy scenes, and RTT.
- Enable the backend by default only after it is at least as reliable as the
  software renderer and reports unsupported capabilities accurately.

## Source file organization

### Current layout (through Phase 2)

The existing two-file implementation remains acceptable through the completed
Phase 2 work:

```
src/render/dreamcast/
├── SDL_render_dreamcast_pvr.c
├── SDL_render_dreamcast_pvr_data.h
```

The main renderer file is approximately 38 KB and is not unreasonably large
compared with other SDL hardware backends such as PSP. File size alone is not
a reason to refactor.

### Recommended future layout

Phase 3 texture support introduces a distinct subsystem involving texture
format mapping, CPU staging allocations, VRAM allocation and ownership, logical
and backing dimensions, pitch calculations, partial updates, lock and unlock
behavior, dirty/upload state, UV scaling, texture filtering, synchronous
uploads, future DMA uploads, and future render-target texture state. The
recommended future organization is:

```
src/render/dreamcast/
├── SDL_render_dreamcast_pvr.c
├── SDL_render_dreamcast_pvr.h
├── SDL_render_dreamcast_pvr_geometry.c
├── SDL_render_dreamcast_pvr_texture.c
└── SDL_render_dreamcast_pvr_data.h
```

This layout is a direction, not an immediate mandatory rename. The exact
private-header arrangement should be chosen when the refactor occurs.

#### Intended responsibilities

`SDL_render_dreamcast_pvr.c` should retain:

- renderer creation and destruction;
- callback registration;
- high-level `RunCommandQueue` dispatch;
- scene and polygon-list lifecycle;
- `RenderPresent`;
- blend capability reporting;
- renderer-level PVR ownership;
- transitions between major renderer states.

`SDL_render_dreamcast_pvr_geometry.c` should eventually contain:

- effective geometry bounds;
- Sutherland–Hodgman clipping;
- intersection and ARGB interpolation;
- deterministic fan-anchor selection;
- clipped polygon and triangle emission;
- quad expansion;
- point and rectangle emission helpers;
- other geometry-specific helpers.

`SDL_render_dreamcast_pvr_texture.c` should contain:

- texture format translation;
- texture creation and destruction;
- logical and backing-size calculation;
- CPU staging and VRAM allocation;
- texture update;
- texture lock and unlock;
- synchronous upload;
- pitch and subrectangle handling;
- filtering state;
- dirty state;
- UV scaling;
- future upload optimizations.

A shared private header should eventually contain:

- internal renderer structures;
- texture structures;
- queued vertex or rectangle structures;
- geometry-bound structures;
- internal constants;
- declarations shared across implementation files.

`SDL_render_dreamcast_pvr_data.h` may eventually be replaced or complemented
by `SDL_render_dreamcast_pvr.h` when shared helper declarations are required.
Do not rename it preemptively.

#### Refactor timing

The source split should occur when one or more of these conditions becomes true:

- Phase 3 adds enough texture logic that lifecycle and geometry code become
difficult to navigate;
- the main C file approaches roughly 50–60 KB;
- static helper ordering becomes awkward;
- agents or maintainers repeatedly confuse texture, geometry, and lifecycle
  responsibilities;
- unrelated texture and geometry edits regularly conflict;
- render-to-texture work requires a clearer texture subsystem boundary.

Prefer splitting before or during Phase 3 implementation, but only after the
Phase 3 design is settled.

The refactor must be behavior-preserving and should not be mixed with
unvalidated functional changes where avoidable.

Do not imitate the software renderer's many-file rasterizer layout. The native
PVR backend should be separated by major subsystem, not into one file per
primitive.

## Viable v1 definition

A useful first release does not require RTT, YUV, VQ, or every custom blend
mode. It should provide:

- correct renderer and PVR lifecycle management (renderer-owns-PVR model);
- clear, points, lines, rectangles, copies, copy-ex, and geometry;
- viewport and pixel-exact clipping;
- RGB565, ARGB1555, and ARGB4444 textures;
- static and streaming texture access;
- nearest and linear filtering;
- standard color and alpha modulation;
- correct `NONE`, `BLEND`, `ADD`, and `MUL` where hardware mappings validate;
- stable physical-hardware behavior with retained screenshots; and
- no PVR ownership conflicts (single renderer, renderer-owns-PVR).

Keep ordering and scene ownership as the earliest technical gates. Defer PVR
format and batching optimizations until SDL rendering semantics are correct.
