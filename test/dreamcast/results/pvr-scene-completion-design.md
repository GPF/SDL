# PVR scene completion and deferred texture reclamation

Design notes for the deferred-free barrier in `DC_PVR_FinishActiveScene`
(`src/render/dreamcast/SDL_render_dreamcast_pvr.c`). Facts below are read from the
KallistiOS sources named in each item (`kernel/arch/dreamcast/hardware/pvr/`) or measured
on hardware; anything that is inference is marked as such.

## The invariant

A destroyed texture's VRAM must stay allocated until every PVR scene that can still
fetch it has finished rendering. `pvr_mem_free` writes allocator free-list pointers into
the first 8 bytes of the freed block, and a new texture can be uploaded into the same
block, so an early free corrupts any scene still to be rendered. Gate 8 caught the
8-byte case: 5 of 6 runs defective before the fix.

The aim of a design is to keep that invariant without forcing the CPU to sleep at
destroy or present time solely to reclaim VRAM.

## KOS lifecycle (verified from source)

State in `pvr_state` (`pvr_internal.h`):

| field | meaning | set | cleared |
|---|---|---|---|
| `ta_busy` | a scene is open or its render has not started | `pvr_start_ta_rendering()` at scene begin (`pvr_scene.c`) | `pvr_render_lists()` when the render **starts** (`pvr_irq.c`) |
| `render_busy` | a render is in progress | `pvr_render_lists()`, just before `ta_busy` is cleared | `ASIC_EVT_PVR_RENDERDONE_TSP` handler (render **done**) |
| `render_completed` | a screen render finished and awaits a page flip | RENDERDONE handler, unless the render was to a texture | `pvr_vblank_handler()` when it flips the view page |

A render starts only when `pvr_render_lists()` finds all of: `ta_busy`, `!render_busy`,
`(!render_completed || curr_to_texture)`, and every enabled list transferred. It is
called from the list-done interrupts and from the vblank handler ("we may have a pending
render, that couldn't be done as the previous render wasn't flipped yet").

Consequences:

1. **A scene's render can start a whole vblank after `pvr_scene_finish()`.** For a screen
   scene it cannot start until the previous screen render has been flipped. Render-to-
   texture scenes skip the flip condition but still need `!render_busy`.
2. **Renders are serialized.** Scene N+1's render cannot start while scene N's render is
   in progress, so when N+1's render has started, N's has completed.
3. **Texture fetches happen during the render**, between `render_busy = 1` and the
   RENDERDONE interrupt. The TA only writes parameter and tile data.

### What each call proves

| call / observation | what it proves |
|---|---|
| `pvr_scene_finish()` returning | nothing about rendering ("it's just a matter of waiting for the interrupt") |
| `pvr_check_ready()` returning **0** | the scene's render has **started** (`ta_busy == 0`). Note the sense: 0 = ready, -1 = busy. |
| `pvr_wait_ready()` returning | same: the render has started; blocks via `genwait` until then |
| `pvr_wait_render_done()` alone | only waits if `render_busy` is already set. Called before the render started it returns immediately. This is the original bug. |
| ready, then `render_busy == 0` | the scene's render has finished: its textures are free to reclaim |
| the **next** scene's `pvr_wait_ready()` in `pvr_start_ta_rendering()` (inside `pvr_scene_begin`) | the previous scene's render has started, so the one before it has finished (consequence 2) |

KOS exposes no public non-blocking "render done" query. `pvr_wait_render_done()` blocks;
replacing the RENDERDONE ASIC handler would break KOS's own handler; reading
`pvr_state.render_busy` needs the private `pvr_internal.h`.

## Measurements

`clock_gettime(CLOCK_MONOTONIC)`, synthetic bench, 3 alternating runs per variant for
the first table, spread under 0.1%. (`SDL_GetTicksNS` was wrong on this port until
`a8a91e3c3`.)

| barrier before the deferred free | ms per render-target switch | ms per frame (8 quads + present) | gate 8 |
|---|---|---|---|
| none (racy original) | 3.28 | 16.65 | 6/6 corrupt |
| sleeping `pvr_wait_ready()` + `pvr_wait_render_done()` (committed) | 10.03 | 20.05 | 3/3 clean |
| spin on `pvr_check_ready()`, sleeping `pvr_wait_render_done()` | 10.03 | 20.05 | 8/8 clean |
| spin on `pvr_check_ready()`, spin on `render_busy` | 3.43 | 16.67 | 10/10 clean |

Ms per frame by workload (60 frames each; original / committed / spin-both):

| workload | original | committed | spin both |
|---|---|---|---|
| empty frame | 16.46 | 20.06 | 16.46 |
| 100 sprites | 17.26 | 30.01 | 23.72 |
| 600 points | 36.98 | 50.08 | 43.57 |
| 4 full-screen translucent quads | 25.30 | 30.22 | 25.90 |
| 12 full-screen translucent quads | 50.26 | 60.12 | 51.78 |
| 12 quads + 100 sprites + 600 points | 86.60 | 100.16 | 93.23 |

Readings:

- Every blocking wait costs about one 10 ms scheduler tick (inference: durations are
  multiples of ~10 ms, and `SDL_Delay(1)` also takes ~10 ms). Making only
  `pvr_wait_ready()` non-sleeping does not help because the sleeping render-done wait
  that follows pays the same tick.
- A fully spinning wait pays only real PVR time and is correct on gate 8.
- Waiting for render-done at all costs CPU/GPU overlap: roughly `min(CPU time, GPU time)`
  per frame (100 sprites: 17.3 to 23.7 ms). Frames that are GPU-bound lose almost
  nothing.
- A negative result recorded earlier ("a spin on `pvr_check_ready()` is not a valid
  barrier", 4 of 10 gate 8 runs corrupt) was a bug in that experiment: the loop was
  `while (!pvr_check_ready())`, which exits as soon as the TA is **busy**, so it never
  waited. It is retracted.

## Implemented: retirement by scene generation (option C)

`DC_PVR_FinishActiveScene` no longer waits. Instead:

- **Scene numbers.** `scenes_begun` counts `pvr_scene_begin*()` calls, including the
  presentation scene that `RenderPresent` starts. `scenes_entered` is the highest scene
  whose first `pvr_list_begin()` has returned (also set when `pvr_scene_finish()` opens
  any list never opened).
- **Retire, never free.** `DC_PVR_DestroyTexture` puts the texture on `retired_textures`
  tagged `retire_gen = scenes_begun`: the last scene that could reference it, because the
  SDL core flushes queued commands that use a texture before calling the backend destroy.
  This also covers the old "no scene open" immediate free, which was equally unsafe once
  a finished scene's render can still be running.
- **Proven-complete scenes** (`DC_PVR_CompletedScenes`, monotonic), from facts 1 to 4
  above: with `m` the latest entered scene, renders up to `m-2` are complete; if scene
  `m` is finished and `pvr_check_ready() == 0`, renders up to `m-1` are.
- **Reclaim** (`DC_PVR_ReclaimRetired`) at scene entry, scene finish, texture destruction
  and the end of `RenderPresent`: free every retired texture whose tag is proven complete.
  Nothing waits.
- **Blocking drain** (`DC_PVR_WaitScenesComplete`) only in three places: when
  `pvr_mem_malloc` fails (retry once after reclaiming; also used by the `.dt` loader),
  before a CPU upload into texture VRAM that a scene not yet proven complete sampled
  (`DC_PVR_WaitTextureIdle`, using `last_used_gen` set when a header samples the
  texture), and in `DestroyRenderer` before any VRAM is freed (it now waits for every
  submitted render whether or not a scene was open; it used to wait only if one was).
  The drain checks the KOS wait results and does not free on a timeout. If a scene is
  open and has entered the TA it does not wait for `ta_busy` (that would not end until the
  scene is finished) and only waits for render-done, giving `m-1`.

Render-target buffers use the same rule: a target texture's `vram` and `vram_next` are
freed through the retire list, and the swap after a target scene is a pointer swap that
only has to be ordered before later scenes, which PVR render serialization provides.

Residual assumptions, stated so they can be challenged:

- Fact 2 relies on `pvr_wait_ready()` inside `pvr_start_ta_rendering()`, which times out
  after 100 ms without reporting it. A render stalled that long means the system is
  already failing; the committed sleeping wait ignored the same timeout.
- Texture VRAM is held until a later scene proves completion, normally about one frame
  later. Out-of-memory handling drains first, so this costs a wait only under memory
  pressure.
- Uploads into a texture that a not-yet-proven scene sampled still block (the write fence).
  A texture that is uploaded every frame right after a render-to-texture scene that
  sampled it pays that wait; renaming the backing store (allocate a new block, retire the
  old one) would remove it and is not done.
- `SDL_RenderPresent` now returns before the frame is rendered. A capture taken right
  after it can show the previous frame, so `pvr_phase0_capture()` waits 100 ms first.

### Results (hardware, 640x480)

Benchmarks use `clock_gettime`. Correctness uses the two lifetime detectors plus the
existing fixtures.

| | racy original | committed sleeping wait | **retirement** |
|---|---|---|---|
| ms per render-target switch | 3.28 | 10.03 | **3.29** |
| ms per frame, 8 quads + present | 16.65 | 20.05 | **16.62** |
| 100 sprites | 17.26 | 30.01 | **16.68** |
| 600 points | 36.98 | 50.08 | **29.5** |
| 4 full-screen translucent quads | 25.30 | 30.22 | **25.2** |
| 12 full-screen translucent quads | 50.26 | 60.12 | **50.0** |
| 12 quads + 100 sprites + 600 points | 86.60 | 100.16 | **74.7** |
| ms per frame, upload right after a target scene | 23.5 (incorrect) | 40.05 | **30.0** |

| detector | racy original | committed | **retirement** |
|---|---|---|---|
| gate 8 (one run each) | 6/6 corrupt | 3/3 clean | **16/16 clean** (10 with the old capture, 6 with the settled capture) |
| `pvr_texture_churn`, 100 iterations per run | 100/100 corrupt | 300/300 clean | **1500/1500 clean** (15 runs), VRAM lost: 0 in every run |
| `pvr_texture_update_race`, 242 frames per run | 242/242 corrupt | 2 runs clean | **12 runs, 2904/2904 frames clean** |

Existing fixtures against the retirement library: boundary, lifecycle, textured_quad
(settled capture), clear_triangle, coordinates, copy_transform, point_line, default_lines,
blend_modes, dt_texture, and `pvr_phase1_hardening` P1-A, P1-B (failure-injection library),
P1-C repeat control and repeat pressure 1 to 3 all pass, and their captures match earlier
known-good runs pixel for pixel except the documented unwritten bottom-row band (16 px)
and 2 px of dither noise. `pvr_dt_direct` and `pvr_phase1_targets` ran to completion
(`pvr_targets result=PASS`, exit 0) but have no earlier capture to compare against. The
pressure gates exhaust VRAM, destroy 230 textures and reallocate them, which exercises
the out-of-memory drain.

The spin-wait variant (option B in earlier revisions of this note: `pvr_check_ready()`
then `render_busy`) was correct on gate 8 but gave back CPU/GPU overlap (100 sprites: 23.7
ms) and needed KOS-internal state, so it is not used; its numbers remain the ceiling
for "make the wait non-sleeping".
