# Bounded VRAM-Pressure and Allocation-Rollback Test

**Purpose:** Determine whether bounded target-texture VRAM exhaustion fails
cleanly, releases both current/next allocations correctly, and permits known-
good rendering and target creation afterward.

**Guard:** `DC_PVR_RENDER_TARGET_PROTOTYPE` — does not promote or remove the
prototype. The renderer source is not modified in this pass.

**Scope:** RGB565 target textures only, bounded allocation arrays, repeated
rollback cycles, mixed-size fragmentation probe, and a single post-recovery
screenshot.

## Build

```sh
cd test/dreamcast/test/pvr_render_target_stress
make -f Makefile.dc -B HARDWARE_SCREENSHOTS=1
```

The Makefile verifies that the requested archive contains both
`_DC_RenderDriver` and `_DC_PVR_SetRenderTarget`. It passes the prototype
archive file path directly to the linker so the KOS wrapper does not silently
substitute the installed `kos/addons/lib/dreamcast/libSDL2.a`.

## Build result

```
kos-cc -I/opt/toolchains/dc/kos/addons/include/SDL2 -DDREAMCAST -D__DREAMCAST__ \
	-Wall -Wextra -Werror -DDC_PVR_RENDER_TARGET_PROTOTYPE -c pvr_render_target_stress.c -o pvr_render_target_stress.o
kos-cc -o pvr_render_target_stress.elf pvr_render_target_stress.o -Wl,-Map,pvr_render_target_stress.map \
	/home/gpf/code/dreamcast/SDL2/build-scripts/dcbuild_prototype/libSDL2.a /opt/toolchains/dc/kos/addons/lib/dreamcast/libSDL2main.a \
	-lGL -lm -lkosutils -lpthread
```

Zero warnings, zero errors.

## Linker-map evidence

All SDL2 symbols resolve from the prototype archive:

```
/home/gpf/code/dreamcast/SDL2/build-scripts/dcbuild_prototype/libSDL2.a(SDL.c.obj)
/home/gpf/code/dreamcast/SDL2/build-scripts/dcbuild_prototype/libSDL2.a(SDL_render.c.obj)
...
```

Key prototype symbols present in the ELF:

```
8c04a4d4 t _DC_PVR_CreateTexture
8c049ad0 t _DC_PVR_DestroyTexture
8c04931c t _DC_PVR_SetRenderTarget
8c1104c4 D _DC_RenderDriver
```

## Output paths

| Artifact | Path |
|----------|------|
| ELF | `test/dreamcast/test/pvr_render_target_stress/pvr_render_target_stress.elf` |
| BIN | `test/dreamcast/test/pvr_render_target_stress/pvr_render_target_stress.bin` |
| MAP | `test/dreamcast/test/pvr_render_target_stress/pvr_render_target_stress.map` |

## Expected serial milestones

1. **Phase 1 — baseline:** `pvr_mem_available()`, renderer name/flags, output
   dimensions, per-target dual-allocation cost (262,144 bytes for 256×256).
2. **Phase 2 — exhaustion:** VRAM before/after every creation attempt, clean
   exhaustion message when `SDL_CreateTexture` returns NULL, per-target
   count, VRAM consumed, and rollback confirmation.
3. **Phase 3 — recovery:** VRAM before/after a 128×128 recovery create,
   successful render/sample/present, destroy delta, final low-load frame.
4. **Phase 4 — rollback cycles:** baseline and final VRAM printed per cycle,
   accumulating-loss detection, post-cycle sanity create.
5. **Phase 5 — mixed-size:** alternating 64×64 / 128×128 / 256×256 creates,
   reverse-order destroys, post-churn 256×256 sanity check.
6. **Phase 6 — final screenshot:** sampled recovery target, green status bar,
   white corner markers, no corruption.

## Screenshot

Path: `/pc/pvr_render_target_stress-hardware.ppm`

Build with `HARDWARE_SCREENSHOTS=1`; the Makefile then defines
`DC_PVR_RENDER_TARGET_PROTOTYPE_HARDWARE_SCREENSHOT`. The screenshot shows the
final recovery frame: a sampled 128×128 target, green status bar, and white
corner markers.

## Hardware test

Upload to a real Dreamcast at 640×480 VGA:

```sh
kos-tool -t 192.168.0.128 -x pvr_render_target_stress.elf -m .
```

Hold each frame at least 12 seconds. Capture the final recovery frame via
`vid_screen_shot("/pc/pvr_render_target_stress-hardware.ppm")` or by
photographing the physical display.

## Test structure

| Phase | Description | Max targets |
|-------|-------------|------------:|
| 1 | Baseline VRAM and renderer info | — |
| 2 | Bounded exhaustion (256×256) | 32 |
| 3 | Known-good 128×128 recovery | 1 |
| 4 | Repeated exhaust→free→recovery cycles | 32 × 10 |
| 5 | Mixed-size fragmentation probe | 24 |
| 6 | Final recovery screenshot | 1 |

## VRAM accounting equations

- **Per-target cost:** `backing_w * backing_h * 2 * 2` bytes (current + next).
- **256×256 target:** `256 * 256 * 2 * 2 = 262,144` bytes.
- **Baseline delta:** `pvr_mem_available()` after renderer creation minus
  `pvr_mem_available()` before renderer creation equals renderer + screen-pair
  overhead. This is the allocator baseline against which rollback is measured.
- **Leak detection:** `pre_baseline - post_baseline > 0` between phases
  indicates unrecovered VRAM.

## Clean exhaustion vs. leak

- **Clean exhaustion:** `SDL_CreateTexture` returns NULL with
  `SDL_OutOfMemory()`. The returned SDL error string is recorded. VRAM
  consumed equals the sum of successful dual allocations.
- **Leak detection:** After freeing all targets, `pvr_mem_available()` must
  return to the pre-exhaustion baseline. A stable allocator delta (documented
  in prior tests) is acceptable; an accumulating delta across rollback cycles
  is a failure.

## Failed partial dual allocation

The guarded prototype allocates `vram_next` first, then `vram`. If the second
`pvr_mem_malloc` fails, `vram_next` is freed before returning the error. The
test detects this by:

1. Recording `pvr_mem_available()` immediately before each create.
2. Recording it immediately after the failure.
3. Comparing against the sum of all previously successful allocations.
4. If `post_failure_vram == pre_exhaustion_vram`, the failed creation leaked
   nothing. Any difference indicates unreleased VRAM.

## Source concerns

- The renderer source is not modified. This test only exercises the existing
  guarded prototype.
- The 256×256 dual cost (262,144 bytes) is the current POT-backed allocation
  for the guarded prototype. If the prototype changes its allocation strategy,
  the per-target cost constant must be updated.
- The test assumes `pvr_mem_available()` is monotonic across the test
  (no background allocation by KOS).

## Git status

New files only under `test/dreamcast/test/pvr_render_target_stress/`.
No modifications to renderer source, existing tests, KOS, OpenGL/GLdc, or
PVR_PCLIP.

## Physical hardware result (2026-07-29)

Passed on a real Dreamcast at 640x480 VGA. The authoritative serial log is
`hardware-20260729-154438.log` and the retained raw framebuffer is
`pvr_render_target_stress-hardware-final.ppm` in this directory.

- Initial prototype texture-heap availability: 3,845,800 bytes.
- Fourteen 256x256 dual-allocation targets succeeded. Allocation 15 failed
  cleanly with 174,888 bytes remaining.
- The failed partial allocation returned to the exact pre-attempt value:
  174,888 bytes before and after.
- Freeing all 14 targets restored the exact 3,845,800-byte baseline.
- Ten bounded exhaust/free cycles each created 14 targets, failed cleanly on
  allocation 15, and returned to the exact fixed baseline.
- All 24 mixed-size allocations succeeded; reverse destruction restored the
  exact baseline, and a later 256x256 target rendered successfully.
- The final recovery target, four green target-corner markers, red center,
  green status bar, and four white screen-corner markers are complete in the
  raw capture. There is no missing geometry or visible corruption.
- The test reported four checks, zero failures, `PASS`, and returned zero to
  `dc-load-ip` without external termination.

An earlier run in `hardware-20260729-154230.log` wrote the same capture path
during phase 3 and phase 6; phase 6 produced the retained
`pvr_render_target_stress-hardware.ppm`. The source was corrected so only
phase 6 writes the requested screenshot and then rerun in full. Both final
framebuffers are byte-identical (SHA-256
`5835eede4c4398706c139a7976ae69fb772fce5cc582caf2eb6ceae457f49a23`).
