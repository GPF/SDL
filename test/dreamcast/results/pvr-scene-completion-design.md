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

## Options

**A. Sleeping wait (committed).** Correct and simple, public KOS API only. Costs a
scheduler tick per scene finish, which is too much for target-heavy or 60 fps scenes.

**B. Bounded spinning wait.** Same two waits, spinning: `while (pvr_check_ready() != 0)`
then `while (render_busy)`. Correct on gate 8 (10/10) and nearly free in the benchmark.
Needs the private `render_busy` (or a small KOS addition such as
`pvr_check_render_done()`), must be bounded (fall back to the sleeping wait after a
timeout so a hardware stall cannot hang the CPU), and burns CPU for the render duration,
which matters for audio and other threads (inference: the KOS scheduler preempts at the
tick, so other threads still run, but this was not measured). Gives up CPU/GPU overlap.

**C. Non-blocking retirement by scene generation.** Give each SDL scene a sequence
number. A texture destroyed while scene `s` is open or was last finished is tagged
`retire_after = s` (this includes the immediate-free branch in `DC_PVR_DestroyTexture`
for `!scene_active`, which is also unsafe once finish no longer waits). It may be freed
once scene `s+1`'s render is known to have started, because renders are serialized.
That is observable without sleeping: `pvr_check_ready() == 0` after scene `s+1` was
finished, or the return of the following `pvr_scene_begin`. Cost: no waiting, VRAM held
about one extra frame, CPU/GPU overlap preserved. Required fallbacks: a blocking drain
when `pvr_mem_malloc` fails, at `DestroyRenderer`, and when the retire queue grows past
a bound or the app goes idle (no later scene would otherwise advance it).

Open questions for C, not yet answered:

- `DC_PVR_FinishActiveScene` also swaps render-target buffers after a target scene
  finishes and relies on the render having completed; removing the render-done wait must
  not change that ordering (inference: PVR ordering makes later sampling safe, but CPU
  reads of target contents would not be).
- The presentation path runs an extra "preservation seed" scene after each frame; the
  generation numbering must count it.
- Validating C needs a detector that fails on early reclamation. Gate 8 detects a free
  that precedes the render start but exits before any reclamation point, so on its own
  it cannot judge C. `test/dreamcast/pvr_texture_churn` repeats gate 8's scene 40 times
  per run (same textures, positions and destroy-before-present, with the same warm-up
  presents, then a short delay so the final frame is displayed) and compares the
  destroyed texture's quad with the expected checker. Validated on hardware: the racy
  original fails all 40 iterations in every run (exactly the 16 corner pixels, 640 bad
  pixels per run, 2 runs); the committed fix passes 40/40 twice; spin on
  `pvr_check_ready()` + sleeping render-done passes 120/120; spin on both passes 120/120.
  Earlier attempts with other geometry (a 256x256 full-screen texture, then a 64x64
  texture in a small quad, with a frame-number marker) could not reproduce the race
  even on the original code, because the race exists only while the scene's render has
  not started yet and those frames did not leave the PVR in that state; the gate 8 scene
  and its warm-up pattern do.

## Recommendation

Short term: B with a bounded fallback, since it is correct on the existing detector and
removes the measured stall. Long term: C, once a fixture exists that fails on early
reclamation. Do not rely on `pvr_scene_finish()` or a bare `pvr_wait_render_done()` for
completion in either case.
