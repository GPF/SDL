# PVR line capacity probe (bounded, single frame per count)

Test-only diagnostic. It changes no backend behavior.

## Question

The geometry bench line workload timed out at high call counts. Which phase
actually fails: TA submission, OPB/tile capacity, or render completion time?

## Method

One frame per line-call count, sweep `1, 2, 4, 6, 7, 8`, stop at the first
anomaly. Each call draws one diagonal segment; lines are parallel and offset by
24 pixels so they never overlap, and each call uses a distinct RGB565-exact
color so dropped geometry is measurable per call.

Per count the probe records:

* SDL API errors during submission;
* CPU submit time (a submission stall shows up as a huge elapsed time);
* `PVR_GET` TA register state (`PVR_TA_OPB_START/END/POS`,
  `PVR_TA_VERTBUF_START/END/POS`);
* `SDL_RenderPresent` elapsed time;
* true render completion, measured by calling `pvr_wait_render_done()`
  repeatedly until it succeeds — a single KOS call gives up after 100 ms and
  cannot distinguish "still rendering" from "never completes";
* a physical framebuffer capture;
* a known-good recovery frame (black clear + gray marker).

Both ELFs link explicitly against isolated archives, and each linker map is
checked:

```sh
make -B -f Makefile.dc SDL2_LIBDIR=/tmp/build-fastpath \
  SDL2_INCLUDEDIR=/tmp/build-fastpath/include/SDL2 \
  SDL2_CONFIGDIR=/tmp/build-fastpath/include-config-/SDL2 \
  EXTRA_CFLAGS="-DPVR_HARDWARE_SCREENSHOT"
```

Baseline archive: `/tmp/build-baseline` (pinned `HEAD`, pre-fast-path).
Fast-path archive: `/tmp/build-fastpath` (working tree).

## Hardware results (real Dreamcast, 640x480 VGA, 2026-07-31)

The archived logs were captured before the fixed-cost model in the probe was
corrected from 44 to 76 bytes. Consequently, their printed `predicted_usage`
values are 32 bytes low. The register-derived `vertbuf_bytes_reported` values
are unaffected and are the measurements shown below. The probe source now uses
the corrected 76-byte fixed term; the hardware run was not repeated for this
reporting-only correction.

`pixels_per_call = 575`, so quads = `575 x calls`, triangles = `2 x quads`,
vertex commands = `6 x quads`.

| Calls | Vertex commands submitted | Modeled bytes requested | Vertex-buffer high-water | Render | Geometry per call |
|---:|---:|---:|---:|---|---|
| 1 | 3,450 | 69,076 | 69,076 / 524,288 | completes ~12 ms | 575 each — complete |
| 2 | 6,900 | 138,076 | 138,076 / 524,288 | completes ~13 ms | 575 each — complete |
| 4 | 13,800 | 276,076 | 276,076 / 524,288 | completes ~8-15 ms | 575 each — complete |
| 6 | 20,700 | 414,076 | 414,076 / 524,288 | completes ~15 ms | 575 each — complete |
| 7 | 24,150 | 483,076 | 483,076 / 524,288 (92.1%) | completes ~13-16 ms | 575 each — complete |
| 8 | 27,600 | 552,076 | **524,288 / 524,288 (register reaches end)** | **never completes** | no completed frame |

The modeled byte count is `20 * submitted_vertex_commands + 76`. At 8 calls,
the register reaches the configured buffer end even though the requested modeled
size is 552,076 bytes. The render does not complete during ten 100 ms waits; the
recovery frame also fails, and PVR shutdown times out. The failure was not
recoverable within this run.

`pvr_check_ready()` is `-1` before present and `0` after the failed wait. In KOS,
that transition occurs when list registration completes and the render is
started; `pvr_wait_render_done()` still timed out. The failure was after
submission, during render completion, and no SDL API error occurred during
submission. The timeout alone does not identify which hardware capacity caused
the incomplete render.

## Cause

The vertex-buffer register reaches its configured end at the first failing
count. Measurements for 1, 2, 4, 6, and 7 calls fit the empirical model
`20 bytes per vertex command + 76 fixed bytes`; extrapolating that model gives
an estimated maximum of 26,210 commands (about 4,368 six-vertex quads, or 7.6
calls at 575 pixels per call). This exact command threshold was not independently
swept: the hardware test brackets it between 24,150 successful and 27,600
failing commands. The eight-call workload requests 552,076 modeled bytes, or
105.3% of the configured buffer.

The test does not establish that OPB/tile capacity was irrelevant. The aggregate
OPB allocation is 76,800 bytes, but it is partitioned into per-tile bins; a
scene-wide triangle count cannot show that every tile stayed within its bin
capacity. `PVR_TA_OPB_POS` was constant across this sweep and does not provide
per-tile occupancy. Vertex-buffer exhaustion is directly observed at the
failing count; a simultaneous OPB limit has not been ruled out.

Successful counts completed in 8-16 ms, while the failing count remained
incomplete for about 1.016 seconds. That makes ordinary render cost an unlikely
explanation, but the timeout does not prove the failed scene could not take
longer. The filled vertex-buffer register is independent evidence of buffer
exhaustion.

## Baseline versus fast path

At successful counts 1-7, the per-call pixel counts match and the captures are
**byte-identical** between the two archives. At failing count 8, the captures
are also byte-identical, but both show the previous completed front buffer; that
capture does not compare geometry from the incomplete scene:

* count 7: `3fbbef777cfca6e81d3309b3cae8ccbd96d579b0d78a6be26cd19a8fffd6d23c`
* count 8: `7598c64904bb9d36972370c1b4642997ad451aea1328cf4e6784f61112fc3a75`

The measured difference is CPU timing: the fast path submits during the draw calls
(~21.7 ms for 8 calls), while the baseline queues in ~1.4 ms and submits inside
`SDL_RenderPresent` (~103 ms). The hardware runs show the same observed
success/failure boundary. Emitted-vertex equivalence is established separately
by the host differential harness.

## Conclusion

No fast-path-specific backend defect is reproduced: baseline and fast-path
archives have the same success/failure boundary. At eight calls the measured
vertex-buffer cursor reaches its configured end, strongly implicating vertex
buffer exhaustion. The test does not exclude an OPB/tile limit or a render taking
longer than the observation window as a simultaneous or alternative cause.
Backend behavior is left unchanged.

Uncertainty: `pvr_prim()` has no software overflow result in direct-submit mode,
so the exact hardware behavior on vertex-buffer overflow is not established by
its return value. KOS's debug interrupt handler can log overflow events, but
those logs do not establish which capacity caused this run's incomplete scene.
The 20-bytes-per-command plus 76-byte fixed-cost model is empirical, not a
documented hardware fact, and was measured with one stable polygon header. A
future capacity guard would need to account for header changes and be validated
against hardware behavior.

The original geometry benchmark's 3,000 line calls are not a valid single-scene
workload. SDL clips each line to the 640x480 viewport before Bresenham
rasterization; applying that clipping to the benchmark endpoints yields about
1,529,154 points, or 9,174,924 vertex commands (about 350x the estimated
26,210-command limit). The current benchmark is bounded to two line calls per
frame.

Full logs: `hardware-fastpath.log`, `hardware-baseline.log`.
Captures: `fastpath-captures/`, `baseline-captures/`.
