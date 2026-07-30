# PVR Render-Target Performance Comparison

**Status:** Measurement test — not renderer optimization or promotion.

**Date:** 2026-07-29

**Scope:** Measure the CPU-visible and end-to-end frame cost of the guarded
prototype's logical-screen RTT plus physical-presentation path compared with
the ordinary direct-screen renderer.

## 1. Design

This test builds two separate ELFs from the same source file. Both ELFs run
identical screen-only workloads (A–D) and report per-frame durations. The
prototype ELF additionally reports workloads (E–F) that exercise target-texture
lifecycle.

The measurement interval begins immediately before the workload's draw
commands and ends after one explicit `vid_waitvbl()` returns. It includes:

- SDL command ingestion and queue construction;
- SDL command queue execution;
- RTT scene finish (prototype, when active);
- physical presentation scene (prototype draws the completed logical screen);
- `pvr_wait_render_done()` inside the renderer's `SDL_RenderPresent` path; and
- one explicit vertical-blank wait after presentation.

This is a wall-clock measurement of the full frame path, not an isolated
texture-read or geometry benchmark.

**Workloads E and F are prototype-only and are not included in the ordinary-
versus-prototype screen-only overhead comparison.**

## 2. Timer Choice

**API:** `timer_us_gettime64()` from `<kos/timer.h>`

**Declaration:**

```c
static inline uint64_t timer_us_gettime64(void);
```

**Returns:** Number of microseconds since KOS was started (monotonic uptime),
as a 64-bit unsigned integer.

**Why this is correct:**

- It is the standard KOS high-resolution monotonic timer, documented in
  `kos/timer.h` as returning microseconds since boot.
- It is implemented as a `static inline` that calls `arch_timer_gettime()`
  (which reads the SH-4 hardware timer registers) and converts the result to
  microseconds.
- It is used throughout KOS examples and the SDL2 renderer for frame timing.
- It is not the RTC (which is wall-clock date/time), not a one-shot timer
  (which is interrupt-based), and not a millisecond timer (which loses
  sub-millisecond precision).
- The timer does not require initialization or registration — it is available
  from the first instruction after KOS boot.

**Resolution:** Microsecond granularity. The underlying hardware timer on the
SH-4 runs at the CPU frequency (200 MHz on the Dreamcast), so the effective
resolution is sub-microsecond; the API rounds down to the nearest microsecond.

## 3. Workload Definitions

All workloads use fixed counts. No unbounded loops, random inputs, SDL_Delay,
or screenshot calls inside measured intervals.

### Workload A — Clear-only

- One full-screen `SDL_RenderClear`.
- One `SDL_RenderPresent` per frame.
- **Purpose:** Baseline overhead with minimal draw commands.

### Workload B — Moderate untextured geometry

- One full-screen clear.
- 100 deterministic filled rectangles (positions, sizes, and colors derived
  from a loop index — no randomness).
- `SDL_BLENDMODE_NONE` and `SDL_BLENDMODE_BLEND` in stable alternating order.
- One `SDL_RenderPresent` per frame.
- **Purpose:** Untextured geometry path overhead.

### Workload C — Moderate textured geometry

- One deterministic RGB565 texture (128x128, static, checkerboard) created
  before timing begins.
- One full-screen clear.
- 200 deterministic texture copies (positions, sizes, color modulation, and
  alpha modulation derived from a loop index).
- Nearest filtering (default) and color/alpha modulation exercised.
- One `SDL_RenderPresent` per frame.
- **Purpose:** Textured geometry path overhead.

### Workload D — Higher command load

- One full-screen clear.
- 400 deterministic draws — a documented mix of filled rectangles and texture
  copies (odd indices = rectangles, even indices = texture copies).
- Blend modes `SDL_BLENDMODE_NONE` and `SDL_BLENDMODE_BLEND` alternate.
- Stay below the validated TA-capacity boundary (well under several thousand
  sprites).
- One `SDL_RenderPresent` per frame.
- **Purpose:** Higher command-load overhead.

### Workload E — 256x256 target update (prototype only)

- Render into a 256x256 RGB565 target (seed with a unique red/blue marker).
- Reset to screen.
- Sample the target once via `SDL_RenderCopy`.
- Present.
- Measure the complete frame.
- **Purpose:** Per-frame cost of target reselection, RTT scene, and
  preservation seed.

### Workload F — Target preservation (prototype only)

- Seed an initial yellow marker into a 256x256 target before timing.
- For each measured frame: reselect target, add a small green marker at a
  bounded position, reset to screen, sample target, present.
- Verify the original marker remains visible afterward.
- **Purpose:** Per-frame cost of repeated target reselection and preservation
  seed across many frames.

**Workloads E and F are NOT included in the ordinary-versus-prototype screen-only
overhead comparison.**

## 4. Warmup and Sample Counts

| Parameter | Value |
|---|---|
| Warmup frames per workload | 60 |
| Measured frames per workload | 300 |
| Total measured frames (A–D only) | 1200 |
| Total measured frames (A–F, prototype) | 1800 |

## 5. Statistical Method

For each workload's 300 measured durations (in microseconds):

| Statistic | Method |
|---|---|
| **Minimum** | Smallest duration value |
| **Maximum** | Largest duration value |
| **Arithmetic mean** | `total / 300` (integer division) |
| **Median** | Integer average of zero-based sorted indices 149 and 150 |
| **p95** | Nearest-rank value at zero-based index `ceil(300 * 0.95) - 1 = 284` |
| **p99** | Nearest-rank value at zero-based index `ceil(300 * 0.99) - 1 = 296` |
| **Total duration** | Sum of all 300 durations |
| **Frames > 16667 µs** | Count of durations exceeding 16667 (1/60 Hz) |
| **Frames > 20000 µs** | Count of durations exceeding 20000 |
| **Effective FPS (×1000)** | `300 * 1000000000 / total_us` (integer division) |

All statistics use integer arithmetic. No floating-point formatting.

The ascending sort is implemented via insertion sort (efficient for N = 300).

## 6. Build Commands

### Ordinary build

```bash
cd test/dreamcast/test/pvr_render_target_performance
make -f Makefile.dc ordinary.elf ordinary.bin
```

### Prototype build

```bash
cd test/dreamcast/test/pvr_render_target_performance
make -f Makefile.dc prototype.elf prototype.bin
```

### Both builds (with symbol checks)

```bash
make -f Makefile.dc all
```

### With hardware screenshots

```bash
make -f Makefile.dc HARDWARE_SCREENSHOTS=1 all
```

## 7. ELF / BIN / MAP Paths

| Build | ELF | BIN | MAP |
|---|---|---|---|
| Ordinary | `ordinary.elf` | `ordinary.bin` | `ordinary.map` |
| Prototype | `prototype.elf` | `prototype.bin` | `prototype.map` |

## 8. Linker Isolation Checks

Both builds verify their linkage via `sh-elf-nm` on the ELF (not the map,
which the Makefile also checks):

### Ordinary ELF checks

```bash
sh-elf-nm ordinary.elf | grep -q '_DC_RenderDriver'    # must PASS
sh-elf-nm ordinary.elf | grep -q '_DC_PVR_SetRenderTarget'  # must FAIL (absent)
```

### Prototype ELF checks

```bash
sh-elf-nm prototype.elf | grep -q '_DC_RenderDriver'    # must PASS
sh-elf-nm prototype.elf | grep -q '_DC_PVR_SetRenderTarget'  # must PASS
```

These checks are automated in the Makefile via the `check-ordinary` and
`check-prototype` phony targets.

The ordinary ELF links the explicit local archive at
`build-scripts/dcbuild/libSDL2.a`. The prototype ELF links
`build-scripts/dcbuild_prototype/libSDL2.a`; neither defaults to the installed
SDL archive.

Map-file equivalents (linker map uses underscore-less names):

```bash
grep 'DC_RenderDriver' ordinary.map       # must PASS
grep 'DC_PVR_SetRenderTarget' ordinary.map  # must FAIL
grep 'DC_RenderDriver' prototype.map       # must PASS
grep 'DC_PVR_SetRenderTarget' prototype.map  # must PASS
```

## 9. Hardware Launch Commands

### Ordinary

```bash
kos-tool -t 192.168.0.128 -x ordinary.elf -m .
```

### Prototype

```bash
kos-tool -t 192.168.0.128 -x prototype.elf -m .
```

**Do not terminate kos-tool or dc-load while the ELF is running.** Let the
test exit normally and wait for `Program returned`. If the console becomes
unresponsive, ask the onsite operator to restart the Dreamcast.

## 10. Serial Summary Format

### Machine-readable PERF lines

```
PERF build=ordinary workload=A frames=300 min_us=... max_us=... mean_us=... median_us=... p95_us=... p99_us=... over_16667=... over_20000=... fps_x1000=... total_us=...
PERF build=prototype workload=A frames=300 min_us=... max_us=... mean_us=... median_us=... p95_us=... p99_us=... over_16667=... over_20000=... fps_x1000=... total_us=...
```

### VRAM lines

```
VRAM build=ordinary available=...
VRAM build=prototype available=...
```

### Pass/fail line

```
PASS: YES
PASS: NO
```

### Interpretation

- `fps_x1000` of 60000 means exactly 60 FPS. Values below 60000 indicate
  the frame budget was exceeded.
- `over_16667` counts frames exceeding the 1/60 Hz threshold.
- `over_20000` counts frames exceeding 20 ms.
- Report both absolute timings and `prototype_minus_ordinary` deltas for each
  workload.

## 11. Screenshot Expectations

### Capture paths

| Build | Path |
|---|---|
| Ordinary | `/pc/pvr_render_target_performance-ordinary.ppm` |
| Prototype | `/pc/pvr_render_target_performance-prototype.ppm` |

### Visual expectations (both builds)

Each capture must show the same deterministic reference screen:

1. **Dark background** (blue-ish, 10/10/20).
2. **Textured panel** (warm-toned checkerboard, 240×180).
3. **Translucent overlap** (magenta, alpha 128).
4. **Four white screen-corner markers** (8×8 pixels).
5. **Green status bar** (24 px tall at bottom).

### Prototype-only additional expectation

The prototype capture must additionally show a small sampled
target-preservation panel (workload F) without obscuring the shared comparison
scene.

### Screenshot conditions

Screenshots are captured only when `HARDWARE_SCREENSHOTS=1` is set in the
Makefile. The compile definition `DC_PVR_RENDER_TARGET_PERFORMANCE_HARDWARE_SCREENSHOT`
enables `vid_screen_shot()` calls. Each ELF captures exactly once at the end.

## 12. Interpretation Rules

1. **Physical hardware is authoritative.** Do not use Flycast or any emulator.
2. **Do not silently define an acceptable overhead threshold.** Report absolute
   timings and prototype-minus-ordinary deltas without declaring any value
   acceptable or unacceptable.
3. **Separate screen-only overhead from target-specific cost.** Workloads E and
   F are prototype-only and must not be mixed into the A–D comparison.
4. **Note the approximately 2 MiB prototype logical-screen VRAM cost**, but use
   measured `pvr_mem_available()` values from the serial output rather than
   inferred numbers.
5. **A KOS out-of-memory message is irrelevant to this benchmark** and should
   not occur. If it does, report it but do not treat it as a performance result.
6. **Any freeze, reset, timeout, missing screenshot, failed recovery, or
   visual mismatch is a test failure.** Do not continue past such a failure.
7. **Do not recommend removing the prototype guard** until physical measurements
   are available and reviewed.

## 13. Measurement Limitations

- The timer measures wall-clock time including SDL API ingestion, render-done
  synchronization, and an explicit vblank wait, not isolated GPU execution.
- The timer does not separate SDL command queue execution from PVR submission
  from scene finish — it captures the full path.
- `pvr_wait_render_done()` latency is included.
- Background KOS activity (network, filesystem) may add jitter.
- The test does not measure upload bandwidth, texture-read bandwidth, or
  fill-rate separately.
- The test does not exercise twiddled textures, VQ compression, DMA, or other
  Phase 6 optimizations.
- The warmup count (60 frames) is sufficient to stabilize the renderer state
  but does not guarantee thermal or cache stabilization.
- The test does not measure initial renderer-creation cost, only per-frame
  presentation cost.

## 14. Files Created

| File | Purpose |
|---|---|
| `pvr_render_target_performance.c` | Test source (both builds) |
| `Makefile.dc` | Build system (ordinary + prototype targets) |
| `README.md` | This document |

## 15. Physical-Hardware Results

The final source was built with strict warnings and hardware screenshots:

```bash
make -f Makefile.dc -B HARDWARE_SCREENSHOTS=1 all
```

Both ELFs passed their symbol gates, completed on a physical 640x480 VGA
Dreamcast, wrote their raw captures, reported zero failures, returned zero,
and returned control normally to `dc-load-ip`.

Authoritative evidence retained in this directory:

- `ordinary-hardware-20260729-final.log`
- `prototype-hardware-20260729-final.log`
- `pvr_render_target_performance-ordinary-final.ppm`
- `pvr_render_target_performance-prototype-final.ppm`

### Screen-only comparison

| Workload | Ordinary mean | Prototype mean | Prototype - ordinary | Ordinary FPS | Prototype FPS |
|---|---:|---:|---:|---:|---:|
| A | 16,679 us | 16,675 us | -4 us | 59.954 | 59.969 |
| B | 16,678 us | 22,236 us | +5,558 us | 59.956 | 44.970 |
| C | 16,678 us | 33,356 us | +16,678 us | 59.956 | 29.978 |
| D | 33,361 us | 50,039 us | +16,678 us | 29.974 | 19.984 |

The ordinary renderer reported 5,943,040 bytes available after creation. The
prototype reported 3,845,800 bytes, a reduction of 2,097,240 bytes from the
paired 1024x512 RGB565 logical-screen allocations.

Prototype-only target workload E averaged 33,357 us (29.978 FPS). Repeated
preservation workload F averaged 25,012 us (39.980 FPS). These values describe
target use and are not part of the A-D ordinary/prototype comparison.

Direct inspection of both final raw captures passed. Their shared dark
background, checker panel, translucent magenta overlap, green status bar, and
four white framebuffer-corner markers match. The prototype additionally shows
the sampled preservation target with its green upper-left and yellow
lower-right markers. No missing geometry, edge loss, stale frame, or corruption
is visible.

## 16. Promotion Decision

The current guarded preservation implementation is functionally correct on the
tested hardware, but its always-on logical-screen RTT path causes material
screen-only regressions in B-D and reserves about 2 MiB of VRAM even when an
application never creates a target texture. Therefore this implementation is
**not promoted to the ordinary renderer**. Keep
`DC_PVR_RENDER_TARGET_PROTOTYPE` and ordinary capability isolation in place.

Future promotion work must avoid charging ordinary screen rendering for the
paired logical-screen path, or establish an explicit opt-in policy with costs
acceptable to the project. Wider video-mode testing and optimization do not
change this measured 640x480 VGA decision by themselves.

## 17. Ordered-render-target follow-up (hardware validated)

The build now also accepts `SDL2_ORDERED_LIBDIR` and produces `ordered.elf`
from an archive compiled with `DC_PVR_ORDERED_RENDER_TARGETS`. The ordered
program enables `SDL_HINT_DC_PVR_RENDER_TARGETS`, runs the same screen-only
workloads A-D and target workloads E-F, and writes this additional capture when
screenshots are enabled:

```text
/pc/pvr_render_target_performance-ordered.ppm
```

Physical 640x480 VGA validation passed for both builds. Each reported zero
failures, returned zero normally to `dc-load-ip`, and wrote its raw capture.
Both builds reported 5,943,040 bytes available after renderer creation.

| Workload | Ordinary mean | Ordered mean | Difference |
|---|---:|---:|---:|
| A | 16,679 us | 16,679 us | 0 us |
| B | 16,678 us | 16,678 us | 0 us |
| C | 16,678 us | 16,678 us | 0 us |
| D | 33,361 us | 33,361 us | 0 us |
| E | — | 16,679 us | target update |
| F | — | 16,678 us | target preservation |

The ordinary and ordered reference captures share the dark background, textured
panel, magenta overlap, and green status bar. The ordered capture additionally
shows its expected sampled-target panel with green and yellow markers. No
unexpected geometry, screen-copy artifact, or visual corruption was observed.
This validates the low-cost ordered path for the tested 640x480 VGA workload;
wider video-mode and stress coverage remains future hardening.
