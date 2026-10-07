# Testing the SDL3 Dreamcast PVR renderer

This document describes reproducible procedures for the native Dreamcast PVR
renderer. Durable hardware results live in [`results/`](results/); current state
lives in the project status notes.

**Hardware is the correctness authority.** A successful host compile is not a
pass, and a flycast pass is not a hardware pass. Emulators hide alignment,
cache, DMA, PVR/TA flag, and timing bugs that real hardware shows.

## Hardware quick reference

The dc-load-ip console is `192.168.0.128`:

```bash
ping -c 2 -W 2 192.168.0.128
ps aux | grep -i 'kos-tool\|sh-elf-gdb' | grep -v grep
```

Before a launch, ensure no old loader or GDB process is using the session. All
agents and scripts must use `tools/dc-load/kos-tool-locked.sh` with a task
label; this serializes launches using the shared console lock. The wrapper
records its owner and refuses concurrent launches. It removes the lock after the
loader exits. If a session times out, do not clear the lock or kill `kos-tool`:
exit the console program with Start+A+B+X+Y, wait for the loader to return, then
clear a genuinely stale lock only with `--clear-stale` (which refuses while any
`kos-tool` process exists).

mGBADC uses its own lock path. If both projects may touch the same console,
point this wrapper at the same lock so the two repositories serialize:

```bash
DC_CONSOLE_LOCK=/tmp/mgbadc-dreamcast-console.lock SDL_DC_LOCK_TASK=pvr-gate \
  tools/dc-load/kos-tool-locked.sh -t 192.168.0.128 -x test/dreamcast/pvr_phase0_lifecycle/pvr_phase0_lifecycle.elf
```

**Restricted agent-shell limitation (observed in mGBADC, applies here too):** the
default agent command shell may deny `AF_INET` socket creation with `EPERM`, so
`kos-tool` reports `Failed to create UDP sockets` before it contacts the console.
That is an execution-environment restriction, not a Dreamcast, route, or
`kos-tool` regression. Run the same locked command in a network-enabled shell.

## Build

```bash
cd build-scripts
./dreamcast.sh              # configure + build
./dreamcast.sh install      # install library and headers into the KOS addon tree
```

Then build a test target:

```bash
cd test/dreamcast/pvr_phase0_lifecycle
make -f Makefile.dc
```

For physical framebuffer evidence on the visual gates, rebuild with optional
capture enabled:

```bash
make -B -f Makefile.dc SCREENSHOT=1
```

After the last presented frame and a vblank wait, the fixture saves a raw PPM
under `/pc/`. Launch it through `tools/dc-load/kos-tool-locked.sh` with `-m .`
from the repository root to return the PPM to the host. The lifecycle gate
captures its clear frame; the textured-quad gate captures its final nearest and
linear frames separately. Screenshot capture is not part of the default build.

The KOS environment must be sourced (`environ.sh`) so `kos-cc` and `KOS_BASE`
resolve. `PLATFORM_DREAMCAST` comes from the KOS CMake platform module, so
`SDL_VIDEO_RENDER_DREAMCAST_PVR` defaults ON for Dreamcast builds.
After changing SDL library sources, run `./dreamcast.sh install` before rebuilding
fixtures: each fixture links the SDL3 archive installed in the KOS addon tree.

## Test ladder

Use the same ladder as the rest of the Dreamcast work:

1. host build + `git diff --check`;
2. one hardware gate (`pvr_phase0_lifecycle`);
3. small smoke slice (clear/solid geometry, textured quad, coordinates);
4. boundary and rejection fixtures (`pvr_phase0_boundary`);
5. pressure fixtures (list capacity, vertex buffer overflow) — not in Phase 0.

Report each level separately. A test that passes only through the software
renderer fallback is not PVR coverage: every gate asserts the renderer name and
aborts if it is not `Dreamcast PVR`, so a PASS from a fixture is only valid as
PVR coverage when its `renderer=` line says `Dreamcast PVR`.

Fixtures exit automatically after presenting their final frame. Gates 8 and 9
hold the captured frame for three seconds. For a captured frame, build with
`SCREENSHOT=1` and launch with `-m .`; the screenshot is saved under `/pc/`
before the fixture exits. Without capture, the result is a log/API gate and does
not pause for manual inspection.

## Phase 1 hardening fixture

Build `pvr_phase1_hardening` against a normal PVR library. Allocation-failure
injection requires a separate library configured with
`SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS=ON` (or `./dreamcast.sh --enable-pvr-tests`).
Build with `SCREENSHOT=1` and
`make -f Makefile.dc p1a p1b` for separate gate ELFs and captures. Launch
`pvr_phase1_hardening_p1a.elf` and then
`pvr_phase1_hardening_p1b.elf` through separate locked console runs. P1-A
exercises 64 finite
`A → B → screen → B → A → screen` cycles with persistent screen/A/B markers,
distinct A/B viewport and clip settings left installed across switches, state
assertions after each reselect, and repeated post-switch target sampling.
P1-B injects failure at logical-screen allocation 1 and 2 and target allocation
1 and 2; every failure is followed by a normal allocation, draw, and screen
present.

The fixture uses the same three rendered-frame capture warmup as the earlier
Phase 1 gate because first-frame black captures are already documented for
this PVR presentation path. The stress result itself is captured after its
64-cycle pass.

Expected P1-A markers include `switch_stress=PASS cycles=64`, `result=PASS`,
and `Program returned 0`. P1-B prints the screen and target allocation steps,
`allocation_fail_recovery=PASS`, `screen_after_failure=PASS`, and the same
clean exit. Captures: `/pc/pvr_phase1_hardening_stress.ppm` and
`/pc/pvr_phase1_hardening_recovery.ppm`.

P1-C's canonical replacement scene is defined in
`pvr_phase1_hardening.c`. It uses three 64x64 RGB565 targets: retained red,
freed/reallocated magenta, and fresh yellow. Each target is cleared black and
gets an 18x18 marker at (4,4). The logical-screen scene clears black, samples
those textures in that order into 96x96 rectangles at (32,48), (168,48), and
(304,48), then draws an untextured cyan 96x96 rectangle at (440,48). The target
viewports are default and clipping is disabled. The only render presents are
the three fixed white-square warmups and one final scene present. The canonical
repeated fixture captures the physical framebuffer after each of the first
three vblanks following that present. These are capture-only checkpoints and
do not submit additional render presents. Control and pressure use identical
scene setup and draw code.

Build the no-exhaustion control and three uniquely labeled pressure ELFs with
`make ... p1c_repeat_control p1c_repeat_pressure_1 p1c_repeat_pressure_2 p1c_repeat_pressure_3`. Each pressure run uses
uniform RGB565 targets to reach actual allocator failure (320-success cap),
keeps the same three marked targets, frees the rest, reallocates the freed
count, marks the first reallocated target magenta, frees the remaining
reallocated targets, and marks one fresh target yellow. The API sequence then
draws the exact same final scene as the control. KOS's allocator warning at
deliberate exhaustion is expected. Each ELF uses distinct capture and log
names under `results/phase1-captures/`.

The earlier single-capture pressure runs returned the preceding white-square
warmup page. Pixel comparison showed those captures exactly match the
pre-final framebuffer and lack all four final-scene markers. The canonical
multi-vblank fixture captured the final scene at vblanks 2 and 3 in all four
pressure runs (one diagnostic run plus three repeats); the three repeated
pressure captures also match their control pixel-for-pixel at vblanks 2 and 3.
One of the three repeated runs still showed the warmup page at vblank 1, while
the other two showed the final scene immediately. This reproduces the capture
timing issue and demonstrates that target contents render correctly after real
exhaustion/reallocation. P1-C passes with this fixed capture procedure; retain
the earlier stale-page result as a presentation/capture timing observation.
At the time of this prototype test record, target support remained guarded;
the later promotion and post-promotion smoke are recorded below.

KOS completion semantics were checked in its PVR implementation: normal
`pvr_list_finish()` and `pvr_scene_finish()` return success after submission;
the list error is an invalid/unopened-list debug check. `pvr_wait_render_done()`
can time out. The backend marks that path failed before the current/next swap,
keeps the old allocation authoritative, and rejects subsequent rendering. That
wait timeout is the meaningful observable completion failure; forcing hardware
to wedge is not a useful deterministic fixture. Therefore Gate P1-D is a
source-level invariant review, not a fabricated hardware failure injection.

## Gate 1 — opt-in driver selection and PVR lifecycle

```bash
SDL_DC_LOCK_TASK=pvr-gate tools/dc-load/kos-tool-locked.sh -t 192.168.0.128 \
  -x test/dreamcast/pvr_phase0_lifecycle/pvr_phase0_lifecycle.elf
```

Expected log:

```
pvr_gate renderer=Dreamcast PVR
pvr_gate vsync=PASS default=0 toggle=0,1,0
pvr_gate vsync_interval_rejected=PASS value=2
pvr_gate output=640x480
pvr_gate format[0]=SDL_PIXELFORMAT_RGB565
pvr_gate format[1]=SDL_PIXELFORMAT_ARGB1555
pvr_gate format[2]=SDL_PIXELFORMAT_ARGB4444
pvr_gate clear_present=PASS
pvr_gate destroy_returned=PASS
pvr_gate lifecycle=PASS
Program returned 0
```

Pass criteria:

- `renderer=Dreamcast PVR` with no driver name passed to `SDL_CreateRenderer`,
  after the test selects `SDL_DC_DREAMCAST_PVR_VIDEO`. The PVR renderer is
  opt-in: the default Dreamcast video mode is still OpenGL, so a fixture that
  does not select the PVR mode is not exercising the PVR path.
- `Program returned 0` in the same session. `destroy_returned=PASS` only records
  that destruction returned; the lifecycle criterion is the clean exit code plus
  the absence of a hang. A hang means scene/list lifetime is unbalanced or
  `pvr_init()` was already owned by the video layer.
- A clean crash prints `Program returned 1`. A hang has no software interrupt
  path; use the controller chord briefly, then power-cycle.
- The VSync markers verify SDL3's API state: intervals 0 and 1 are accepted and
  interval 2 is rejected. Interval 0 disables SDL's simulated pacing; the video
  layer still flips the display page at vblank.

## Gate 2 — float color policy and solid geometry

```bash
SDL_DC_LOCK_TASK=pvr-smoke1 tools/dc-load/kos-tool-locked.sh -t 192.168.0.128 \
  -x test/dreamcast/pvr_phase0_clear_triangle/pvr_phase0_clear_triangle.elf
```

Golden packed colors the backend must produce (`color_scale = 1.0`, nearest
integer, clamped, never wrapped):

| input | expected bytes |
| --- | --- |
| 0.25, 0.50, 0.75 | 64, 128, 191 |
| 0.75, 0.50, 0.25 | 191, 128, 64 |
| 0.50, 0.25, 0.75 | 128, 64, 191 |
| 0.10, 0.20, 0.30 (clear) | 26, 51, 77 |

The display quantizes to RGB565, so on-screen values are the 565 approximation
of those bytes, not the bytes themselves. Compare the *pattern*, not exact
channel values.

Geometry: apex at pixel `(320,120)`, base corners `(160,360)` and `(480,360)`.

Failure signatures:

- banding or flat facets → shading/interpolation flag wrong;
- triangle offset by a whole viewport → coordinate contract wrong;
- colors one LSB low → nearest-integer policy became truncation.

## Gate 3 — textured quad

```bash
SDL_DC_LOCK_TASK=pvr-smoke2 tools/dc-load/kos-tool-locked.sh -t 192.168.0.128 \
  -x test/dreamcast/pvr_phase0_textured_quad/pvr_phase0_textured_quad.elf
```

Expected: 128x128 checkerboard quad at pixel `(256,176)`, red/blue alternating,
nearest on even frames and bilinear on odd frames.

Expected output is modelled in `pvr_phase0_textured_quad/expected_model.py`
(`python3 expected_model.py capture.ppm ...`, reports MATCH per mode). Nearest is
exact: every texel is a 2x2 block, 16 px cells. Bilinear uses texel-center sampling
with **repeat** addressing, so the quad's edge pixels blend with the opposite texture
edge (the corner pixel reads 19/0/12, not pure red). Hardware bilinear tracks the ideal
float filter within about 3 of 31 levels; its blend weights run a little high in the
red channel (cause not established), so compare with a tolerance, not equality. The
two 2026-10-06 rebaseline runs are bit-identical to each other; the older stored
bilinear frame differs from them by one level on 1920 blend pixels (unexplained;
possibly a library-version or dither difference).

Failure signatures:

- black quad → texture never uploaded, or the VRAM pointer is wrong;
- quad drawn in the flat draw color → texture env flag wrong;
- checkerboard sheared → twiddling/format mismatch;
- the printed `texture_format=` line is the core's conversion, not what the test
  asked for. Check it before blaming the backend.

## Gate 4 — coordinate transform

```bash
SDL_DC_LOCK_TASK=pvr-smoke3 tools/dc-load/kos-tool-locked.sh -t 192.168.0.128 \
  -x test/dreamcast/pvr_phase0_coordinates/pvr_phase0_coordinates.elf
```

Logical presentation 320x240 on a 640x480 output, so `current_scale` is 2.0.

| block | logical input | expected pixel |
| --- | --- | --- |
| red | (0, 0) | (0, 0) |
| green | (310, 230) | (620, 460) |
| blue | (160, 120) | (320, 240) |
| yellow | viewport origin (100, 100) | (200, 200) |
| cyan | viewport-relative (50, 50), inside clip | (300, 300) |
| magenta | viewport-relative (95, 95), outside clip | not drawn |

Failure signatures:

- blocks at half distance → `current_scale` applied twice, or not applied;
- yellow at `(100,100)` instead of `(200,200)` → pixel viewport origin not added
  at emission;
- magenta visible → clip rect treated as framebuffer-relative instead of
  viewport-relative.

## Gate 5 — rejection fixtures

```bash
SDL_DC_LOCK_TASK=pvr-boundary tools/dc-load/kos-tool-locked.sh -t 192.168.0.128 \
  -x test/dreamcast/pvr_phase0_boundary/pvr_phase0_boundary.elf
```

Expected:

```
case target_texture=PASS
case lock_static_texture=PASS
case blend_mod_rejected=PASS
case update_while_scene_open=PASS
case update_after_present_allowed=PASS
case non_triangle_geometry=PASS
case non_finite_geometry=PASS
pvr_boundary result=PASS cases=7
```

Case 3 verifies the PVR-specific SDL core gate rejects `MOD` from
`SDL_SetRenderDrawBlendMode`. SDL3 keeps required built-in blend support for
other renderer identities. The backend also retains a queue-time guard in case
an unsupported mode reaches command emission through another path.

Case 7 is the `-ffast-math` proof: the release build folds `isfinite()` to true,
so the backend checks IEEE-754 exponent bits. A FAIL here means the guard was
optimized away.

## Gate 6 — texture copy, rotation, and flip

```bash
make -B -f Makefile.dc SCREENSHOT=1 -C test/dreamcast/pvr_phase0_copy_transform
SDL_DC_LOCK_TASK=pvr-copy-transform tools/dc-load/kos-tool-locked.sh -m . \
  -t 192.168.0.128 -x test/dreamcast/pvr_phase0_copy_transform/pvr_phase0_copy_transform.elf
```

The fixture draws a four-color texture as a plain copy, a 90-degree clockwise
rotation, a horizontal flip, and a vertical flip. Expected corner colors
(`TL/TR` on the first line, `BL/BR` on the second):

```
case      TL      TR      BL      BR
copy      red     green   blue    yellow
rotate90  blue    red     yellow  green
flip_h    green   red     yellow  blue
flip_v    blue    yellow  red     green
```

The fixture presents three frames before capture to avoid capturing the stale
page on initial presentation. Confirm the renderer line and inspect
`results/phase0-captures/pvr_phase0_copy_transform.ppm`; command success alone
does not verify UV orientation.

## Gate 7 — scale-1 points and line-method rejection

```bash
make -B -f Makefile.dc SCREENSHOT=1 -C test/dreamcast/pvr_phase0_point_line
SDL_DC_LOCK_TASK=pvr-point-line tools/dc-load/kos-tool-locked.sh -m . \
  -t 192.168.0.128 -x test/dreamcast/pvr_phase0_point_line/pvr_phase0_point_line.elf
```

The fixture sets `SDL_HINT_RENDER_LINE_METHOD=2` before renderer creation,
verifies render scale is exactly `1.0,1.0` with logical presentation disabled,
then draws colored points and submits a line. At scale 1.0 SDL3 sends the points
to `QueueDrawPoints`; a scaled-presentation fallback would instead convert them
to fill rectangles. Method 2 sends the line to `QueueDrawLines`, which must
reject it at queue time.

Expected log markers:

```
pvr_point_line renderer=Dreamcast PVR
pvr_point_line line_method=2 scale=1.0,1.0 logical_presentation=disabled
pvr_point_line points_scale1=PASS
pvr_point_line line_method2_rejected=PASS (...)
pvr_point_line result=PASS (visual point check and line rejection)
Program returned 0
```

Inspect `results/phase0-captures/pvr_phase0_point_line.ppm` for the red, green,
and blue dotted blocks. Gate 9 separately captures the default line-method path.

## Gate 8 — deferred texture destruction

```bash
make -B -f Makefile.dc SCREENSHOT=1 -C test/dreamcast/pvr_phase0_deferred_texture
SDL_DC_LOCK_TASK=pvr-deferred-texture tools/dc-load/kos-tool-locked.sh -m . \
  -t 192.168.0.128 -x test/dreamcast/pvr_phase0_deferred_texture/pvr_phase0_deferred_texture.elf
```

The fixture uploads two checkers and presents three textured warmup frames. It
then queues both draws, destroys the red/blue source texture before
`SDL_RenderPresent`, and captures immediately after presentation. SDL3 core
flushes the queued draw as part of `SDL_DestroyTexture`; the backend must keep
the source VRAM alive until PVR render completion. The retained green/yellow
texture is a visual control.

Expected markers include:

```
pvr_deferred_texture renderer=Dreamcast PVR
pvr_deferred_texture warmup=PASS frames=3
pvr_deferred_texture destroyed_before_present=PASS
pvr_deferred_texture present_after_destroy=PASS
pvr_deferred_texture result=PASS (inspect retained textured draw)
Program returned 0
```

Inspect `results/phase0-captures/pvr_phase0_deferred_texture.ppm` for both
checkerboards. API markers alone do not prove the submitted texture remained
visible. The fixture holds the frame for three seconds after capture.

## Gate 9 — default line method

```bash
make -B -f Makefile.dc SCREENSHOT=1 -C test/dreamcast/pvr_phase0_default_lines
SDL_DC_LOCK_TASK=pvr-default-lines tools/dc-load/kos-tool-locked.sh -m . \
  -t 192.168.0.128 -x test/dreamcast/pvr_phase0_default_lines/pvr_phase0_default_lines.elf
```

The fixture requires `SDL_HINT_RENDER_LINE_METHOD` to be unset, selects no
explicit renderer name, and asserts the result is `Dreamcast PVR`. It verifies
scale `1.0,1.0` and disabled logical presentation, then draws forward and
reversed horizontal/vertical lines, shallow and steep diagonals (including a
reversed diagonal), an isolated zero-length line, and a connected translucent
polyline. At this scale SDL3's default POINTS method expands horizontal and
vertical segments to one-pixel rectangles and diagonals to Bresenham points;
the connected joints should receive single coverage.

Expected markers include:

```
pvr_default_lines line_method_hint=unset (SDL3 default POINTS)
pvr_default_lines renderer=Dreamcast PVR
pvr_default_lines view=PASS scale=1.0,1.0 logical_presentation=disabled
pvr_default_lines cases=PASS horizontal vertical shallow steep reversed zero_length connected
pvr_default_lines result=PASS (inspect rasterization)
Program returned 0
```

Inspect `results/phase0-captures/pvr_phase0_default_lines.ppm` for colored
single-pixel lines, the isolated white zero-length pixel, and the connected
translucent zigzag without brighter shared joints. The fixture holds the frame
for three seconds after capture.

## Preserving a failure

When a gate fails, keep: the exact log, the test binary, the frame number, the
expected and observed values, and the fixture that produced the first
divergence. Do not re-run until the first divergence is understood — a second
run on hardware can change what you see.

## Promoted Phase 1 — RGB565 render target preservation

RGB565 render targets are part of every normal Dreamcast PVR build. No target
feature flag is needed. Configure and build normally, then build the focused
fixture against that library. Test-only allocation and target-switch failure
injection is disabled by default; use `./dreamcast.sh --enable-pvr-tests` only
for dedicated recovery tests.

The `test/dreamcast/pvr_phase1_targets` fixture asserts the `Dreamcast PVR`
renderer and checks screen → target → screen preservation, A → B → A switching,
completed-target sampling, 127×93 NPOT edges, SDL3 selected-target present
rejection and continued use, plus failed-switch rollback. The promotion smoke
used this fixture against the normal, injection-free library and passed on
physical hardware. The hardening P1-A gate also passed with 64 switching cycles
and viewport/clip restoration. P1-B passed paired allocation-failure recovery
in the separate test build. All physical captures are visual evidence rather
than renderer readback. See
`results/phase1-captures/pvr_phase1_promotion_smoke.log` and
`results/phase1-targets-prototype.md` for the results and captures.

Only RGB565 target textures are supported. ARGB1555, ARGB4444, PAL8, other
converted formats, `.dt` target integration, DMA/partial/strided target work,
`RenderReadPixels`, address-mode emulation, unsupported blend modes,
list-routing optimization, and FSAA remain deferred. The renderer does not
advertise those target formats.

## Native PVR sampled `.dt` textures

`SDL_LoadDreamcastTexture_IO` routes `.dt` files to the native PVR renderer when
the selected renderer is `Dreamcast PVR`; the existing GLdc upload path remains
in place for OpenGL. PVR builds also provide the public loader when OpenGL is
disabled. The PVR loader creates immutable static sampled textures
and validates the version-zero `DcTx` header, power-of-two dimensions, format,
flags, and exact 32-byte-aligned payload length before allocating VRAM.

Accepted PVR payload layouts are direct ARGB1555, RGB565, and ARGB4444; VQ
payloads (including reduced codebooks); and square twiddled mip chains. VQ or
mipmapped payloads must be twiddled. Strided and partial layouts, PAL8,
bump-map, YUV, and target textures are unsupported. SDL-created target formats
remain restricted to RGB565, independent of `.dt` sampled-texture formats.

Build the focused fixture against the writable local SDL3 install after
`./dreamcast.sh --prefix "$PWD/local-install"` and `./dreamcast.sh install`:

```sh
cd test/dreamcast/pvr_dt_texture
make -f Makefile.dc SCREENSHOT=1
```

The fixture asserts the native renderer, loads and samples the 256×256
full-codebook RGB565 VQ fixture from the existing ROM disk, rejects a malformed
chunk, and captures `/pc/pvr_dt_texture.ppm`. Launch only through the locked
console wrapper. The 2026-10-06 physical VGA gate passed; the log and capture
are retained in `results/pvr-dt-texture.md` and `results/pvr_dt_texture.ppm`.
That gate covers the full-codebook RGB565 VQ case only. Direct formats, reduced
codebooks, and mipmapped payloads are accepted by the parser but still need
separate SDL3 hardware coverage. The GLdc route was not changed semantically.

### Direct 16-bit `.dt` coverage

`test/dreamcast/pvr_dt_direct/` covers direct ARGB1555, RGB565, and ARGB4444
sampled payloads. Assets are regenerated with the active KOS `pvrtex`; the
`.dt` parser is unchanged.

Status on physical hardware (2026-10-06, VGA): **the gate passes.** All six
panels (direct and sampled-target) match 6400/6400, 0 of 38400 pixels outside
tolerance, `compare_capture.py` exit 0, against the reinstalled `local-install`
(`results/phase1-captures/pvr_dt_direct_gate_reinstalled.log`). The loader
rejects the malformed chunk. The earlier black target row and one-pixel skew had
two causes: a wrong-page capture (`vid_screen_shot()` reads `vram_l`, not the PVR
front page) and a stale library. Any earlier "pass" for this fixture was against
the stale artifact and does not count.

RGB565 direct textures must use `SDL_BLENDMODE_NONE`; ARGB1555 and ARGB4444 must
use `SDL_BLENDMODE_BLEND`. Draws must be confirmed by inspecting captured pixels,
not by draw-success logs.

Findings and the full investigation are in
[`results/pvr-dt-direct-16bit.md`](results/pvr-dt-direct-16bit.md).

Two build rules matter for this fixture: `CPPFLAGS` must reach the compiler, and
an empty `SDL3_CONFIG_INCLUDEDIR` swallows `-DPVR_PHASE0_HARDWARE_SCREENSHOT` as
an `-I` argument. Delete the previous capture before a run so a stale file cannot
be read as a fresh one.

## Native PVR `NONE` and `BLEND` audit

The native PVR renderer accepts `SDL_BLENDMODE_NONE` and
`SDL_BLENDMODE_BLEND` for both draw commands and texture copies. The focused
`pvr_blend_modes` fixture also checks that an unsupported `ADD` request fails
without changing the previous mode. Its left capture panel draws directly to
the screen; the right panel renders the same scene into an RGB565 target and
samples that target back to the screen. Each panel has rows for untextured
`NONE`, untextured `BLEND`, textured `NONE`, and textured `BLEND`; each row
shows alpha modulation at 0, 128, and 255 over a blue destination. The white
RGB565 source texture is color-modulated to (255, 128, 64).

Build with `make -f Makefile.dc SCREENSHOT=1` in
`test/dreamcast/pvr_blend_modes`, then launch the ELF through the shared locked
console wrapper. The 2026-10-06 hardware capture confirmed `NONE` writes source
RGB at all alpha values, while `BLEND` preserves blue at alpha 0, mixes source
and blue at 128, and writes source RGB at 255. The screen and sampled-target
panels matched for every tested draw and texture-copy row after RGB565
quantizing. The strict fixture compile passed `-Wall -Wextra -Werror`; see
`results/pvr-blend-modes.md` for the measured pixels and retained capture.

## What Phase 0 does not cover

These are not tested because they are not implemented, and a pass on an
emulator for any of them is not coverage of this backend:

- target formats other than RGB565 and the deferred target work listed above;
- PAL8/palette textures;
- strided textures and partial upload;
- DMA upload;
- `RenderReadPixels`;
- HDR rendering and color spaces;
- texture address modes (the PVR always wraps, so `CLAMP` is not honored);
- `ADD`/`MOD`/`MUL` blend modes;
- opaque and modifier list routing;
- FSAA.


## Capture helper and target-switch trace

- `pvr_phase0_capture()` (`pvr_phase0_screenshot.h`) reads the PVR front page via
  `pvr_get_front_buffer()` (RGB565 only). `vid_screen_shot()` reads `vram_l`,
  which is not the displayed page when the PVR flips pages. `vid_waitvbl()` only
  waits for the display (framebuffer) vblank, not PVR rendering.
- Pixel verification belongs to the host: run `compare_capture.py <capture.ppm>`
  (exit status nonzero on mismatch). Fixture `TARGET SUBMIT OK` lines mean only
  that the SDL calls succeeded.
- Optional trace: build the library with `-DDC_PVR_TRACE_TARGETS` into a
  **separate** build dir and prefix (e.g. `build-scripts/dcbuild-trace`,
  `local-install-trace`), never over `local-install`. It logs `PVRTRACE switch`
  lines (target vram/vram_next/sample_vram/current_valid and screen buffers) at
  each `SDL_SetRenderTarget`. No behavior change when the macro is off.

## Re-baseline after the front-buffer capture fix (2026-10-06)

Re-run against the reinstalled `local-install` with the front-buffer helper:

- `pvr_phase1_targets` (promotion fixture): `result=PASS`; pages 1-4 are
  pixel-identical to the stored captures (0 differing pixels), so its earlier
  evidence stands (`pvr_phase1_targets_rerun*`).
- Hardening P1-A, P1-C repeat control and repeat pressure 1: all PASS.
  New control and pressure captures are pixel-identical at all three captures.
  Versus the stored captures there is a constant 1-pixel horizontal offset of the
  untextured cyan rectangle (new x=440..535, stored 441..536; 249 px), and
  stored pressure vblank-1 was the stale warmup page. The new geometry is the
  specified one. Attribution: the same hardening control built against the stale
  archive (`build-scripts/local-install.stale-20261006`) with the new
  front-buffer helper shows x=441..536 at all three captures
  (`stalelib_*`), so the stale library causes the offset, not the capture path.
  Unexplained: the promotion fixture was pixel-identical across the two
  libraries, so the offset is not a general library difference; it appears only
  for the hardening scene's geometry. Stored captures are kept; new ones are `rebaseline_*` under `results/phase1-captures/`

Update from the second pass: `pvr_blend_modes` shows the same one-pixel offset on
its sampled target panels (x=344..553, y=16..212), so the offset was not limited
to the hardening scene's geometry. That makes the promotion fixture being
pixel-identical across the two libraries harder to explain, and the attribution
still rests on pattern matching rather than on a reproduced stale-library build.

### Second-pass re-baseline (2026-10-06)

Every fixture that calls `pvr_phase0_capture()` was rebuilt with `SCREENSHOT=1`
and explicit `SDL3_PREFIX` / `SDL3_CONFIG_INCLUDEDIR` (the Makefile defaults point
at the stale KOS addon archive), each ELF was checked for the `front buffer
capture` string, and each launch went through `tools/dc-load/kos-tool-locked.sh`
with a `SDL_DC_LOCK_TASK` label. Comparison used `tools/ppm_diff.py` at tolerance
8 per channel. All re-run fixtures ran in 640x480 VGA RGB565; none requested a
display mode the helper cannot read. Hardware run output was not saved to files;
only build logs are under `logs/`.

15 fixtures re-run: 14 exited 0, 1 failed. 7 captures are pixel-identical to
stored, 7 differ, 1 produced no capture. "Identical" means nothing changed, not
that the scene is correct; the expected-content column is the only place a side
is judged. Expected content was derived from each fixture's own drawing code, as
`pvr_dt_direct/compare_capture.py` does for the .dt gate.

| Fixture | Exit | vs stored (tol 8) | vs expected content |
|---|---|---|---|
| `pvr_phase0_lifecycle` | PASS | 0 | not modelled; unchanged only |
| `pvr_phase0_clear_triangle` | PASS | 0 | not modelled; unchanged only |
| `pvr_phase0_coordinates` | PASS | 0 | not modelled; unchanged only |
| `pvr_phase0_copy_transform` | PASS | 0 | not modelled; unchanged only |
| `pvr_phase0_point_line` | PASS | 0 | not modelled; unchanged only |
| `pvr_phase0_default_lines` | PASS | 0 | not modelled; unchanged only |
| `pvr_phase0_clear_triangle` | PASS | 0 | identical |
| `pvr_phase0_coordinates` | PASS | 0 | identical |
| `pvr_phase0_copy_transform` | PASS | 0 | identical |
| `pvr_phase0_point_line` | PASS | 0 | identical |
| `pvr_phase0_default_lines` | PASS | 0 | identical |
| `pvr_phase0_deferred_texture` | PASS | 16 | **Checked.** Expected ARGB1555 checker at (128,176,128,128): stored matches expected exactly (0 mismatches), new has 16 at x=128..135 y=176..177 (white + dark blue instead of red). So the *new* capture is the one that deviates. Not threshold-sensitive (16 px at tol 2, 8 and 16); a third run reproduced the same 16 pixels (`rebaseline_pvr_phase0_deferred_texture_repeat.ppm`). The deviating values are intermediate between the white clear and the blue texel (0xFFFF, 0x1813, 0x1815), so they are written content, not unwritten-page pixels. This is gate 8, whose whole purpose is corruption after destroying a texture before present, so this needs a decision, not a repeat. |
| `pvr_phase0_textured_quad` | PASS | 3840 each | quad only, x=256..383 y=176..303. **Resolved against an expected-output model** (`pvr_phase0_textured_quad/expected_model.py`, derived from the fixture source): the new pair is correct; the stored pair is the other frame parity, i.e. swapped relative to its filenames (stored `linear` = exact nearest frame, stored `nearest` = a bilinear frame). New `nearest` matches the nearest model exactly (16384/16384 px, nothing outside the quad changed); new `linear` matches the bilinear model within 2.75 of 31 levels. The stored pair is retired as evidence. |
| `pvr_phase0_boundary` | FAIL | no capture | case 1 `target_texture` now succeeds because targets are promoted, so the fixture aborts before capturing. Nothing to compare; the fixture's expectation predates promotion and was not changed. |
| `pvr_blend_modes` | PASS | 1845 | target panels only, x=344..553 y=16..212. The whole panel content is one pixel left of stored (stored = new shifted right by 1). Consistent with the closed stale-library 1-px offset, but that remains an inference: the old library is gone and no expected-content model was built for this scene. See the Closed section, which now absorbs this. |
| `pvr_dt_texture` | PASS | 16197 | Ground truth established (see `results/pvr-dt-texture.md`): `Troy2024.dt` is self-consistent (32 + 2048 codebook + 16384 8-bit indices); decoded with y-even morton block indices and `(x&1)<<1\|(y&1)` texel order, the **new** capture is closer (43% exact, mean abs diff 4.8) than the stored one (15%, 10.2). Both read ~+4 brighter than the decode; filtering/dither not ruled out. No positional shift. Threshold-sensitive: 45257 px at tol 2, 16197 at tol 8, 6818 at tol 16. |
| `pvr_dt_diag` (textured) | PASS | 4912 | **Checked against the fixture's final frame** (red clear, white marker at (300,300), pot panel at (16,16) with the red/green checker at target (8,8,48,48)): new matches expected except 1001 px, stored mismatches expected at 4192 px (black panel, missing marker). So stored is the wrong frame and the new capture is the correct one, apart from the bands. New's remaining pixels: 704 px at y=0..1 x=128..639, ~16 px at y=479 x=608..637, and 281 panel block-boundary pixels. The band pixels read black in every run even when the frame before the final one was cleared to magenta (`pvr_dt_diag_bandprobe.c` probe run), so they are not retained previous-frame content; their extent also varies between runs (512/192, 512/203, 512/200), so they are unwritten pixels of the captured page reading as 0 — a capture artifact, not scene content. |
| `pvr_phase1_hardening` P1-B | PASS | 0 | recovery capture identical; built against the separate injection prefix `build-scripts/local-install-pvrtests` (never over `local-install`). |
| P1-C repeat pressure 2 | PASS | 249 x3 | same pattern as the already-recorded control: cyan rect one pixel left (new 440..535, stored 441..536) plus a 57-px marker-edge band at x=337..338 y=53..81. Stored vblank 1 here was the final scene, not the warmup page. |
| P1-C repeat pressure 3 | PASS | 249 x3 | same pattern as pressure 2. |

Not re-baselined (already re-run and passing against the new library): the
`pvr_dt_direct` gate, `pvr_phase1_targets`, hardening P1-A, P1-C repeat control,
and P1-C repeat pressure 1. Fixtures that do not use the capture helper were not
touched.

Tolerance sensitivity (tol 2 / 8 / 16): deferred 16/16/16, diag 4912/4912/4912
(real), dt_texture 45257/16197/6818 (threshold-sensitive, one LSB).

Hardware run output was not saved for the main fixtures; only the two follow-up
runs have run logs (`logs/rebaseline_run_pvr_dt_diag_bandprobe.log`,
`logs/rebaseline_run_pvr_phase0_deferred_texture_repeat.log`).

New captures and logs are `rebaseline_*` under `results/phase0-captures/` and
`results/phase1-captures/`; build logs are under `logs/`. Stored captures are kept.

The `pvr_phase0_boundary` fixture is not valid for the promoted library: case 1
expects `target_texture` to be rejected, which was true before promotion. It needs
a one-line expectation update or retirement; nobody has taken that, and it produced
no capture.

### Closed: 1-pixel offset in the stale library (no further investigation planned)

The pre-2026-10-06 `local-install` library drew the hardening P1-C scene's
untextured cyan rectangle one pixel to the right (x=441..536 instead of the
specified 440..535). The offset was reproduced with the front-buffer helper
(`results/phase1-captures/stalelib_pvr_phase1_hardening_p1c_repeat_control*`), so
it is a library difference, not a capture-path one. The promotion fixture was
pixel-identical across the old and new libraries, so the old library did not
shift every draw; which draws it affected was never determined.

The old install archive was deleted and the investigation was closed on purpose:
the current library draws the specified geometry, and earlier results can be
re-run against it. To reopen it, rebuild the old library from the commit it was
built from (it predated the working-tree changes to
`src/render/dreamcast/SDL_render_dreamcast_pvr.c` that existed on 2026-10-06; the
exact build commit was not recorded), build the hardening `p1c_repeat_control`
fixture against it with `SCREENSHOT=1` and explicit `SDL3_PREFIX` /
`SDL3_CONFIG_INCLUDEDIR`, and compare the cyan rectangle's x range at all three
captures. Do not use the old library as a pass artifact.

### Gate 8 deferred-texture corner: intermittent, correlates with displayed page (2026-10-06)

Six runs of the unmodified gate against the current library, built with the trace
define (`deferred_trace_run{1..6}` under `results/phase0-captures/`): the
16-pixel deviation at x=128..135 y=176..177 (top-left of the destroyed texture's
quad) appears in 5 of 6 runs and is exactly the same 16 pixels each time. It is
present on all 5 runs where the displayed PVR page was `0xa5129100` (linear
`0xa5094880`, the `vram_l` page) and absent on the 1 run where it was
`0xa5929100`. Which page is displayed varies run to run for the same ELF.

Modes of the probe switch in the fixture (`-DDEFERRED_PROBE_MODE`): 1 (never
destroy) and 2 (destroy after present) showed 0 mismatches on the page that
produces the defect in mode 0, one run each. The trace shows the deferral is
behaving as designed: destroy with `scene_active=1`, texture VRAM `0xa4455160`
freed only afterward. The cause is not established. So gate 8 is **not** a clean
pass: it is intermittent, parity-dependent, and needs the destroy-before-present
path; the earlier stored pass is consistent with having captured the other page.
Next step if pursued: log the freed block against the page that is displayed and
check the allocator's in-band metadata writes against the display page.

### Gate 8 corner: cause and fix (2026-10-07)

Cause: `pvr_wait_render_done()` only blocks when KOS `render_busy` is set, and that
is set from the TA-complete interrupt. If the TA has not finished the scene, the
wait is a no-op, `DC_PVR_ReleaseDeferredTextures()` runs, and `pvr_mem_free`
writes dlmalloc free-list pointers into the first 8 bytes of the freed texture
before the PVR fetches those texels. Those 8 bytes are texels 0..3 of row 0,
which is exactly the 16-pixel corner (decoded values match `0x8c1311dc`). A raw
`pvr_mem_malloc` after the free returned the same block with those pointers in it
(probe mode 3, 6 runs). Displayed page was not the discriminator.

Fix: `DC_PVR_FinishActiveScene` calls `pvr_wait_ready()` before
`pvr_wait_render_done()`, so the render-done wait is a real barrier. Verified on a
non-trace library: the unmodified gate, 10 runs, 0 mismatching pixels in all of
them (full frame), on the page that was defective before. Regression runs on the
fixed library: `pvr_phase0_boundary` 7/7, `pvr_phase0_lifecycle`,
`pvr_phase0_textured_quad` (same captures as the pre-fix rebaselines), and
`pvr_phase1_hardening` P1-A, P1-B (injection prefix), P1-C repeat control and
repeat pressure 1 to 3: all PASS, captures identical to the pre-fix rebaselines.

Cost of the added wait. Measured with `clock_gettime(CLOCK_MONOTONIC)`, because
`SDL_GetTicksNS` is wrong on this port (it advanced 14 ms during a 2000 ms
`SDL_Delay`; `SDL_systimer.c` reads the SH4 perf counter and returns a frequency
constant, `5 * 1000000000`, that overflows a 32-bit int). Synthetic bench, 3
alternating runs per variant, spread under 0.1%:

| barrier before the deferred free | per render-target switch | per frame (8 quads + present) | gate 8 |
|---|---|---|---|
| none: `pvr_wait_render_done()` only (racy original) | 3.28 ms | 16.65 ms (60 fps) | 6 of 6 runs corrupt |
| `pvr_wait_ready()` + `pvr_wait_render_done()` (this fix) | 10.03 ms | 20.05 ms (50 fps) | 3 of 3 clean |
| spin until `pvr_check_ready() == 0`, then `pvr_wait_render_done()` | 10.03 ms | 20.05 ms | 8 of 8 clean |
| spin until `pvr_check_ready() == 0`, then spin until `render_busy == 0` | 3.43 ms | 16.67 ms | 10 of 10 clean |

So the fix is correct but currently imposes a scheduler-tick-sized stall: about
+6.7 ms per scene finish at a target switch and +3.4 ms per presented frame in this
synthetic. Every blocking wait here costs a full ~10 ms (durations are multiples of
about 10 ms), which fits sleeping waits waking on the scheduler tick; an inference,
not separately proven. A wait that spins instead of sleeping pays only the real PVR
time and is just as correct. `pvr_check_ready()` returns 0 when the TA is ready (the
render has started) and -1 when busy, the opposite of a boolean. An earlier version of
this section claimed +73 us and +0.12 ms (broken SDL clock) and later that a spin on
`pvr_check_ready()` was not a valid barrier (the loop condition was inverted, so it
never waited); both claims were wrong and are retracted.

A frame whose CPU work is long and whose GPU work is short loses CPU/GPU overlap when
the barrier waits for render-done at all (spin-wait vs no barrier, ms per frame:
100 sprites 23.7 vs 17.3, 600 points 43.6 vs 37.0; GPU-heavy frames are unaffected,
12 full-screen translucent quads 51.8 vs 50.3). Reclaiming deferred textures without
waiting would recover that. The sleeping wait should not be treated as the final
design; see `results/pvr-scene-completion-design.md`.

Evidence kept (the rest of the runs are not committed; counts above are from the
worklog): pre-fix failing run `results/phase0-captures/deferred_trace_run1.{log,ppm}`
(16 px defect, displayed page `0xa5129100`); post-fix clean runs
`results/phase1-captures/rebaseline_fix_g8_mode0_run{1,5,10}.{log,ppm}`, all displayed
on `0xa5129100`. Hardening regression logs were not committed.

## Timer calibration

`timer_calibration` checks `SDL_GetTicks`/`SDL_GetTicksNS`/`SDL_GetPerformanceFrequency`
against `clock_gettime(CLOCK_MONOTONIC)` (which tracks real time on the console)
across sleeping, vblank-waiting and busy phases, plus a tick-based deadline loop.
Build it against the library under test with
`make -f Makefile.dc SDL3_PREFIX=<prefix> SDL3_CONFIG_INCLUDEDIR=<prefix>/include`
and run it through the locked launcher. It prints `timer_calibration result=PASS` or
`FAIL` and returns 0 or 1; every phase must be within 2% and the frequency must be 1e9.

Before the fix, `SDL_systimer.c` read the SH4 perf counter and returned a frequency of
`5 * 1000000000`, which overflows a 32-bit int to 705032704. SDL's clock then ran 1.41x
fast while busy and nearly stopped while a thread slept (14 ms over a 2000 ms
`SDL_Delay`), so a 500 ms tick-deadline loop with `SDL_Delay(1)` took 51.7 s of real
time. All six checks failed. After the fix (generic `clock_gettime(CLOCK_MONOTONIC)`
path) all pass, within 0.01%. Note that `SDL_Delay(1)` itself waits about 10 ms (the KOS
scheduler tick), which a 500 ms deadline loop shows as ~51 iterations.

Any SDL-reported frame rate or timing taken on Dreamcast before this fix, including
the fps lines printed by the SDL test programs, was not wall-clock.
