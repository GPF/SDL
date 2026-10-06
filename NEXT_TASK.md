# NEXT_TASK — SDL3 Dreamcast PVR renderer

State: Phase 0 Gates 1–9 and the guarded Phase 1 RGB565 target prototype's
initial hardware matrix have passed on physical hardware. The prototype remains
opt-in; repeated-switch stress, VRAM allocation failure/recovery, and
scene-completion failure behavior remain to be tested. Results are in
`test/dreamcast/results/phase0-baseline.md` and
`test/dreamcast/results/phase1-targets-prototype.md`. Agent proposes, agent
verifies, Dreamcast hardware decides.

Durable hardware results go in `test/dreamcast/results/`. Procedures are in
`test/dreamcast/TESTING.md`.

## Phase 0 hardware gates — complete

All nine gates passed on hardware. Their fixture assertions establish API and
log behavior; captured frames provide visual evidence for the rendered output.

1. **Gate 1 — opt-in driver selection + lifecycle** (`pvr_phase0_lifecycle`).
   **PASS on hardware.** Recorded in `test/dreamcast/results/phase0-baseline.md`.
   Every fixture asserts the renderer name and aborts on a fallback, so a PASS
   from a fixture is only PVR coverage when its `renderer=` line says
   `Dreamcast PVR`.
2. **Gate 2 — float color policy** (`pvr_phase0_clear_triangle`). **PASS.**
   Golden table: 64/128/191, 191/128/64, 128/64/191, clear 26/51/77.
3. **Gate 3 — textured quad** (`pvr_phase0_textured_quad`). **PASS** after
   correcting opaque ARGB1555 fixture texels.
4. **Gate 4 — coordinate transform** (`pvr_phase0_coordinates`). **PASS.**
   Scale 2.0, viewport origin addition, viewport-relative clip.
5. **Gate 5 — rejections** (`pvr_phase0_boundary`, 7 cases). **PASS** after
   moving MOD rejection into the PVR-specific SDL core setter gate and uploading
   the boundary fixture texture before its in-scene update check.
   Case 7 is the `-ffast-math` proof that the NaN guard survives the release
   build.
6. **Gate 6 — copy, rotation, and flip** (`pvr_phase0_copy_transform`). **PASS.**
   Capture confirms copy orientation plus 90° clockwise rotation, horizontal
   flip, and vertical flip through SDL3's `QueueGeometry` path.
7. **Gate 7 — scale-1 points and line rejection** (`pvr_phase0_point_line`).
   **PASS on hardware.** Scale was 1.0 with logical presentation disabled,
   selecting the direct `QueueDrawPoints` path; `SDL_HINT_RENDER_LINE_METHOD=2`
   selected the `QueueDrawLines` path and the backend rejected it at queue time.
8. **Gate 8 — deferred texture destruction** (`pvr_phase0_deferred_texture`).
   **PASS on hardware.** Destroyed-source and retained-control checkers were
   visible after presentation.
9. **Gate 9 — default SDL3 line method** (`pvr_phase0_default_lines`).
   **PASS on hardware.** Hint unset, scale 1.0, logical presentation disabled;
   expected line rasterization was captured.

Hardware gates are complete. Future gates and Phase 1 work are listed below.

## Fixes verified by hardware gates

- **Deferred VRAM ordering.** `DC_PVR_ReleaseDeferredTextures()` now runs after
  render completion and before `pvr_shutdown()`. `pvr_shutdown()` calls
  `pvr_mem_initialize(NULL, 0)` and `pvr_mem_reset()`, so freeing after shutdown
  operated on an invalidated pool.
- **Line commands.** `QueueDrawLines` no longer aliases the points callback.
  Phase 0 has no line primitive, so it rejects at the queue boundary instead of
  accepting a queue and failing later at emission. The default SDL3 line method
  (points/rects) still works through the geometry path.
- **Default video mode reverted to opt-in.** The Dreamcast default is
  `SDL_DC_OPENGL_VIDEO` again, so applications that request OpenGL by name are
  not moved to a PVR display configuration. `DC_PVR_RenderDriver` moved from the
  first entry in `render_drivers[]` to the last hardware entry, so a non-PVR
  window is not disturbed by a failed PVR attempt. The PVR renderer is selected
  through the video layer's `SDL_HINT_RENDER_DRIVER` when the PVR mode is
  explicitly requested.
- **Gate 1 shutdown reporting.** `shutdown=PASS` printed unconditionally. It now
  reports that destruction returned, and the lifecycle criterion is the exit code
  plus the absence of a hang.
- **Renderer name asserted in every fixture**, not just printed.
- **Gate 5 case numbering** corrected to 1-7.
- **Window layer PVR branch** (`SDL_dreamcastwindow.c`): the PVR mode matched none
  of the display-mode branches, so window creation failed with "Unsupported
  display mode". PVR now uses the standard resolution/refresh table with
  `PM_RGB565`, matching the SDL2 PVR path. Hardware-verified by Gate 1.
- **`renderer->name` assignment** added in `DC_PVR_CreateRenderer`. SDL3 core does
  not set it from the driver; each backend assigns it itself. Hardware-verified by
  Gate 1.

Build and `git diff --check` were clean after this pass.

## Gate 8 — deferred texture destruction — hardware PASS

The fixture queues a draw from an uploaded texture, destroys that texture
before `SDL_RenderPresent`, then presents. SDL3 core flushes the queued draw from
`SDL_DestroyTexture`; the PVR backend defers VRAM release until render completion.
The screenshot shows the destroyed-source checker and a retained control checker
after presentation. See `results/phase0-captures/pvr_phase0_deferred_texture.ppm`
and the Gate 8 entry in `test/dreamcast/results/phase0-baseline.md`.

The first single-scene captures were black even when the texture was retained.
Three warmup frames containing real textured draws, followed by the queued-
destroy frame, produced the expected capture. This remains a presentation/page
warmup observation; it does not identify the exact cause of the black captures.

## Gate 9 — default SDL3 line method — hardware PASS

With `SDL_HINT_RENDER_LINE_METHOD` unset, scale `1.0,1.0`, and logical
presentation disabled, the fixture rendered axis-aligned, diagonal, reversed,
zero-length, and connected lines through the default POINTS path. It asserted
the selected renderer was `Dreamcast PVR`; the screenshot shows the expected
rect/point rasterization and single-coverage connected joints. See
`results/phase0-captures/pvr_phase0_default_lines.ppm`.

## Remaining Phase 0 decisions — resolved

- **VSync:** PVR accepts SDL3 intervals 0 and 1; other intervals are
  unsupported. Interval 0 disables SDL's simulated pacing, while the Dreamcast
  video layer still flips pages at vblank. Gate 1 verifies 0→1→0 and rejects
  interval 2.
- **Header cache:** removed the unused `header_format` field. A texture's format
  is immutable, and the cached texture pointer already distinguishes textured
  headers.
- **Build artifacts:** `.elf` and `.bin` outputs under `test/dreamcast/` are
  ignored; source, Makefiles, and captured `.ppm` evidence remain trackable.

## Remaining Phase 1 renderer scope

The guarded RGB565 preservation prototype below exists. General target support
remains experimental; the following work is deferred and untested:

- Target stress/failure recovery and capability promotion.
- Unordered target semantics and PVR list-routing optimization.
- `.dt` texture loading through `SDL_LoadDreamcastTexture_IO`.
- PAL8/palette textures and the optional palette callbacks.
- Strided textures, partial upload, DMA upload.
- `RenderReadPixels` (needed before any self-checking visual gate can exist).
- Opaque and modifier list routing, and the culling/depth-write flags that go
  with them.
- FSAA.
- Texture address modes. The PVR always wraps, so `CLAMP` needs CPU-side
  emulation or explicit rejection. Phase 0 documents it as unimplemented rather
  than approximating it.

## Phase 1 first slice — guarded RGB565 preservation prototype (hardware pass)

The initial slice ports the validated SDL2 preservation model behind an
experimental build guard. Keep target capability unadvertised until the
remaining stress and failure-recovery matrix passes. The prototype supports
RGB565 only; PAL8, `.dt`, DMA, list-routing optimization, format fallback
changes, and additional blend-mode core changes are outside this slice.

### SDL3 target-switch contract

Current SDL3 `SDL_SetRenderTarget()` validates the texture, flushes the current
command queue, changes `renderer->target` and `renderer->view`, updates color
scale, invokes the backend `SetRenderTarget` callback, then queues viewport and
clip state for the selected view. The target switch is therefore a lifecycle
callback between two command queues, not a render command. The callback sees
the requested target already installed in SDL core. SDL core now propagates a
failed queue flush before changing targets, and restores the prior
`renderer->target`, `renderer->view`, and color scale if the backend callback
fails. The PVR callback consumes a one-shot failure injection before mutating
its selected destination. Hardware Gate 10 exercised callback failure rollback,
including color scale (a 2× SDR white-point target was rejected, and the
subsequent screen gray patch captured at RGB 128). Flush-failure injection and
scene-completion failure recovery are not yet hardware-tested.

SDL3 also rejects `SDL_RenderPresent()` while a texture target is selected (the
public API returns an error before flushing). SDL2 preservation notes include
target-selected-present behavior from their SDL2 backend/prototype. Do not port
that behavior into SDL3. The equivalent SDL3 test verifies the rejection,
confirms the target remains selected and usable, then switches to the screen
and samples the committed target.

### Intended destination state machine

Each render destination has a `current` and `next` PVR allocation plus a
`current_valid` bit. The renderer owns one paired logical-screen destination;
each RGB565 target texture owns its own pair. Renderer state identifies the
selected destination, active scene kind, scene target, and whether a PVR scene
is open. Allocation and VRAM ownership stay explicit; all outstanding work is
waited on before freeing either member of a pair or shutting PVR down.

1. **Create:** allocate the logical-screen pair. A target texture allocates its
   pair only after validating dimensions, RGB565 format, KOS stride/alignment,
   and VRAM size. Both destinations begin invalid; never seed from undefined
   memory. Allocation failure cleans up the partial pair and leaves no target
   capability advertised.
2. **Queue:** queue callbacks only copy/convert immutable CPU-side command data.
   They do not open, finish, or submit PVR scenes. `RunCommandQueue` lazily
   opens a scene/list for the currently selected destination on the first
   clear/draw command. It starts rendering into `next`, seeds it from `current`
   when valid, then emits queued commands in painter order.
3. **Commit destination:** on target switch or screen presentation, finish the
   list and scene and wait for render completion. Only on successful completion
   swap `current`/`next` and mark `current_valid`. On failure, do not publish the
   next allocation as current; latch renderer failure so later commands cannot
   pretend the destination switch succeeded.
4. **Switch target:** SDL has already flushed the old queue. Commit the old
   destination, select the requested target or logical screen, invalidate PVR
   header/cache state, and let SDL's following viewport/clip commands restore
   per-view state before subsequent drawing. Screen → A → screen and A → B → A
   must each preserve all prior pixels not overwritten by later commands.
5. **Present on screen:** SDL has already flushed queued commands. Commit the
   logical-screen destination, render its current allocation to the physical
   screen, finish and wait. Do not present or change destination while a target
   is selected; SDL core rejects that call. After successful screen present,
   the next frame's logical screen starts invalid because SDL does not promise
   preserved backbuffer contents across presents.
6. **Sample target:** only a committed, valid target allocation may be bound as
   a source. Preserve the SDL2-validated PVR RTT edge rule:
   `u_max = (logical_w - 1) / backing_w` and
   `v_max = (logical_h - 1) / backing_h` (zero for dimensions <= 1). Ordinary
   uploaded textures keep their existing `logical / backing` UV rule.
7. **Destroy:** selected-target destruction follows SDL's reset-to-screen
   sequence. Flush/commit references first, wait for PVR completion, then free
   both allocations. Renderer destruction finishes any active scene, waits,
   releases target and screen VRAM while the allocator is alive, then shuts
   down only the PVR instance it owns.

The guarded Gate 10 fixture passed on physical hardware for screen → target →
screen, target A → B → A, completed-target sampling, 127×93 NPOT edge sampling,
selected-target present rejection and continued target use, and failed-switch
rollback. Captures show preserved and overwritten regions in
`test/dreamcast/results/phase1-captures/`. The guard remains opt-in and target
capability is not promoted. Repeated stress switching and VRAM allocation
failure/recovery remain untested, so this prototype is not ready for general
application use.

## SDL core decisions resolved

1. **Blend modes:** `IsSupportedBlendMode` consults the capability callback for
   required built-in modes only when the renderer name is `Dreamcast PVR`.
   Other renderers retain SDL's built-in support guarantee. Gate 5 passed after
   MOD began failing at the setter; the queue-time PVR guard remains defensive.
2. **Format fallback:** SDL3 matches bytes-per-pixel and alpha, then falls back
   to the first renderer format. PVR lists RGB565 first. Default builds reject
   target textures; the opt-in prototype creates RGB565 targets only, and its
   fixture requests RGB565 directly. No extra core exception was needed for
   this tested format list. Add explicit fallback tests before exposing other
   target formats or changing format ordering. This differs from the SDL2
   fallback algorithm, which needed the explicit ARGB8888 target special case;
   do not copy that exception into SDL3 without new evidence.

## Unverified assumptions

These are read from source, not proven on hardware:

- `pvr_wait_render_done()` does not call `vid_flip()` — there is no `vid_flip()`
  anywhere in KOS's PVR code (verified by grep). Presentation therefore depends
  on the vblank handler's page flip being enabled for the active mode. Gates 1–9
  captured the rendered frames on hardware, confirming basic presentation in the
  tested mode. Page-flip timing and other buffering modes have not been measured.
- The PVR video mode's buffering comes from the existing video layer. Its basic
  presentation path has been exercised by Gates 1–9; behavior under other modes
  or timing conditions remains unverified.
- The fallback path when the PVR driver fails on a non-PVR video mode (core
  warns and moves to the next driver) has not been exercised.
- `aligned_alloc(32)` for the staging store is kept for future DMA use; the SQ
  path does not require it today.

## Stop and report if

- A gate fails in a way that changes the coordinate contract.
- Basic rendering requires invasive changes outside the renderer.
- The first hardware triangle lands somewhere the derived contract does not
  explain.
- PVR ownership conflicts with the video layer (the video layer already called
  `pvr_init_defaults()` for the active mode).

## Suggested order

Gates 1–9 are complete. The next task is focused stress/failure testing of the
guarded target prototype: repeated switches/seeds, VRAM allocation failure and
recovery, and fail-closed behavior after a failed PVR scene completion. Keep the
build guard opt-in and do not advertise target capability until those tests pass.
