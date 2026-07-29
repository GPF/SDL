# Native PVR renderer: TA command-capacity stress test

Focused stress test for the Dreamcast native PVR renderer. It exercises the
8192 32-byte-unit TA command-buffer scene budget with Workloads A through E.
Workload F is a boundary probe that is excluded from ordinary builds. The test
does not measure or characterize an OPB limit; OPB pressure is tile-dependent
and has no reliable scene-wide primitive count.

Design and formulas: `src/render/dreamcast/PVR_CAPACITY_RESEARCH.md`.

The serial counts are analytical expectations derived from the renderer paths;
KOS exposes no ordinary TA/OPB usage counter. Stage completion and a recovery
frame are useful observations, but they are not runtime overflow detectors and
do not characterize the exact unsafe boundary.

## Safe ordinary workloads

All ordinary workloads stay below the 85% threshold of the 8192-unit budget.

| Workload | Formula | Ramp | Final units | Percentage |
|----------|---------|------|-------------|------------|
| A | `1 + 3N` | 256, 512, 1024, 2048, 2300 triangles | 6901 | 84.2% |
| B | `7N` | 256, 512, 768, 900, 990 quads | 6930 | 84.6% |
| C | `1 + 6N` | 256, 512, 1024, 1150 copies | 6901 | 84.2% |
| D | `1 + 6N` | 256, 512, 1024, 1150 points | 6901 | 84.2% |
| E | `3763 + 6S + 7O` | 5 stages, final: 400 sprites + 75 overlays | 6688 | 81.6% |

Workload E uses this exact ordered composition: one clear (1 header + 4
vertices), one textured background quad (1 + 6), `S` same-texture sprites
(6 each), `O` simultaneously live unique translucent textures (1 + 6 each),
25 UI rectangles (1 shared header + 150 vertices), then 600 points that reuse
the UI header (3600 vertices). Its stages are:

| Stage | S | O | Headers | Vertices | Total | Percentage |
|-------|---|---|---------|----------|-------|------------|
| 1 | 100 | 15 | 18 | 4450 | 4468 | 54.5% |
| 2 | 200 | 30 | 33 | 5140 | 5173 | 63.1% |
| 3 | 300 | 45 | 48 | 5830 | 5878 | 71.8% |
| 4 | 350 | 60 | 63 | 6220 | 6283 | 76.7% |
| 5 | 400 | 75 | 78 | 6610 | 6688 | 81.6% |

Workload B forces header recompilation on every draw by alternating
`SDL_BLENDMODE_NONE` and `SDL_BLENDMODE_BLEND` on the same texture. The
renderer's polygon-header cache is keyed on (texture pointer, blend mode,
texture_env, filter); toggling blend mode on every quad invalidates the cache
each time and produces exactly one header per quad.

## Build

From the repository root:

```sh
make -C test/dreamcast/test/pvr_capacity_stress -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild"
```

Strict warnings (`-Wall -Wextra -Werror`). The ordinary build excludes
Workload F and hardware screenshot support.

For every physical-hardware validation, build the ordinary A-E workload set
with framebuffer capture enabled:

```sh
make -C test/dreamcast/test/pvr_capacity_stress -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild" HARDWARE_SCREENSHOTS=1
```

This defines `DC_PVR_CAPACITY_HARDWARE_SCREENSHOTS`; it does not enable
Workload F.

For the boundary probe (operator opt-in only):

```sh
make -C test/dreamcast/test/pvr_capacity_stress -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild" \
  BOUNDARY_PROBE=1 BOUNDARY_MAX=2800
```

## Physical-Dreamcast run

Upload and execute on hardware via the Broadband Adapter:

```sh
kos-tool -t 192.168.0.128 -x \
  test/dreamcast/test/pvr_capacity_stress/pvr_capacity_stress.elf -m .
```

Review serial output for the PASS line and per-stage accounting. Every stage is
presented for 2,000 ms and followed by a known-good low-load recovery frame
held for 1,000 ms. The test holds the final recovery frame for 10,000 ms for
inspection.

The screenshot-enabled hardware run writes raw 640x480 framebuffer captures
beneath this directory through the `/pc` mount. It captures the final A-D
stress frame, one recovery frame for each workload, and all five visually
distinct E stages. Before each `vid_screen_shot`, the test waits for three
vblanks so the PVR presentation buffers advance to the displayed page.

## Expected serial output

```
Selected video driver: ... (mode SDL_DC_DIRECT_VIDEO)
Selected render driver: Dreamcast PVR
Selected pixel formats: ...
Renderer flags: ...
Window: 640x480
Active renderer: Dreamcast PVR

=== Workload 1: Stable untextured triangles ===
  Ramp 1/5: N=256 triangles
  triangle stage: ... percentage=XX.X%
  ...
  Ramp 5/5: N=2300 triangles
  triangle stage completed: expected_submitted=6901 expected_hdr=1 expected_vert=6900
                            expected_total=6901 percentage=84.2%
    Known-good recovery frame presented.
  Workload A complete.

=== Workload 2: Header-heavy textured quads ===
  Ramp 1/5: N=256 quads
  ...
  Ramp 5/5: N=990 quads
  quad stage completed: expected_submitted=6930 expected_hdr=990 expected_vert=5940
                        expected_total=6930 percentage=84.6%
    Known-good recovery frame presented.
  Workload B complete.

=== Workload 3: Same-texture copies ===
  Ramp 1/4: N=256 copies
  ...
  Ramp 4/4: N=1150 copies
  copy stage completed: expected_submitted=6901 expected_hdr=1 expected_vert=6900
                        expected_total=6901 percentage=84.2%
    Known-good recovery frame presented.
  Workload C complete.

=== Workload 4: Stable-state points ===
  Ramp 1/4: N=256 points
  ...
  Ramp 4/4: N=1150 points
  point stage completed: expected_submitted=6901 expected_hdr=1 expected_vert=6900
                         expected_total=6901 percentage=84.2%
    Known-good recovery frame presented.
  Workload D complete.

=== Workload 5: Mixed realistic frame ===
  Stage 1: sprites=100 overlays=15
  Stage 2: sprites=200 overlays=30
  Stage 3: sprites=300 overlays=45
  Stage 4: sprites=350 overlays=60
  Stage 5 (final): sprites=400 overlays=75
  Stage 5 (final) completed: expected_submitted=6688 expected_hdr=78
                             expected_vert=6610 expected_total=6688
                             percentage=81.6%
    Known-good recovery frame presented.
  Workload E complete.

Workload F is excluded (compile with DC_PVR_CAPACITY_BOUNDARY_PROBE).

PASS: all ordinary workloads (A-E) completed.
```

## Visual expectations

Each workload paints a distinct background color and layout so that on
physical hardware a reviewer can verify:

* Workload A: dark-blue field with many light-green triangles.
* Workload B: a textured field with a grid of small quads whose consecutive
  draws alternate `NONE` and `BLEND` state.
* Workload C: a textured field with a dense grid of same-texture copies.
* Workload D: a dense light-green scattered-point pattern.
* Workload E: dark-blue scene with a textured background panel, sprite
  columns, colored translucent overlay bands, yellow UI rectangles, and
  light-green points.

Every workload stage is followed by a small recovery frame (a dark field with
a bright rectangle) to confirm that a later synchronized low-load scene still
renders correctly.

## Validation order

1. Strict build and static checks (`-Wall -Wextra -Werror`, explicit local
   archive, linker map confirming the native PVR renderer).
2. Physical Dreamcast hardware via BBA.
3. Serial-output review.
4. Capture and directly inspect every visually distinct hardware workload.
5. Flycast as an optional diagnostic aid.

Hardware is first and authoritative.

## Physical-hardware validation (2026-07-29)

Passed on a real Dreamcast at 640x480 VGA through the Broadband Adapter. The
strict screenshot-enabled A-E build used the explicit local SDL archives,
contained no Workload F probe, and produced no warnings. The authoritative
serial log is `hardware-20260729-020212.log`. Every A-E stage printed the
expected headers, vertices, total units, and percentage; every stage reached
its recovery frame; Workload F remained excluded; the test printed PASS and
returned zero. A subsequent strict ordinary rebuild confirmed that capture
code, hardware paths, and `vid_screen_shot` are absent when
`HARDWARE_SCREENSHOTS` is not enabled.

The first framebuffer-capture attempt read the previous presentation buffer.
The capture helper was corrected to wait three vblanks before calling
`vid_screen_shot`, matching the established native-PVR screenshot workflow.
The rerun produced 14 distinct, synchronized 640x480 hardware captures.
Direct inspection confirms complete A triangles, complete B and C texture
grids, complete D points, progressive E sprite/overlay/UI/point composition,
visibly translucent E overlays, and stable recovery frames. No missing
geometry, corruption, freeze, reset, tearing, incomplete presentation, or
failed recovery was observed.

| Capture | SHA-256 |
|---------|---------|
| `pvr_capacity_stress-hardware-a-final.ppm` | `7c5a6a726cd54dfd193dbe434f54dd05b4ac66d71ecc77f369026f1e755f9f1f` |
| `pvr_capacity_stress-hardware-a-recovery.ppm` | `e44d153f3de335b9a71df20968a76c48d6f96d9825bedeefac73c7805b7e1c3e` |
| `pvr_capacity_stress-hardware-b-final.ppm` | `ba2e87bd7a9f310f2d7b8f723af07cc8fbe59f24804137f80d61f9eba2e1366f` |
| `pvr_capacity_stress-hardware-b-recovery.ppm` | `4d04a7bd33b9d8ac7bee4fdcc3af558aae9dc905e7d770a3d317bfe7b05f68e5` |
| `pvr_capacity_stress-hardware-c-final.ppm` | `2d47c4565e21581db7ccf398c02ff37ee07066df524e16601e123dc37d23d3ad` |
| `pvr_capacity_stress-hardware-c-recovery.ppm` | `c8c31a0ac29d051f6acf7b9572b48609912913064154db5aa7fd441003f5f27f` |
| `pvr_capacity_stress-hardware-d-final.ppm` | `f9b88bfae0e128241ff14091daaf6870e35dc402d195b84709a6800e39e93947` |
| `pvr_capacity_stress-hardware-d-recovery.ppm` | `164a8cdff62679a865670c7077850cb3b37bd7c4fce6fd1c9097da025502d518` |
| `pvr_capacity_stress-hardware-e-stage1.ppm` | `fdceb48f80b823d55a5248d811bc52508a113573c069f3e6af11e80a3e2a66e1` |
| `pvr_capacity_stress-hardware-e-stage2.ppm` | `cc191b58f29316546ae45358a5a6c290d1df77466949a7aaa821edcbcf139190` |
| `pvr_capacity_stress-hardware-e-stage3.ppm` | `326637a91b9186e9b0884a08ac784aded6c96da2e906e17a8b3e4b6e01139b64` |
| `pvr_capacity_stress-hardware-e-stage4.ppm` | `ff29add3b0cd3d4989d5e92dc2197c33089557bdaee00ea8bef2001e924f559d` |
| `pvr_capacity_stress-hardware-e-stage5.ppm` | `946eb74ce759f0a8c1f306cbb424916a724264c9fb732206a8f3c393098ed27b` |
| `pvr_capacity_stress-hardware-e-recovery.ppm` | `faeca67f60bb55f444064262e3283a5f870717456eb80b87398bc1127b636a37` |

## Workload F: boundary probe

Workload F is compile-time opt-in only. It is **not** part of the ordinary
test.

```sh
make -C test/dreamcast/test/pvr_capacity_stress -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild" \
  BOUNDARY_PROBE=1 BOUNDARY_MAX=2800
```

Ramp: 2680, 2700, 2720, 2730, 2740, 2750, 2775, 2800 triangles.
Formula: `1 + 3N`. The operator must stop/reset on the first observable anomaly;
the program has no reliable automatic overflow detector.
`BOUNDARY_MAX` is passed as `DC_PVR_CAPACITY_BOUNDARY_MAX`; the source rejects
values above 2800.

**Warnings before any run:**

* Console corruption or failure to complete the scene is possible.
* Recovery, if any, may require `pvr_shutdown()` plus `pvr_init()`, but
  success is not guaranteed.
* Do not rely on `pvr_wait_ready()` or `pvr_wait_render_done()` return values
  as overflow indicators. Timeouts are ambiguous and not reliable overflow
  detectors.
* If the console stops responding or the output is clearly wrong, a console
  reset may be required. No recovery is promised.

**This workload was not run during implementation.** Do not assume it
recovers normally.
