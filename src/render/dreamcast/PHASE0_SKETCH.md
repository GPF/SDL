# Phase 0: Native PVR Implementation Contract

This document is subordinate to the repository `AGENTS.md`. Although the
filename is retained for continuity, it is no longer speculative pseudocode.
It records the implemented Phase 0 contract and validation results so later
phases do not regress native PVR ownership, SDL painter order, command
submission, or static 16-bit texture handling.

## Phase 0 contract

- The renderer initializes PVR with checked `pvr_init()` and shuts it down only
  when that initialization succeeded. The configuration enables only
  `PVR_LIST_TR_POLY`, selects direct submission, and disables translucent
  autosorting. The backend does not call `pvr_dma_init()` or any DMA transfer
  API; KOS receives `dma_enabled = 0`.
- The native renderer requires `SDL_DC_DIRECT_VIDEO`, a non-OpenGL window, no
  current OpenGL context, and a valid PVR-compatible output width. Failed
  validation occurs before PVR initialization. Unsupported render-target
  creation is rejected explicitly.
- A frame has one screen scene and one transparent polygon list. Every draw,
  including `SDL_BLENDMODE_NONE`, stays in that list and uses depth comparison
  `ALWAYS` with depth writes disabled. Command order is therefore submission
  order.
- Queue callbacks are CPU-only ingestion. `QueueGeometry` expands indices and
  copies position, color, and UV data into memory returned by
  `SDL_AllocateRenderVertices`. `QueueSetViewport` and `QueueSetDrawColor` are
  no-ops because their command structures already own the required state.
- `QueueCopy` and `QueueCopyEx` are unset. SDL translates copies, rotation, and
  flips into `QueueGeometry`, so their current callback signatures are avoided
  entirely. `QueueGeometry` includes the current branch's `scale_x` and
  `scale_y` parameters, and `SetTextureScaleMode` has its current `void`
  signature.
- Only `RunCommandQueue` begins the scene/list and emits PVR polygon headers and
  vertices. It walks SDL's command chain exactly once and handles clears and
  geometry in place. A clear is an untextured, full-output quad, so a clear
  appearing between draws remains ordered.
- `RenderPresent` never walks render commands. It ensures an empty frame still
  has a scene/list, finishes that list and scene, and waits for rendering to
  complete.
- KallistiOS PVR page flips are vblank-driven. The backend advertises fixed-on
  vsync, accepts `SetVSync(1)`, and rejects disabling it rather than claiming an
  unsupported unsynchronized presentation mode.
- The renderer advertises `SDL_RENDERER_ACCELERATED` and its fixed
  `SDL_RENDERER_PRESENTVSYNC` behavior. It does not advertise
  `SDL_RENDERER_TARGETTEXTURE` and has no render-target callback.

## Geometry data flow

`QueueGeometry` converts each indexed or unindexed triangle-list element into
one compact renderer-owned vertex:

```text
position (x, y), texture coordinate (u, v), packed ARGB color
```

The copied command data remains valid until `RunCommandQueue`. There are no PVR
calls in any queue callback. During execution, each independent group of three
vertices is emitted as a three-vertex strip whose third vertex is marked
`PVR_CMD_VERTEX_EOL`. Headers are recompiled only when texture or blend state
changes.

## Phase 0 textures

The directly supported static formats are:

- `SDL_PIXELFORMAT_RGB565` → `PVR_TXRFMT_RGB565`
- `SDL_PIXELFORMAT_ARGB1555` → `PVR_TXRFMT_ARGB1555`

Each texture owns a zero-initialized CPU backing allocation and a same-sized
VRAM allocation. The logical dimensions are preserved separately from the
power-of-two backing dimensions. Both stores use a 16-bit backing pitch.

For an update rectangle, the upload path:

1. validates the rectangle against the logical dimensions;
2. validates that caller pitch covers exactly `rect.w * 2` readable bytes;
3. copies only those bytes for each of `rect.h` rows into the appropriate CPU
   backing offset;
4. leaves POT padding zero-filled (or preserves it on partial updates); and
5. synchronously uploads the entire initialized backing store with
   `pvr_txr_load`.

This never assumes that caller rows extend into POT padding and never reads
past the supplied rectangle. For SDL's logical normalized source UVs, the
queued PVR UVs are calculated directly against the backing dimensions:

```text
pvr_u = (sdl_u * logical_width)  / backing_width
pvr_v = (sdl_v * logical_height) / backing_height
```

Nearest-neighbor filtering is fixed for Phase 0.

## Explicitly deferred

Pixel-exact clipping (viewport translation is now implemented in Phase 2.0),
points/lines edge behavior, streaming locks, bilinear filtering, additional
formats and blend modes, and render-to-texture are later phases. Phase 0
supports full-screen unclipped clears, geometry-generated solid rectangles,
`NONE`/`BLEND`, nearest textured copies, and viewport-offset translation for
both clears and geometry vertices.

## Build and validation

The CMake option is `SDL_VIDEO_RENDER_DREAMCAST_PVR`, disabled by default. When
enabled for Dreamcast, CMake adds the native renderer source explicitly.

The validation frame alternates opaque and alpha-blended overlapping
rectangles, then adds an overlapping RGB565 or ARGB1555 textured quad. Its
pixel ordering must match the SDL software renderer in Flycast and on hardware.
TA/list capacity should also be measured with several thousand sprites before
proceeding beyond the spike.

`test/dreamcast/test/pvr_lifecycle` repeatedly rejects an unsupported creation,
creates the native renderer, validates fixed-on vsync, allocates a texture,
and destroys the renderer. Alternating iterations present normally or flush
without presenting so destruction also covers an active scene and deferred
VRAM cleanup. After 32 stress iterations it creates one final renderer,
presents a stable blue rectangle with an overlapping red RGB565 textured
rectangle, and keeps PVR alive for a real five-second inspection interval.

The lifecycle test passes in Flycast and on physical Dreamcast hardware at
640x480 VGA. Brief red lines or blanking during the stress loop are expected:
each iteration deliberately shuts down PVR, which clears and reconfigures video
memory. The hardware visual gate is the stable final frame displayed during
the five-second hold; that frame renders correctly after the repeated
create/destroy cycle. TA/list-capacity stress testing remains outstanding.
