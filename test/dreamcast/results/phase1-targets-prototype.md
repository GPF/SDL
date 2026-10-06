# Historical guarded Phase 1 RGB565 target implementation — hardware PASS

This entry preserves the original guarded implementation evidence. The RGB565
target path has since been promoted to normal Dreamcast PVR builds.

Built the screenshot-enabled `pvr_phase1_targets` fixture against the local
`libSDL3.a` from the guarded implementation, then ran it on physical hardware at `192.168.0.128` through
`tools/dc-load/kos-tool-locked.sh` with `-m .`. The fixture asserted the native
renderer name and returned exit code 0.

Observed checks:

- Screen → target A → screen preserved the red screen region and later yellow
  region.
- A → B → A preserved both targets; completed targets sampled correctly to
  screen.
- 127×93 NPOT target sampling showed all four red, green, blue, and white edge
  texels.
- `SDL_RenderPresent()` rejected while a target was selected; that target
  remained selected and usable afterward.
- Injected target-switch failure restored the screen destination. The selected
  target had a 2× SDR white point; the subsequent screen gray patch captured as
  RGB 128, verifying color-scale rollback.

Exact hardware markers:

```
pvr_targets renderer=Dreamcast PVR
pvr_targets warmup=PASS frames=3
pvr_targets failed_switch_rollback=PASS target=screen
pvr_targets screen_target_screen=PASS
pvr_targets target_a_b_a_sample=PASS
pvr_targets npot_edges=PASS
pvr_targets selected_target_present_rejected=PASS
pvr_targets target_usable_after_rejection=PASS
pvr_targets result=PASS (inspect pages 1-4)
Program returned 0
```

Captures: [`phase1-captures/`](phase1-captures/). Page 1 includes the gray
rollback patch; page 3 contains the NPOT edge probe. Captures come from the
physical framebuffer and are not renderer readback.

At the time of the initial matrix, repeated switching and allocation recovery
remained open. Those gates and the completion source review are now recorded
below; bounded physical VRAM exhaustion remains untested. That initial status was superseded by the subsequent P1-A through P1-D results
and canonical P1-C evidence below.

## Phase 1 hardening gates (2026-10-05)

Added `test/dreamcast/pvr_phase1_hardening/` as a finite physical-hardware
fixture. It runs 64 screen/A/B cycles, keeps destination markers, changes and
restores target viewport/clip state, and samples both targets after each cycle.
It also injects failures at screen allocation steps 1/2 and target allocation
steps 1/2, then verifies recovery by creating and drawing to another target.
The allocation injector is compiled only with the guarded target build.

The guarded library was built through the repository's Dreamcast build flow,
installed to a writable workspace prefix, and linked with the established
Dreamcast test Makefile flow. Separate P1-A and P1-B ELFs were launched in
independent locked sessions on physical hardware. An empty
`SDL3_CONFIG_INCLUDEDIR` initially swallowed the screenshot define after an
empty `-I`; the fixture Makefile now adds that include path only when supplied.
The final screenshot-enabled builds and both hardware runs passed.

P1-A **PASS on physical hardware**. The independent run asserted
`renderer=Dreamcast PVR`, completed 64 screen/A/B cycles with viewport/clip
churn and repeated target sampling. It leaves distinct A/B viewport and clip
settings installed and asserts their restoration after reselecting each target;
it also asserts the screen view is restored. The run returned code 0 and showed
the red screen marker, green/blue target markers, and yellow target updates in
the physical capture.

P1-B **PASS on physical hardware**, in a separate run after P1-A passed. Both
logical-screen allocation injection steps and both target allocation steps
failed at the intended ordinal. The paired-target failure path freed its first
allocation; each failure was followed by successful renderer/target use, and
ordinary screen rendering completed afterward. The purple recovery target was
visible in the captured frame. The fixture returned code 0.

KOS source confirms `pvr_scene_finish()` returns 0 after normal submission;
`pvr_list_finish()` returns failure only for an invalid open-list state in its
debug guard. `pvr_wait_render_done()` can time out. The guarded backend treats
that timeout as fatal before swapping current/next, retaining the former
current allocation and failing closed for later work. P1-D is therefore a
source-level proof, not an injectable hardware gate. No fake completion error
was introduced.

Core rollback re-check: `SDL_SetRenderTarget()` saves and restores
`renderer->target` and `renderer->view`, then calls `UpdateColorScale()` after
backend rejection. The saved view object contains the target-specific viewport
and clip state, and `UpdateColorScale()` recalculates target white point state.
No concrete missing restoration was found; the core change was not broadened.

Historical SDL2 research consulted: `PVR_RENDER_TARGET_PRESERVATION_RESEARCH.md`
and `PVR_ORDERED_RENDER_TARGET_RESEARCH.md`. The implementation retains paired
allocations and validated PVR texture seed behavior from that evidence.

P1-C reached real allocator failure after 233 successful uniform 64x64 targets
with a 320-target cap. It freed 230, reallocated all 230, sampled retained and
reallocated targets, freed the reallocated subset, then created and submitted a
fresh target. The expected KOS out-of-memory warning appeared. The program
returned code 0, but the physical recovery capture appeared black despite the
API-level PASS markers. At that point P1-C was **NOT PASS**: visible retained,
reallocated, and fresh-target output was not confirmed. The first 160-target
cap did not reach exhaustion and is retained as calibration evidence. No extra
present or delay was added to conceal the black capture.

### P1-C diagnostic comparison (2026-10-06)

Without changing renderer code, the fixture now renders one shared provenance
scene both without exhaustion and after exhaustion/reallocation. It has four
obvious regions: retained pre-exhaustion RGB565 target (red), freed/reallocated
target (magenta), fresh post-recovery target (yellow), and an untextured cyan
screen quad. Both the no-exhaustion control and post-exhaustion capture show
all four regions. Both runs report `SDL_RenderPresent()` success and return 0.

This establishes that the logical-screen commit/presentation path and all three
sampled target generations can render after exhaustion in this diagnostic
scene. Source ordering agrees: target scenes swap current/next only after
`pvr_wait_render_done()` succeeds; target changes and scene completion
invalidate the polygon header cache; sampling compiles the current
`texturedata->vram` pointer. After the prior screen present the logical screen
is intentionally marked invalid, then the provenance scene writes to
`screen_vram_next`; `SDL_RenderPresent()` finishes/waits/commits that scene,
copies `screen_vram` to the physical framebuffer, finishes/waits again, and
returns success.

At this first diagnostic review, the earlier all-black five-texture recovery
frame was not reproduced by the four-source diagnostic, and P1-C was still
**UNRESOLVED** pending more controlled pressure evidence. The exact five-texture
scene remains unavailable. No allocation or renderer behavior was changed.
Evidence: `pvr_phase1_hardening_p1c_control.log`,
`pvr_phase1_hardening_p1c_diagnostic.log`,
`pvr_phase1_hardening_vram_control.ppm`, and
`pvr_phase1_hardening_vram_recovery.ppm`.

Gate summary at the first diagnostic review: P1-A **HARDWARE PASS**; P1-B
**HARDWARE PASS**; P1-C **UNRESOLVED**; P1-D **SOURCE REVIEW PASS**. Durable evidence is in
`results/phase1-captures/pvr_phase1_hardening_p1a.log`,
`results/phase1-captures/pvr_phase1_hardening_stress.ppm`,
`results/phase1-captures/pvr_phase1_hardening_p1b.log`, and
`results/phase1-captures/pvr_phase1_hardening_recovery.ppm`, plus the P1-C logs
`results/phase1-captures/pvr_phase1_hardening_p1c.log`,
`results/phase1-captures/pvr_phase1_hardening_p1c_cap160.log`, plus the
diagnostic control and pressure logs/captures listed above. The current
`pvr_phase1_hardening_vram_recovery.ppm` is the colored four-source diagnostic
capture; the earlier black capture was overwritten and is not durable.

At that review point the recommendation remained **KEEP GUARDED**. The later
canonical repeated comparison below supersedes the P1-C status, while target
support remains guarded.

## Canonical P1-C repeated hardware comparison (2026-10-06)

The original five-texture scene remains unreconstructable. To establish a
repeatable pressure gate without pretending to reproduce it, the fixture now
defines a canonical four-source scene in source: three uniform 64x64 RGB565
targets (retained red, freed/reallocated magenta, fresh yellow), plus one
untextured cyan rectangle. Each target is black with an 18x18 marker at (4,4).
The final logical-screen draw clears black, samples those three textures in
that order into 96x96 rectangles at (32,48), (168,48), and (304,48), and draws
the cyan rectangle at (440,48). Targets use default viewport and disabled
clipping. The scene has one final `SDL_RenderPresent()`. Control and pressure
runs use identical scene setup and draw code.

The pressure sequence retains three marked targets, allocates until real
failure at 233 successful 64x64 targets (320 cap), frees 230, successfully
reallocates 230, writes the magenta marker to the first replacement, frees the
other replacements, and writes the yellow marker to a fresh target. KOS's
allocator warning occurs at the deliberate exhaustion point.

The matched baseline hardware comparison initially appeared to produce
**Outcome B**: the control capture was correct, while all three single pressure
captures showed only the prior warmup page. Each run
asserted `renderer=Dreamcast PVR`, reported API-level PASS, and returned 0.
The pressure runs all reported the same successful allocation count and
reallocation count. Texture object pointers were stable across the three
pressure processes (`retained0=0x8c14f648`, `reallocated0=0x8c18f3f0`,
`fresh=0x8c18c9d8`); these are SDL texture object addresses, not VRAM pointers.
The control used distinct expected object addresses. Renderer-internal screen
and target VRAM pointers were not instrumented in this fixture.

Baseline evidence:

- Control: `pvr_phase1_hardening_p1c_canonical_control.log/.ppm`
- Pressure 1: `pvr_phase1_hardening_p1c_canonical_pressure_1.log/.ppm`
- Pressure 2: `pvr_phase1_hardening_p1c_canonical_pressure_2.log/.ppm`
- Pressure 3: `pvr_phase1_hardening_p1c_canonical_pressure_3.log/.ppm`

A matched checkpoint diagnostic captured the framebuffer after each of the
three existing warmup presents and before the final scene. The warmup and
pre-final captures showed the same white-square framebuffer in both runs.
The initial pressure capture was pixel-for-pixel identical to its pre-final
capture, showing the prior physical page rather than the recovery markers.

The next diagnostic issued exactly one render `SDL_RenderPresent()` and captured
after each of the first three following vblanks; it added no render presents.
The control and pressure diagnostic both showed the complete final scene in all
three captures. Three further pressure repetitions reached exhaustion and
reallocated 230 targets each. In one repetition, vblank 1 still showed the
warmup page; vblanks 2 and 3 showed the final scene. The other two repetitions
showed the final scene from vblank 1 onward. Across all three repetitions,
control and pressure captures were pixel-identical at vblanks 2 and 3
(ImageMagick absolute pixel difference 0). Thus all four pressure runs with vblank-series
capture show correct retained, reallocated, fresh, and untextured markers by
vblank 2. The stale vblank-1 page is a presentation/capture timing observation.

Evidence includes `pvr_phase1_hardening_p1c_diag2_control.log` and
`pvr_phase1_hardening_p1c_diag2_pressure.log` with warmup/pre-final/final
captures; `pvr_phase1_hardening_p1c_diag3_control.log` and
`pvr_phase1_hardening_p1c_diag3_pressure.log`; and the repeated logs
`pvr_phase1_hardening_p1c_repeat_control.log` and
`pvr_phase1_hardening_p1c_repeat_pressure_{1,2,3}.log`. Each repeated run has
three distinct `*_final_vblank_{1,2,3}.ppm` captures.

Source review remains consistent: swaps follow successful completion/wait,
target switches and scene completion invalidate the header cache, and sampling
uses the current texture VRAM pointer. No renderer allocation, preservation,
commit/swap, or presentation logic was changed. The original exact five-texture
scene remains unreconstructable; the result above closes P1-C for the new
canonical fixture only.

P1-C is **HARDWARE PASS for the canonical fixture** with the fixed three-vblank
capture sequence. The earlier stale-page image is retained as a capture timing
observation, not erased as if it had not occurred. Target support remains
**KEEP GUARDED**; no promotion was made in this diagnostic task.

## Promotion status

RGB565 target preservation is now unconditional in normal PVR builds. The
production path retains paired `current`/`next` buffers, completed-target
sampling, validated NPOT edge UVs, and SDL3 target rollback. Unsupported target
formats are rejected before SDL3 can silently wrap a closest-format native
target. Failure injection remains separately guarded by
`SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS`.

Post-promotion hardware smoke: **PASS on physical Dreamcast (2026-10-06)**.
The normal, injection-free build passed target creation, unsupported ARGB1555
target rejection, screen/target/screen preservation, A/B switching and
sampling, 127×93 NPOT edge sampling, selected-target present rejection, and
continued target usability. Normal-build P1-A passed 64 switch cycles with
viewport/clip restoration. A separate `SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS=1`
build passed P1-B paired-allocation failure recovery and subsequent screen
rendering. Fixture markers and physical framebuffer captures are retained in
`results/phase1-captures/pvr_phase1_promotion_smoke.log` and the adjacent
`pvr_phase1_promotion_*.ppm` files. The selected-target switch-failure injection
was not part of the normal-build smoke; its prior guarded physical PASS remains
recorded above. The historical five-texture black frame remains
unreconstructable. Canonical P1-C control and pressure captures matched at
vblanks 2 and 3, supporting the documented capture-page timing interpretation
for the scoped promotion decision.
