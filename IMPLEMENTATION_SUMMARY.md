# Native Dreamcast PVR Renderer — Current Status

Updated 2026-09-13 from committed renderer state at `916df8fbf`.

## Status

The native Dreamcast PVR renderer is implemented through the validated Phase
6 increments. Ordered RGB565 render targets are enabled by default and the
renderer advertises `SDL_RENDERER_TARGETTEXTURE`.

All runtime claims below are based on physical Dreamcast testing at 640×480
VGA unless stated otherwise.

## Completed

- Renderer-owned PVR lifecycle, ordered command submission, clear, geometry,
  viewport translation, pixel-exact CPU clipping, points, lines, and blending.
- RGB565, ARGB1555, and ARGB4444 static and streaming textures, including
  partial updates, filtering, modulation, scaling, rotation, and flips.
- Texture lifecycle stress, VRAM exhaustion rollback, and replacement of
  scene-referenced texture allocations.
- Screen/target ordering and RGB565 render-target preservation using paired
  target allocations. Illegal screen-to-target transitions are rejected with
  recovery paths documented in `docs/README-dreamcast.md`.
- Direct `.dt` loading for twiddled 16-bit textures, VQ, reduced-codebook VQ,
  and VQ mipmaps.
- Fixed-class strided NPOT textures for renderer-created RGB565, ARGB1555,
  and ARGB4444 textures.
- Blocking texture-upload DMA with synchronous store-queue fallback.
- Guarded POT partial VRAM uploads.

The focused renderer tests and their retained hardware captures are under
`test/dreamcast/test/pvr_*`.

## Deliberately guarded or unsupported

- `SDL_DC_PVR_PARTIAL_UPLOAD` remains off by default.
- `SDL_DC_PVR_OPAQUE_PREFIX_ROUTING` remains off by default. Hardware testing
  showed a clear-only improvement but roughly 4% slower common rectangle
  workloads, with no capacity gain.
- Punch-through routing, arbitrary interleaved list routing, non-blocking or
  queued DMA, partial strided uploads, strided `.dt`, palettes, bump maps,
  YUV, and `MOD` blending remain unsupported or deferred.

## Remaining work

Phase 7 is hardening and measurement: broader PAL/NTSC characterization,
TA/OPB and VRAM stress, DMA-versus-store-queue profiling, and comparison with
GLdc on representative sprite, streaming, alpha-heavy, geometry, and RTT
workloads. These are not prerequisites for the already validated 640×480
renderer path.

The detailed phase and failure reports under `src/render/dreamcast/` remain
the authoritative historical evidence for individual experiments.
