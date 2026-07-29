# Dreamcast Native PVR Render-Target Preservation Research

**Status:** Independently corrected after source and hardware-evidence review.
The first research pass is **not implementation-ready**. Exact SDL semantics
may be feasible with an offscreen logical-screen design, but a bounded physical
microprobe is required before another renderer coding pass.

**Date:** 2026-07-29

## 1. Scope and authoritative evidence

This document follows the rejected guarded prototype recorded in
`PVR_RENDER_TARGET_RESEARCH.md`. It addresses two separate questions:

1. how to preserve screen and target destination contents across PVR scene
   boundaries; and
2. why the sampled target's extreme right/bottom edge contains periodic holes.

Physical Dreamcast captures under
`test/dreamcast/test/pvr_render_target/` are authoritative. No emulator result
may replace those captures.

The actual test sequences are defined by
`test/dreamcast/test/pvr_render_target/pvr_render_target.c`:

- Page 3: screen S1 -> target scene -> screen S2, without an intervening
  screen present. Red S1 disappears and only green S2 remains.
- Page 7: draw red T1 into one target -> switch to screen -> reselect the same
  target -> draw green T2. Red T1 disappears.
- Page 8: draw blue T1 into one target -> `SDL_RenderPresent` while it remains
  selected -> draw yellow T2. Blue T1 disappears.
- Page 9: copy a recovered green target into the logical screen, perform later
  target lifecycle transitions, then draw a green status bar on the screen.
  The earlier screen copy disappears and only the later bar remains.
- Pages 2, 5, and 10 use 160x120, 256x128, and 127x93 targets respectively.
  All show some extreme bottom/right discontinuity. Therefore this is not
  proven to be an NPOT-only defect.

## 2. Corrections to the first preservation report

The following first-pass conclusions were unsupported or internally
contradictory and must not guide implementation:

1. **RTT-only ping-pong is not exact SDL support.** It can address Pages 7 and
   8, but cannot address screen persistence in Pages 3 and 9. Target-texture
   support necessarily introduces screen -> target -> screen boundaries.
2. **The screen cannot simply remain a direct single destination.** Page 3 is
   the physical counterexample. A later screen scene starts from its background
   plane, not the earlier framebuffer contents.
3. **The RTT pixel-clip register is not half-open.** KOS programs both normal
   screen rendering and RTT with an inclusive maximum of `dimension - 1`.
   `pvr_buffers.c:328-332` initializes screen clip maxima to
   `width - 1`/`height - 1`; `pvr_misc.c:241-247` uses the stored screen values
   or the same `target_w - 1`/`target_h - 1` rule for RTT. Validated screen
   corner pixels also contradict the half-open theory.
4. **Changing PVR clip maxima to `dimension` is unsafe and unjustified.** It
   may enable an extra row/column beyond the allocation and must not be done.
5. **The physical-page descriptions were inaccurate.** Page 7 reselects the
   same live target; it does not destroy and recreate it. Page 2 is 160x120 and
   Page 5 is 256x128, not 127x93.
6. **A CPU upload cannot preserve GPU-produced contents.** Target textures have
   no current CPU mirror. A preservation copy must be a PVR texture-to-target
   draw or a separately proven VRAM copy operation, not `pvr_txr_load` from
   stale CPU data.
7. **The timing figures were estimates without measurement.** The reported
   2.6 ms and 5 ms threshold have no physical benchmark behind them.
8. **The memory table double-counted 512x512.** One RGB565 512x512 allocation
   is 524,288 bytes; two are 1,048,576 bytes (1 MiB), not 2 MiB.

## 3. Verified PVR scene behavior

### 3.1 Every rendered scene has a background plane

KOS `pvr_begin_queued_render()` constructs a full-destination background plane
in `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_misc.c:186-223`.
It selects target dimensions at lines 192-193 and uses `pvr_state.bg_color` for
all three background vertices.

The PVR receives no load-existing-destination operation. Uncovered pixels in a
new scene therefore come from this background plane, not from the previous
contents of the framebuffer or RTT allocation. This matches all four physical
persistence failures.

### 3.2 RTT selection is per scene

`pvr_scene_begin_rtt()` in
`/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_scene.c:155-181`
records the next target address, render width/height, and stride before calling
`pvr_scene_begin()`. `pvr_begin_queued_render()` later programs either a screen
framebuffer address or the RTT address (`pvr_misc.c:230-238`). There is no
public destination-continuation or load operation.

Experimental list continuation changes list submission within one destination
scene. It cannot change destinations or preserve a finished destination and is
not a solution.

### 3.3 Scheduling does not supply preservation

KOS serializes render-core execution, which can make a completed RTT safe to
sample in a later queued scene without a target-switch wait. Serialization
orders reads and writes; it does not copy the old destination into the next
scene.

## 4. Exact preservation requirements

An exact design must handle all of these independently:

```text
screen S1 -> target A -> screen S2 -> present
target A:T1 -> screen/target B -> target A:T2
target A:T1 -> RenderPresent(target still selected) -> target A:T2
target A render -> screen samples A
target A render -> target B samples A
```

The seed copy of an old destination must be the first visible operation in the
new scene. SDL commands then render over it in original order. A leading
full-destination `SDL_RenderClear` may eventually allow the seed to be skipped,
but only after the unoptimized exact path is validated.

## 5. Candidate architectures

### 5.1 Per-target ping-pong only

Each target owns `current` and `next`. A resumed target scene samples `current`
into `next`, draws new commands, finishes, then makes `next` current.

This can preserve RTT targets and is a valid component of an exact design. It
does **not** preserve the logical screen and therefore fails Pages 3 and 9.

**Verdict:** insufficient by itself.

### 5.2 Direct physical screen plus RTT ping-pong

This is the first report's recommendation. It still begins a fresh physical
screen scene after every target excursion. The fresh background plane erases
S1 before S2, exactly as Page 3 demonstrates.

**Verdict:** rejected by physical evidence.

### 5.3 Logical screen in offscreen RGB565 allocations

Treat the SDL screen destination as an offscreen RTT destination. Screen draws
and clears render into the logical-screen allocation. A target excursion uses
another RTT destination. When screen drawing resumes, its old allocation is
sampled into a different allocation before S2. When SDL presents the screen,
a short physical-screen scene samples the completed logical-screen texture.

This is the only identified architecture that can satisfy both screen and RTT
persistence without reading the physical framebuffer. It also cleanly
separates SDL's logical destination from KOS page flipping.

Important consequences:

- Screen rendering becomes RTT even when applications never create a target.
- The screen needs a sampleable RGB565 allocation plus a different allocation
  whenever its contents must be preserved into another scene.
- Presentation adds a full-screen texture draw and a physical screen scene.
- The physical screen's post-present contents are irrelevant to later SDL
  drawing; SDL already does not guarantee backbuffer contents after present.
- The renderer may mark logical-screen contents undefined after a screen
  `RenderPresent`, potentially avoiding a preservation seed at the start of
  the next frame. It still must preserve screen contents across target
  excursions before the same present.

**Verdict:** potentially exact, but requires a physical feasibility and memory
probe before integration.

### 5.4 One scratch allocation shared across destinations

Instead of permanently doubling every target, keep one current allocation per
logical destination and obtain a compatible scratch allocation when resuming
that destination. Seed current -> scratch, render, then swap ownership.

This can reduce steady-state VRAM, but reuse is constrained:

- the old allocation is sampled by the queued seed scene and cannot be freed
  or reused as another render destination until that scene is complete;
- differently sized/strided destinations need compatible scratch storage;
- avoiding a wait may require multiple deferred scratch allocations when
  scenes are queued rapidly; and
- waiting at every switch may destroy the performance benefit of TA/render
  overlap.

**Verdict:** promising memory optimization after correctness, not the first
prototype invariant.

### 5.5 Mirrored screen submission

Submitting each screen command once to a preserved texture and again to the
physical screen doubles geometry/list traffic and still needs separate scenes.
The PVR has no public multi-destination render operation.

**Verdict:** not useful.

### 5.6 Deferred multi-destination command execution

SDL flushes queued commands before target changes and expects resource and
error semantics at those boundaries. Retaining arbitrary backend command data
would require new ownership for textures and vertices, dependency ordering,
destruction handling, and potentially unbounded memory.

**Verdict:** materially larger and riskier than offscreen preservation.

## 6. Preservation-copy mechanics

The first exact prototype should use a PVR textured seed draw:

1. begin RTT scene for `next`;
2. emit a sampled RGB565 header for `current` with `NONE` blending, nearest
   filtering, and explicit edge-safe UV state;
3. draw one full-destination quad;
4. execute queued SDL commands after the seed in their original order;
5. finish the scene and logically swap current/next.

The existing geometry path represents a quad as two independent triangles:
one header plus six 32-byte vertex commands, or seven TA units/224 bytes when a
new header is required. A dedicated triangle-strip seed could use one header
plus four vertices, or five TA units/160 bytes. These are command-traffic
figures, not render-time measurements. The dominant cost is the full-surface
texture read and render write, which must be measured on hardware.

The swap can occur logically when the scene is queued, but allocation reuse
must respect render completion. KOS render serialization may make a later
scene's sampling safe; it does not make the old sampled allocation immediately
safe to free or overwrite from the CPU.

## 7. VRAM accounting

The current prototype allocates full POT backing storage:

| Logical size | POT backing | One RGB565 allocation | Two allocations |
|---|---:|---:|---:|
| 64x64 | 64x64 | 8 KiB | 16 KiB |
| 127x93 | 128x128 | 32 KiB | 64 KiB |
| 128x128 | 128x128 | 32 KiB | 64 KiB |
| 256x256 | 256x256 | 128 KiB | 256 KiB |
| 512x512 | 512x512 | 512 KiB | 1 MiB |
| 640x480 screen | 1024x512 | 1 MiB | 2 MiB |

The last line is the relevant cost for ordinary POT texture sampling. The
logical 640x480 byte count is 614,400 bytes, but that is not the allocation
used by the current texture representation.

A strided 640-pixel screen texture might reduce this cost, but KOS exposes one
global strided-texture width and the renderer already treats differing strided
textures as a Phase 6 batching hazard. It is a separate design requiring proof;
it cannot be assumed in the Phase 5 budget.

The PVR's 8 MiB also contains KOS framebuffer, TA vertex, tile matrix, OPB, and
texture-heap allocations. The renderer currently requests a 512 KiB vertex
buffer and a 16-word translucent OPB bin. Total usable `pvr_mem` after
`pvr_init()` must be measured rather than inferred from 8 MiB. A two-allocation
logical screen alone consumes 2 MiB from that heap in the current POT design.

## 8. Extreme-edge defect

### 8.1 Clip-register theory is rejected

The first report asserted that PVR clip maxima are half-open and recommended:

```c
PVR_SET(PVR_PCLIP_X, target_w << 16);
PVR_SET(PVR_PCLIP_Y, target_h << 16);
```

Do not implement this. KOS consistently stores an inclusive maximum. Screen
rendering uses `width - 1` and `height - 1`, and RTT uses the same convention.
The periodic pattern also cannot be explained by one excluded row/column
alone.

### 8.2 Strongest current hypothesis

The discontinuity is visible after a completed target is sampled and enlarged.
It occurs for both POT and NPOT targets. The target UV maximum is currently:

```text
u_max = logical_w / backing_w
v_max = logical_h / backing_h
```

The PVR's exact nearest/linear texel-center convention has not been established
for this path. Samples near the destination's final pixel centers may select
the first padding texel (`logical_w`/`logical_h`), or wrap at 1.0 for exact-POT
targets. RTT padding is uninitialized, so enlarged padding samples can appear
as periodic dark/stale patterns. This explains why the final few scaled rows
contain repeating prior-looking data rather than a single uniformly absent
line.

This is a hypothesis, not yet a confirmed root cause. `PVR_UVCLAMP_UV` only
clamps coordinates that exceed 1.0; it does not automatically clamp an NPOT
logical edge below 1.0 to the last logical texel. A half-texel inset, padding
initialization/replication, or both may be required. Any adjustment can affect
the already validated ordinary texture path and therefore needs a focused
hardware comparison.

### 8.3 Required diagnostic page

Before changing KOS or renderer defaults, build one physical microtest with
separate cells for 128x128, 127x93, 160x120, and 256x128 RTT targets. Each
target should contain unique one-pixel top/bottom/left/right borders and a
checker interior. Sample each target:

- 1:1 at integer screen coordinates;
- enlarged with nearest filtering;
- enlarged with linear filtering;
- with current UV endpoints;
- with half-texel-center endpoints;
- with logical-edge padding replicated into the POT backing where possible;
- with `PVR_UVCLAMP_UV` as an independent variable.

Include source-texel-index color bands near the final two rows/columns so the
capture proves whether the sampler selects the last logical texel, padding, or
wrapped texel zero. Retain one physical framebuffer capture per page. Do not
change `PVR_PCLIP_X/Y` in this diagnostic.

## 9. Bounded preservation microprobe

Before a renderer integration pass, a standalone guarded hardware probe must:

1. print `pvr_mem_available()` after current renderer/PVR initialization;
2. allocate two 1024x512 RGB565 screen textures and report remaining VRAM;
3. render screen S1 into allocation A;
4. render A as the first textured quad into allocation B, then add S2;
5. present B through a physical screen scene and capture both S1 and S2;
6. repeat the same current -> next seed for 128x128 and 512x512 targets;
7. execute target T1 -> present while selected -> T2;
8. time seed scenes using KOS timers around completion, not CPU submission;
9. report allocation failure and cleanup behavior; and
10. capture every distinct preservation result on physical Dreamcast.

This probe answers whether full-screen offscreen preservation fits alongside
current KOS PVR buffers and whether its fill cost is acceptable. It should not
advertise ordinary `SDL_RENDERER_TARGETTEXTURE` or modify the validated normal
renderer path.

## 10. Verdict

**Verdict: more bounded physical-hardware research is required before another
renderer coding pass.**

Per-target ping-pong is necessary for resumed RTT destinations but insufficient
for SDL target support. Exact semantics require preserving the logical screen
as well. The only currently credible complete architecture is an offscreen
logical screen plus RTT preservation seeds, followed by a physical-screen copy
at screen `RenderPresent`. Its 2 MiB POT screen-allocation peak, actual
post-`pvr_init` VRAM availability, synchronization/reuse behavior, and
full-surface cost remain unmeasured.

The edge-hole cause is also not confirmed. The KOS clip-change recommendation
is rejected. Texture endpoint/padding behavior is the leading hypothesis and
requires the focused physical matrix in Section 8.3.

Keep `DC_PVR_RENDER_TARGET_PROTOTYPE` private and keep ordinary
`SDL_RENDERER_TARGETTEXTURE` disabled. Do not ask for a renderer implementation
until both microprobes establish:

- screen S1 and S2 survive an offscreen seed/present sequence;
- target T1 and T2 survive reselection and target-selected present;
- exact VRAM peaks and completed-render timing are acceptable;
- allocation failure cleans up without corrupting PVR ownership; and
- one evidence-based edge correction works for POT and NPOT targets without
  regressing ordinary texture sampling.

---

## 11. Physical preservation and edge microprobe (2026-07-29)

### 11.1 Test record

The bounded test is under
`test/dreamcast/test/pvr_render_target_microprobe/`. It was built with
`-Wall -Wextra -Werror` against the separate
`DC_PVR_RENDER_TARGET_PROTOTYPE` archive. The ordinary archive remained without
`DC_PVR_SetRenderTarget`. No emulator was used.

Authoritative runs:

- `hardware-20260729-132149.log`: corrected preservation pages and four scaled
  edge matrices;
- `hardware-20260729-133148.log`: current-versus-half-texel 1:1 controls; and
- `hardware-20260729-133408.log`: current-versus-`logical - 1` scaled edge
  comparisons.

The authoritative captures are:

- `pvr_rtt_microprobe-final-page1.ppm` through `final-page8.ppm`;
- `pvr_rtt_microprobe-1to1-page9.ppm` and `1to1-page10.ppm`; and
- `pvr_rtt_microprobe-max-inset-page11.ppm` and
  `max-inset-page12.ppm`.

All three programs returned zero normally to `dc-load-ip`. Two earlier
development runs entered the wrong test path because SDL's Dreamcast entry
wrapper did not forward the loader argument as expected. Those runs are not
validation evidence. Hardware tests must never be terminated externally while
their ELF is running; an early diagnostic return must use an in-program
`arch_exit()` path.

### 11.2 Exact preservation result

The corrected seed sequence performs the old-destination copy and the new SDL
draws in the **same** RTT scene. Raw captures show both markers in every case:

- 640x480 offscreen logical screen: red S1 and green S2 both survive four
  current/next seed scenes;
- 128x128 target: red T1 and green T2 both survive eight seed scenes;
- 512x512 target: red T1 and green T2 both survive four seed scenes; and
- 160x120 target-selected present: blue T1 and yellow T2 both survive when the
  second scene begins with an explicit seed.

This physically validates the core preservation operation:

```text
begin RTT(next)
draw current -> next with NONE blending
draw new SDL commands into next
finish RTT(next)
swap current/next ownership
```

It also validates presenting an offscreen 640x480 logical screen by sampling
the completed current allocation in a physical screen scene.

### 11.3 Measured VRAM

`pvr_mem_available()` immediately after renderer creation was 5,943,040 bytes.

| Logical target | First allocation | Second allocation | Remaining after both |
|---|---:|---:|---:|
| 640x480 (1024x512 backing) | 1,048,632 B | 1,048,608 B | 3,845,800 B |
| 128x128 | 32,800 B | 32,800 B | 5,877,416 B |
| 512x512 | 524,320 B | 524,320 B | 4,894,376 B |

The small amount above the pixel payload is allocator overhead. Destroying the
128x128 and 512x512 pairs restored their exact preallocation availability. The
first large pair left a one-time 24-byte difference; subsequent baselines were
stable, so the probe found no accumulating allocation loss.

A permanent POT-backed logical-screen pair consumes about 2 MiB and leaves
about 3.67 MiB of the current PVR texture heap for application textures,
targets, and preservation scratch. This is feasible but a substantial fixed
cost.

### 11.4 Completed-scene timing

The measurements surround target selection, seed draw submission, and
target-selected `SDL_RenderPresent`, including the render-completion wait. They
are end-to-end scene times, not isolated texture-read bandwidth:

| Seed scene | Count | Minimum | Average | Maximum |
|---|---:|---:|---:|---:|
| 640x480 logical screen | 4 | 6.528 ms | 6.680 ms | 6.968 ms |
| 128x128 target | 8 | 3.341 ms | 3.351 ms | 3.369 ms |
| 512x512 target | 4 | 5.864 ms | 5.988 ms | 6.180 ms |
| 160x120 selected-present | 1 | 3.411 ms | 3.411 ms | 3.411 ms |

The roughly 3.3 ms small-scene floor includes scene lifecycle and completion
latency. A full 640x480 preservation boundary costs about 6.7 ms in this direct
prototype path. Leading full clears and post-present undefined-screen state can
avoid some seeds, but those are later proven optimizations.

### 11.5 Edge-root-cause result

The physical matrix establishes three facts:

1. Current and half-texel UV variants sampled 128x128, 127x93, 160x120, and
   256x128 targets at 1:1 with intact two-pixel borders. The Page 9 and Page 10
   raw captures are byte-identical. RTT source edges and KOS inclusive pixel
   clipping are correct.
2. Enlarged current-endpoint sampling reproduces periodic bottom/right
   contamination for 127x93 and 256x128. Half-texel endpoints improve linear
   filtering but still land on an ambiguous texel boundary and do not fully
   fix enlarged nearest output.
3. Keeping the minimum at zero and mapping the maximum to the last PVR texel
   center fixes both nearest and linear output:

   ```text
   u_min = 0
   v_min = 0
   u_max = (logical_w - 1) / backing_w
   v_max = (logical_h - 1) / backing_h
   ```

   The right-column cells in physical Pages 11 and 12 have complete white,
   magenta, cyan, and yellow borders, while the current-UV left cells retain
   the diagnostic contamination.

The PVR's effective texel centers for this path are therefore `i / backing`,
not the OpenGL-style edge convention assumed by the original
`logical / backing` maximum. Do not modify `PVR_PCLIP_X/Y`.

### 11.6 Revised verdict

**The bounded physical research is complete, and a guarded second coding pass
is feasible.**

The implementation must include both parts together:

1. an offscreen logical-screen preservation path plus current/next RTT
   preservation; and
2. target-texture UV mapping whose logical maximum selects
   `(logical - 1) / backing`.

Per-target ping-pong without logical-screen preservation remains insufficient.
Ordinary `SDL_RENDERER_TARGETTEXTURE` advertisement must remain disabled until
the integrated guarded prototype passes the original Pages 3, 7, 8, and 9,
the full edge matrix, repeated-seed fidelity checks, VRAM exhaustion cleanup,
and strict ordinary-build isolation on physical hardware.

---

## 12. Guarded-second-prototype implementation handoff (2026-07-29)

### 12.1 Files modified

1. `src/render/dreamcast/SDL_render_dreamcast_pvr_data.h`
   - Added `vram_next`, `current_valid` to `DC_PVR_TextureData` (under guard).
   - Added `screen_vram_current`, `screen_vram_next`, `screen_alloc_size`,
     `screen_stride_px`, `screen_backing_w`, `screen_backing_h`,
     `screen_current_valid`, `scene_kind` to `DC_PVR_RenderData` (under guard).

2. `src/render/dreamcast/SDL_render_dreamcast_pvr.c`
   - Added `DC_PVR_SceneKind` enum at file scope.
   - Added `DC_PVR_NextPowerOfTwo` static helper.
   - Added `DC_PVR_EmitPreservationSeed` (edge-safe UVs, NONE blend, nearest).
   - Rewrote `DC_PVR_FinishActiveScene` to handle all scene kinds and mark
     the destination valid only on successful finish.
   - Rewrote `DC_PVR_BeginSceneForCurrentTarget` to begin RTT scene/list
     before emitting the preservation seed, skip seed when current is
     undefined, and handle both target and logical-screen destinations.
   - Rewrote `DC_PVR_SetRenderTarget` to swap current/next after successful
     finish and require both VRAM allocations.
   - Rewrote `DC_PVR_RenderPresent` to handle target-selected present (keep
     target selected, commit RTT) and screen-selected present (begin physical
     screen scene, draw completed logical screen, finish, do not swap screen
     allocations, mark next frame undefined).
   - Updated `DC_PVR_DestroyRenderer` to free screen allocations and reset
     `scene_kind`.
   - Updated `DC_PVR_CreateRenderer` to allocate two screen VRAM surfaces
     after `pvr_init()` with proper failure cleanup.
   - Updated `DC_PVR_EmitHeader` to check `current_valid` instead of
     `rtt_completed` for target textures.

3. `src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c`
   - Updated `DC_PVR_CreateTexture` for targets to allocate `vram_next` and
     set `current_valid = SDL_FALSE`.
   - Updated `DC_PVR_DestroyTexture` to free `vram_next`.

4. `src/render/dreamcast/SDL_render_dreamcast_pvr_geometry.c`
   - Updated `DC_PVR_QueueGeometry` UV mapping: completed targets use the
     edge-safe formula `(logical - 1) / backing`; non-target and uncompleted
     textures keep standard `logical / backing`.

5. `test/dreamcast/test/pvr_render_target/Makefile.dc`
   - Added check for `_DC_PVR_SetRenderTarget` in the requested archive.

6. `test/dreamcast/test/pvr_render_target/pvr_render_target.c`
   - Updated screenshot paths to `pvr_render_target-phase5-pageN.ppm`.
   - Added `PAGE_COUNT = 12`.
   - Added `page11_repeated_seeds` (6 gradient cells after marker A).
   - Added `page12_allocation_failure` (256x256 target, 8x32 create/destroy
     cycle, green recovery bar).
   - Updated `main` to call both new pages.

### 12.2 State-transition and swap rules

**Scene kinds:**
- `DC_PVR_SCENE_NONE`: no active scene.
- `DC_PVR_SCENE_LOGICAL_SCREEN`: rendering into logical-screen RTT.
- `DC_PVR_SCENE_TARGET`: rendering into a target RTT (`scene_target != NULL`).
- `DC_PVR_SCENE_PHYSICAL`: physical screen presentation.

**Commit operation (centralized in `DC_PVR_FinishActiveScene`):**

For both target and logical-screen RTT scenes, the commit operation is:
1. Begin RTT into `next` allocation.
2. Mark scene active.
3. Begin `PVR_LIST_TR_POLY`.
4. If `current_valid` and `current != next`: emit preservation seed from `current` to `next`.
5. Emit queued drawing.
6. Finish list and scene.
7. Only after successful scene completion:
   - Swap `current` and `next`.
   - Set `current_valid = SDL_TRUE`.

**Logical-screen lifecycle:**
1. After renderer creation: `screen_current_valid = SDL_FALSE` (undefined).
2. First draw command begins logical-screen RTT into `screen_vram_next`.
   No seed (current is undefined).
3. On commit (FinishActiveScene): swap current/next, mark current valid.
4. On `SDL_RenderPresent` (screen selected):
   - Finish logical-screen RTT scene (commit/swap handled above).
   - `screen_current_valid = SDL_TRUE` (after commit).
   - Begin physical screen scene.
   - Draw `screen_vram_current` fullscreen with NONE blending.
   - Finish physical scene.
   - Keep `screen_current_valid = SDL_TRUE` (do not clear).
5. On target excursion (screen -> target -> screen):
   - Finish logical-screen RTT (commit/swap handled above).
   - `screen_current_valid = SDL_TRUE`.
   - Begin target RTT.
   - On return to screen: begin logical-screen RTT into `screen_vram_next`.
     Emit seed from `screen_vram_current` (current is valid and distinct
     from next).
   - Draw SDL commands.
   - Commit (swap, mark valid).

**Target lifecycle:**
1. After target creation: `current_valid = SDL_FALSE` (undefined).
2. First draw into target begins RTT into `vram_next`. No seed.
3. On commit (FinishActiveScene): swap current/next, mark current valid.
4. On target switch away: FinishActiveScene handles commit/swap.
5. On target resume: BeginSceneForCurrentTarget begins RTT into `vram_next`.
   If `current_valid && vram != vram_next`: emit preservation seed after
   `pvr_list_begin`.
   Draw SDL commands.
   Commit (swap, mark valid).

**SetRenderTarget:**
- Finish active scene (commit/swap handled in FinishActiveScene).
- Change `current_target` only. Do not swap allocations here.

**RenderPresent (target selected):**
- Finish active RTT scene (commit/swap handled above).
- Keep target selected. Do not present physical screen.
- Continue through the common `pvr_wait_render_done`, deferred-release, and
  scene-reference cleanup path.

**RenderPresent (screen selected):**
- Finish logical-screen RTT (commit/swap handled above).
- Require `screen_current_valid == SDL_TRUE`.
- Begin physical screen scene.
- Draw `screen_vram_current` fullscreen.
- Finish physical scene.
- Keep `screen_current_valid = SDL_TRUE` (do not clear).
- Do not swap screen allocations.

### 12.3 Allocation and rollback behavior

**Logical-screen allocations:**
- Two `pvr_mem_malloc` calls after successful `pvr_init()`.
- Each allocation is `screen_backing_w * screen_backing_h * 2` bytes.
- For 640x480: backing 1024x512, allocation 1,048,576 bytes each, total ~2 MiB.
- If first allocation fails: free renderer data, return SDL error.
- If second allocation fails: free first allocation, free renderer data,
  return SDL error.
- Both allocations freed in `DC_PVR_DestroyRenderer`.

**Target allocations:**
- Two `pvr_mem_malloc` calls in `DC_PVR_CreateTexture` for target access.
- Each allocation is `backing_w * backing_h * 2` bytes.
- The prototype allocates `vram_next` before the primary `vram` allocation.
  If the primary allocation is injected to fail or actually fails, free
  `vram_next` before freeing texture data and returning the SDL error.
- Both allocations owned by the texture. Pointer swapping does not place
  either on the deferred-free list.
- Both freed in `DC_PVR_DestroyTexture`.

**Failure cleanup:**
- If `pvr_init()` fails: free renderer data, return SDL error.
- If screen allocation fails after `pvr_init()`: free renderer data,
  return SDL error.
- If target allocation fails: free first allocation, free texture data,
  return SDL error.
- If a scene begin fails: set `pvr_failed = SDL_TRUE`, do not swap current
  contents, leave the renderer in a failed state.
- If a scene finish fails: set `pvr_failed = SDL_TRUE`, do not swap current
  contents, leave the renderer in a failed state.
- If presentation fails: set `pvr_failed = SDL_TRUE`, leave the renderer
  in a failed state.

**Deferred-free behavior:**
- The existing deferred-texture and deferred-VRAM mechanisms are unchanged.
- Swapped allocations are not added to the deferred-free list.
- Target destruction releases both allocations after any outstanding
  rendering references finish.

### 12.4 UV mapping rules

**Completed targets (texture selected, current_valid == SDL_TRUE):**
```c
u_max = (logical_w <= 1) ? 0.0f :
        (float)(logical_w - 1) / (float)backing_w;
v_max = (logical_h <= 1) ? 0.0f :
        (float)(logical_h - 1) / (float)backing_h;
vertices[i].u = source_uv[0] * u_max;
vertices[i].v = source_uv[1] * v_max;
```

**Non-target and uncompleted textures:**
```c
u_max = (float)logical_w / (float)backing_w;
v_max = (float)logical_h / (float)backing_h;
vertices[i].u = source_uv[0] * u_max;
vertices[i].v = source_uv[1] * v_max;
```

### 12.5 Build verification

**Ordinary archive:**
- `cd build-scripts/dcbuild && make -j4` (clean, no warnings).
- `sh-elf-nm libSDL2.a | grep _DC_PVR_SetRenderTarget` returns empty.

**Prototype archive:**
- `cd build-scripts/dcbuild_prototype && make -j4` (clean, no warnings).
- `sh-elf-nm libSDL2.a | grep _DC_PVR_SetRenderTarget` returns `t _DC_PVR_SetRenderTarget`.

**Test ELF:**
- `cd test/dreamcast/test/pvr_render_target && make -f Makefile.dc -B`
  `HARDWARE_SCREENSHOTS=1 SDL2_LIBDIR=.../dcbuild_prototype`.
- Strict warnings: `-Wall -Wextra -Werror`.
- Linker map confirms linkage to `dcbuild_prototype/libSDL2.a`.
- `sh-elf-nm pvr_render_target.elf | grep _DC_PVR_SetRenderTarget` returns
  `t _DC_PVR_SetRenderTarget`.

### 12.6 Validation expectations

**Physical hardware (640x480 VGA):**
- All 12 pages display as documented.
- Pages 3, 7, 8, 9: destination persistence preserved.
- Pages 11, 12: new guard cases pass.
- No freeze, reset, timeout, or crash.
- Program prints `PASS`, returns 0, returns control to `dc-load-ip`.

**Capture files:**
- Final corrected run under `test/dreamcast/test/pvr_render_target/`:
  `pvr_render_target_phase5b_page1.ppm` through
  `pvr_render_target_phase5b_page12.ppm`.
- Paths are unique; do not overwrite existing `pvr_render_target-hardware-pageN.ppm`.

**Hardware log:**
- Capture log path: `test/dreamcast/test/pvr_render_target/hardware-20260729-115XXX.log`.
- Do not overwrite existing `hardware-20260729-114256.log`.

### 12.7 Next steps after validation

1. Verify all 12 pages on physical hardware with retained screenshots.
2. Compare against GLdc renderer behavior for regression detection.
3. If validation passes: consider removing the prototype guard.
4. If validation fails: diagnose based on the documented expectations and
   the physical microprobe results in Section 11.
5. Add ordinary `SDL_RENDERER_TARGETTEXTURE` capability advertisement only
   after the guarded prototype passes all physical validation.

### 12.8 First guarded-prototype hardware attempt and correction

The first physical launch of the guarded second prototype reached page 1 but
failed while finishing its first target excursion. The authoritative serial
log is `test/dreamcast/test/pvr_render_target/hardware-20260729-150242.log`.
KOS reported `pvr_prim: attempt to submit data unaligned to 8 bytes` and SDL
reported `Dreamcast PVR rejected a render primitive`. No page capture was
produced.

The preservation seed emitter had submitted the compact 20-byte
`DC_PVR_Vertex` ingestion type directly. The corrected emitter converts each
seed vertex to the same 32-byte `pvr_vertex_t` command used by ordinary
geometry, including `PVR_CMD_VERTEX`/`PVR_CMD_VERTEX_EOL`, `z = 1`, UV, and
ARGB fields.

Failure cleanup also exposed two independent ordering defects. Renderer
destruction attempted to finish an already-finished list a second time, then
shut PVR down before freeing its logical-screen allocations. Cleanup now
finishes an active list at most once, frees logical-screen VRAM before a
synchronized shutdown, and deliberately abandons those allocator pointers
after a failed-synchronization recovery shutdown. The program returned control
to `dc-load-ip`; the failed launch is evidence only, not a visual result.

The next physical run, logged as
`test/dreamcast/test/pvr_render_target/hardware-20260729-150436.log`, completed
all 12 pages, reported 10 API checks with zero failures, and returned zero.
Direct inspection passed pages 1--10 and 12. Page 11 showed four rather than
six gradient cells because the test placed the third 8-pixel column at
`x = 64` in a 64-pixel target. Both third-column cells were therefore wholly
outside the half-open target bounds. The corrected page uses in-bounds column
positions 36, 46, and 56 and writes a new `phase5b` capture set so the first
run remains preserved.

### 12.9 Final guarded-prototype hardware validation

The corrected phase5b run is authoritative:

- Serial log:
  `test/dreamcast/test/pvr_render_target/hardware-20260729-150808.log`.
- Raw captures under `test/dreamcast/test/pvr_render_target/`:
  `pvr_render_target_phase5b_page1.ppm` through
  `pvr_render_target_phase5b_page12.ppm`.
- Runtime: 10 API checks, zero failures, all 12 captures written, program
  returned zero normally to `dc-load-ip`.
- Pages 1--2: API state restoration, target orientation, target clear,
  viewport, and clipping passed.
- Pages 3--4: screen S1/S2 persistence and target A/B switching passed.
- Pages 5--6: NONE/BLEND/ADD/MUL and filtering/modulation rendered without
  visible corruption.
- Pages 7--8: target persistence across reselection and target-selected
  `SDL_RenderPresent` passed with both markers retained.
- Page 9: feedback rejection and lifecycle recovery passed; the centered
  target and bottom recovery bar are intact.
- Page 10: the NPOT cyan/white/yellow/magenta border is complete, orientation
  is correct, and there is no stale-padding contamination.
- Page 11: the stable marker and all six ordered repeated-seed cells are
  present without accumulating edge damage.
- Page 12: the allocator lifecycle sequence completed and the green recovery
  bar is intact.

Direct inspection found no missing geometry, corruption, freeze, reset,
timeout, incomplete presentation, or failed recovery. This validates the
guarded preservation design on the tested 640x480 VGA hardware path. Ordinary
builds remain isolated and continue to omit target-texture capability pending
an explicit promotion decision.

### 12.10 Bounded VRAM-pressure follow-up

The focused test under `test/dreamcast/test/pvr_render_target_stress/` passed
on the same physical 640x480 VGA Dreamcast. Its authoritative serial log is
`hardware-20260729-154438.log` and its raw recovery framebuffer is
`pvr_render_target_stress-hardware-final.ppm`. The earlier `154230` run's
final framebuffer is byte-identical and remains retained without the `-final`
suffix.

After the renderer's paired logical-screen allocation, the texture heap had
3,845,800 bytes available. Fourteen paired 256x256 RGB565 targets succeeded.
Target 15 failed during its second allocation with exactly 174,888 bytes both
before and after the failed creation, proving exact partial-allocation
rollback. Freeing all 14 targets restored exactly 3,845,800 bytes.

Ten repeated exhaustion cycles produced the same 14-success/15-failure result
and exact baseline recovery without accumulation. A mixed sequence of 24
64x64, 128x128, and 256x256 targets survived reverse-order destruction,
returned to the exact baseline, and permitted a later 256x256 target to render
successfully. The final sampled target, target-corner markers, center marker,
screen-corner markers, and recovery bar are intact. The program reported four
checks, zero failures, `PASS`, and returned zero normally.

This closes the bounded VRAM exhaustion, partial-allocation rollback,
fragmentation, and post-exhaustion recovery gates for the guarded prototype.
Wider video-mode and performance-policy decisions remain separate promotion
questions rather than failures of this tested allocation path.

### 12.11 Physical performance gate and promotion decision

The focused comparison under
`test/dreamcast/test/pvr_render_target_performance/` built ordinary and guarded
prototype ELFs from one workload source, using explicit local archives and
strict warnings. Both final ELFs passed symbol isolation checks, ran on the
same physical 640x480 VGA Dreamcast, wrote raw framebuffer captures, reported
zero failures, returned zero, and returned normally to `dc-load-ip`.

The full measured interval covers SDL command ingestion, command execution,
scene completion, physical presentation, `pvr_wait_render_done()`, and one
explicit vertical-blank wait. Each workload uses 60 warmup frames and 300
measured frames.

| Workload | Ordinary mean | Prototype mean | Delta | Ordinary FPS | Prototype FPS |
|---|---:|---:|---:|---:|---:|
| A: clear | 16,679 us | 16,675 us | -4 us | 59.954 | 59.969 |
| B: 100 rectangles | 16,678 us | 22,236 us | +5,558 us | 59.956 | 44.970 |
| C: 200 texture copies | 16,678 us | 33,356 us | +16,678 us | 59.956 | 29.978 |
| D: 400 mixed draws | 33,361 us | 50,039 us | +16,678 us | 29.974 | 19.984 |

Prototype-only target update E averages 33,357 us (29.978 FPS), and repeated
preservation F averages 25,012 us (39.980 FPS). These are reported separately
and are not used as ordinary/prototype screen-only deltas.

Available texture VRAM after renderer creation is 5,943,040 bytes for the
ordinary build and 3,845,800 bytes for the prototype, a 2,097,240-byte
reduction. Direct inspection of both final raw captures passes: the shared
reference scene and all four corner probes are intact, and the prototype's
sampled preservation target retains both markers.

Authoritative evidence:

- `ordinary-hardware-20260729-final.log`
- `prototype-hardware-20260729-final.log`
- `pvr_render_target_performance-ordinary-final.ppm`
- `pvr_render_target_performance-prototype-final.ppm`

The current design is functionally validated but fails the promotion policy
gate. Its always-on paired logical-screen RTT path materially slows ordinary
screen-only workloads B-D and reserves about 2 MiB even when no target texture
is created. Keep `DC_PVR_RENDER_TARGET_PROTOTYPE` and do not advertise target
textures in the ordinary renderer. Future promotion must avoid the unconditional
logical-screen cost or adopt a deliberate opt-in policy.

---

**End of document.**
