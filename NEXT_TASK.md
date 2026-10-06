# NEXT_TASK — SDL3 Dreamcast PVR renderer

## Current status

Phase 0 Gates 1–9 are complete on physical hardware. RGB565 render-target
preservation is promoted into normal PVR builds. The target path uses paired
`current`/`next` VRAM allocations for both the logical screen and RGB565 target
textures. Target switches commit only after successful scene completion and
render wait; sampling binds the authoritative current allocation; NPOT targets
retain the validated edge UV maximum. SDL3 restores target/view/color state when
the backend rejects a switch. Unsupported target formats are rejected before
SDL3's closest-format fallback can disguise them as RGB565.

The guarded implementation passed the target acceptance matrix, P1-A switching
stress, P1-B allocation-failure recovery, P1-C canonical VRAM exhaustion and
recovery, and the P1-D source invariant. The post-promotion normal-build
hardware smoke **PASSED on physical hardware** on 2026-10-06. It covered native
renderer identity, unsupported-format
rejection, screen/target/screen preservation, A/B switching and sampling, NPOT
edges, selected-target present rejection, continued target use, 64-cycle
viewport/clip restoration stress, and separate test-build allocation-failure
recovery. Results and captures are in
`test/dreamcast/results/phase1-captures/pvr_phase1_promotion_smoke.log` and
`test/dreamcast/results/phase1-targets-prototype.md`.

The historical five-texture black frame remains unreconstructable. Preserve that
observation. The canonical P1-C control and pressure captures match pixel for
pixel by vblanks 2 and 3; vblank 1 may show the preceding page because of the
established presentation/capture timing behavior. Do not add production sleeps
or extra presents to address it.

## Promoted capability

- RGB565 render targets with paired-buffer preservation.
- Screen/target switching and sampling of completed targets.
- Hardware-validated NPOT target edges.
- SDL3 target-switch rollback for target/view, viewport, clip, and color state.
- Failure injection remains available only in a dedicated build with
  `SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS`; it is off in ordinary builds.

## Unsupported or deferred

- ARGB1555, ARGB4444, PAL8, or converted-format targets.
- `.dt` integration with targets; PAL8 targets.
- DMA, partial, or strided target-specific work.
- `RenderReadPixels` and texture address-mode emulation.
- Unrelated blend modes, list-routing optimization, FSAA, and other unvalidated
  renderer features.

## Next bounded task

Implement and hardware-validate `.dt` texture loading for the existing native
PVR renderer using `SDL_LoadDreamcastTexture_IO`. Limit the work to ordinary
sampled textures: preserve current target-format rejection, avoid target
integration, and add one fixture covering valid `.dt` load/sample plus malformed
input rejection. First inspect the existing loader and SDL2 reference; do not
start PAL8 targets, DMA, or broader texture-path changes in that task.

## Hardware rules

Use `tools/dc-load/kos-tool-locked.sh` for every launch and respect the shared
console lock. Do not clear a live lock, kill `kos-tool`, or interrupt an active
Dreamcast program. Hardware results beat emulator results.
