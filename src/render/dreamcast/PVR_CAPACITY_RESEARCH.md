# Dreamcast Native PVR Renderer: TA/OPB Capacity and Command-Load Stress Research

**Status:** Research, stress-test implementation, and focused 640x480 VGA physical-hardware validation complete.
**Date:** 2026-07-29
**Scope:** PVR capacity definitions, KOS overflow behavior, command-to-PVR expansion estimates, header-cache impact, SDL queue versus PVR capacity analysis, scene-splitting feasibility, capacity observability, stress-workload design, hardware-first validation policy, failure-and-recovery policy, renderer-safeguard options, and scope boundaries.

**Related design:** `PVR_RENDERER_RESEARCH.md` records the established renderer architecture and lifecycle contract. `PVR_CLIPPING_RESEARCH.md` records the validated CPU clipping contract. `PVR_POINTS_LINES_RESEARCH.md` records the points/lines expansion contract. `PVR_TEXTURE_RESEARCH.md` records the texture subsystem contract. `PVR_PARTIAL_UPLOAD_RESEARCH.md` audits the KOS transfer APIs.

## 0. Current renderer summary

The native PVR renderer is complete through Phase 3.4:

* renderer-owned PVR lifecycle;
* SDL command queue integration;
* viewport and clipping;
* points, lines, filled rectangles, and geometry;
* blending and color modulation;
* RGB565, ARGB1555, and ARGB4444 textures;
* static and streaming textures;
* logical NPOT textures with POT backing;
* full and partial updates and locks;
* synchronous full-backing uploads;
* nearest and bilinear filtering;
* copy, scaling, rotation, flips, and custom geometry;
* texture allocation rollback;
* VRAM exhaustion recovery;
* copy-on-write VRAM replacement when submitted geometry still references an allocation.

Partial VRAM uploads and DMA remain intentionally deferred. Non-VGA video modes and TA/OPB stress coverage remain Phase 7 hardening.

This increment focuses exclusively on command-load capacity and focused stress validation.

---

## 1. Current PVR initialization configuration

The renderer initializes the PVR exactly once with a fixed `pvr_init_params_t`:

```c
static const pvr_init_params_t pvr_params = {
    { PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
    512 * 1024,   /* vertex_buf_size = 512 KiB */
    0,            /* dma_enabled = 0 (direct submission, no DMA) */
    0,            /* fsaa_enabled = 0 */
    1,            /* autosort_disabled = 1 (preserve SDL painter order) */
    3,            /* opb_overflow_count = 3 (extra OPB partitions) */
    0             /* vbuf_doublebuf_disabled = 0 (double-buffered vertex buffer) */
};
```

### 1.1 Field summary

| Field | Value | Effect |
|-------|-------|--------|
| `opb_sizes[2]` | `PVR_BINSIZE_16` | Only `PVR_LIST_TR_POLY` enabled (16-word / 64-byte bin size). All other lists disabled. |
| `vertex_buf_size` | 512 KiB per frame buffer (double-buffered, 1 MiB total) | Per-scene TA vertex capacity. KOS assigns `buf->vertex_size = params->vertex_buf_size` for each of the two frame buffers (`pvr_buffers.c`, frames at `0` and `0x400000` in 8 MiB PVR RAM). Corrected by hardware measurement in Section 14; the earlier "256 KiB per frame / 8,192 units" figure was wrong. |
| `dma_enabled` | 0 | Direct submission via store queues. `pvr_prim()` writes directly to the TA input. |
| `autosort_disabled` | 1 | Translucent primitives processed in submission order. Preserves SDL painter order. |
| `opb_overflow_count` | 3 | Three extra OPB partitions beyond the base. KOS provides no renderer-level capacity result; its PVR interrupt handler can emit debug logs for OPB/TA overflow events. |
| `vbuf_doublebuf_disabled` | 0 | Two frames worth of vertex data registered simultaneously. |

### 1.2 List usage

The renderer begins and finishes exactly one list per scene:

```c
pvr_list_begin(PVR_LIST_TR_POLY) → pvr_list_finish()
```

Opaque, modifier, and punch-through lists remain disabled.

### 1.3 Framebuffer mode interaction

The renderer requires `SDL_HINT_DC_VIDEO_MODE=SDL_DC_DIRECT_VIDEO`. Width must be divisible by 32. The same `pvr_params` are used for all supported modes; non-VGA mode implications are documented in Section 2.2 but not implemented in this increment.

---

## 2. Capacity limits

### 2.1 TA vertex buffer (authoritative)

**Measured configuration:** `vertex_buf_size` is 512 KiB for each of two TA
buffers. On 640x480 VGA with direct submission and one stable polygon header,
hardware measurements fit `20 bytes per submitted vertex command + 76 fixed
bytes`. This gives an estimated limit of 26,210 vertex commands per active
buffer. A line-count sweep brackets a failing workload between 24,150 and
27,600 commands; it does not independently test the exact 26,210-command
boundary. This is an empirical model, not a general budget for changing polygon
headers, other video modes, or different PVR configurations. See Section 14.

* **Fixed in hardware.** Selected during `pvr_init` via `vertex_buf_size`.
* **Available for each synchronized scene.** TA capacity becomes available for a later properly synchronized scene through hardware-managed buffer rotation. In direct-submission mode, `pvr_scene_begin()` does not perform a software reset of the TA or OPB pointers.
* **Double buffering does not protect against excessive submission within one scene.** It provides hardware-managed buffer rotation between properly synchronized scenes. Submission beyond the active TA capacity is unsafe. The line-capacity probe observed a full vertex-buffer cursor and an incomplete scene at the first failing count, but the precise hardware overflow behavior and possible simultaneous OPB pressure are not isolated.
* **`pvr_shutdown()` plus `pvr_init()` is not guaranteed to recover.** See Section 3.

### 2.2 OPB (non-authoritative estimate)

The OPB is the PVR's tile-bin structure. For `PVR_LIST_TR_POLY` at 640×480 with `PVR_BINSIZE_16`:

* 300 tiles × 64 bytes per bin = 19,200 bytes per OPB partition.
* Four partitions (base + 3 overflow) = 76,800 bytes total.
* 8 primitive pointers per bin (64 bytes / 8 bytes per pointer).

**At even distribution:** 300 × 8 = 2,400 primitives per partition, 9,600 across four partitions.

**This is an illustrative estimate only.** OPB pressure is per-tile and workload-dependent:

* A tiny primitive touching one tile uses one bin entry.
* A fullscreen quad touches every tile, using one bin entry per tile.
* Long lines or large triangles touch many tiles, using many bin entries.
* Concentrated geometry in one tile may exhaust that tile's bin before the total OPB capacity is reached.
* Overlapping fullscreen translucent layers may exceed per-tile bin capacity.

**No OPB limit can be expressed as one reliable scene-wide primitive count.** The simple "tiles × pointers-per-bin" estimate is not a hard limit.

**Non-VGA modes:** At 320×240 there are 70 tiles, so one OPB partition holds 70 × 64 = 4,480 bytes (4.3× less total OPB memory). Per-tile bin capacity (8 pointers) is the same.

### 2.3 Polygon headers

Headers are 32-byte commands submitted through `pvr_prim()`. Each header configures texture, blend mode, filtering, depth, and culling for the following vertices.

* The renderer uses a header cache keyed on `(texture pointer, blend mode, texture_env, filter)`. Cached headers are reused; uncached headers are compiled and submitted.
* Each submitted header is a 32-byte input command to `pvr_prim()`. Its storage cost in the TA vertex buffer is not established by the single-header line probe; additional header submissions require separate calibration.
* OPB pressure depends on primitive tile coverage and binning; the inspected source does not establish a per-header OPB-bin cost.

### 2.4 Strip and triangle submission

The renderer's geometry paths emit independent triangles. Each triangle submits three `pvr_vertex_t` commands with `PVR_CMD_VERTEX_EOL` on the third. The ordered clear path is the exception: it emits one four-vertex strip.

* Per triangle: 3 commands, 96 bytes.
* Per quad (two triangles): 6 commands, 192 bytes.
* Per clipped triangle (worst case): up to 5 fan triangles, 15 commands, 480 bytes.

---

## 3. KOS overflow behavior

### 3.1 `pvr_prim()`

```c
int pvr_prim(const void *data, size_t size) {
    if(PVR_DEBUG && pvr_state.list_reg_open == PVR_LIST_NONE)
        return -1;
    if(!pvr_list_dma) {
        if(PVR_DEBUG && ((uintptr_t)data & 0x7))
            return -1;
        sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), data, size >> 5);
    }
    else return pvr_list_prim(pvr_state.list_reg_open, data, size);
    return 0;
}
```

* **Returns:** 0 on success, -1 on error (no list open, unaligned).
* **Capacity check:** None. Direct submission via store queues.
* **Critical finding:** `pvr_prim()` does not provide a useful capacity-exceeded result. It writes directly to the TA input. Submission beyond the active TA capacity is unsafe. KOS source alone does not establish the hardware consequence; the 640x480 VGA line probe observed an incomplete scene when the measured vertex-buffer cursor reached its end. It did not isolate whether another capacity limit also contributed.

### 3.2 `pvr_wait_ready()` and `pvr_wait_render_done()`

Both wait for the TA or ISP/TSP to finish processing the previous scene, with a 100 ms timeout.

* **Return:** 0 on success, -1 on timeout.
* **Cannot distinguish overflow from other causes.** A timeout may indicate TA capacity overflow, OPB overflow, an ordinary long render, corrupted hardware state, or unrelated PVR stalls.
* **Timeouts are ambiguous and are not reliable overflow detectors.**

### 3.3 `pvr_scene_begin()`

```c
void pvr_scene_begin(void) {
    pvr_state.next_to_texture = 0;
    pvr_state.ta_checked_ready = 0;
    pvr_state.lists_closed = 0;
    pvr_state.list_reg_open = PVR_LIST_NONE;
    // DMA path resets DMA buffer pointers.
    // Non-DMA (direct submission) path does NOT reset OPB or TA pointers — these are hardware-managed.
}
```

* **Returns:** void. It provides no capacity check.
* **Non-DMA mode:** TA capacity becomes available for a later properly synchronized scene through hardware-managed buffer rotation. In direct-submission mode, `pvr_scene_begin()` does not perform a software reset of the TA or OPB pointers.

### 3.4 `pvr_scene_finish()` (non-DMA path)

Closes the open list, submits blank lists for enabled but unvisited lists, and returns. Does not start rendering or flip buffers. Buffer flipping occurs elsewhere (ISP/TSP interrupt handler or `pvr_wait_render_done()`).

### 3.5 Summary

| API | Returns | Relevant behavior |
|-----|---------|-------------------|
| `pvr_scene_begin` | void | No capacity check; no software reset of TA or OPB pointers in direct-submission mode |
| `pvr_list_begin` | int | No capacity check (-1 if already closed) |
| `pvr_prim` | int | No capacity check (-1 if no list open or data is unaligned in debug builds) |
| `pvr_list_finish` | int | No capacity check (-1 if no list open) |
| `pvr_scene_finish` | int | No capacity check (-1 if no scene started) |
| `pvr_wait_ready` | int | Timeout-returning synchronization function; timeout result is ambiguous |
| `pvr_wait_render_done` | int | Timeout-returning synchronization function; timeout result is ambiguous |

### 3.6 Same-scene overflow consequence

If submission exceeds the active TA vertex-buffer capacity, `pvr_prim()` provides no capacity-exceeded result. The measured 640x480 VGA line probe observed the vertex-buffer cursor reach its 512 KiB end at 27,600 submitted commands and the scene fail to complete. The actual exhaustion boundary was not swept exactly, and the run does not rule out simultaneous OPB/tile pressure. A synchronization timeout alone remains ambiguous.

**Recovery, if possible, may require `pvr_shutdown()` plus `pvr_init()`, but success is not guaranteed.**

### 3.7 Flycast versus hardware

Flycast may handle overflow differently than hardware (e.g., silently dropping primitives). Physical hardware enforces double-buffer synchronization strictly. Hardware is primary. Flycast is optional diagnostic aid.

---

## 4. Current renderer command expansion

| Command | Header submissions | Vertex commands | 32-byte `pvr_prim()` input-command count |
|---------|----------------|----------|-------------|
| Clear | 1 (untextured, NONE) | 4 (one four-vertex strip) | 5 |
| Draw points | 0 (cached) | 6 per point | 6 |
| Fill rects | 0 (cached) | 6 per rect | 6 |
| Geometry triangle | 0 (cached) | 3 | 3 |
| Geometry clipped triangle (worst) | 0 (cached) | 15 | 15 |
| Texture quad | 0 (cached) | 6 | 6 |

This last column counts input commands only; it is not the internal TA
vertex-buffer storage size. A submitted header is one 32-byte input command;
the header's internal storage cost has not been measured separately.

---

## 5. Polygon-header cache impact

The single-entry cache records the last submitted header state, keyed on `(texture pointer, blend mode, texture_env, filter)`. Identical consecutive state reuses the cached header. State changes that replace the cached state:

* Texture pointer change (different pointer, or NULL ↔ non-NULL).
* Blend mode change (`NONE` vs `BLEND`).
* Filter change (`NEAREST` vs `BILINEAR`).
* Texture environment change.

Identical consecutive state reuses the cached header. Returning to an earlier state after an intervening state change requires another header submission.

---

## 6. SDL queue versus PVR capacity

`SDL_RenderFlush()` executes the entire queue in one `RunCommandQueue` call. The renderer begins one scene and submits all commands, then finishes the scene.

* No preflight estimate exists.
* No scene splitting is performed.
* If an SDL queue emits enough geometry to exceed the active TA vertex buffer, submission is unsafe. The renderer has no preflight estimate or scene splitting.

---

## 7. Scene splitting feasibility

**Scene splitting is unsupported by the current renderer architecture.**

Reasons:

1. Buffer flipping is not controlled by `pvr_scene_begin/finish` in non-DMA mode.
2. TA capacity becomes available for a later properly synchronized scene through hardware-managed buffer rotation; direct-submission scene calls do not provide a software mechanism for subdividing one SDL frame safely.
3. In direct-submission mode, `pvr_scene_begin()` does not perform a software reset of the TA or OPB pointers.
4. No observable signal for scene completion.
5. Vblank timing is not controlled.

Scene splitting would require significant KOS/PVR changes. It is not recommended.

---

## 8. Capacity observability

KOS does not provide a high-level capacity query or an overflow return from
`pvr_prim()`; in direct mode it writes to the TA input. Test code can read PVR
registers with `PVR_GET`, including the active vertex-buffer start/end/position
and OPB start/end/position. The line probe used the vertex-buffer cursor to
measure stored bytes. The OPB position did not vary in that test and does not
report per-tile bin occupancy, so it cannot rule out OPB pressure.

`pvr_check_ready()` reports the software TA-ready state. In KOS's render path,
the transition to ready occurs after list registration completes and render
submission starts; it does not report render completion. `pvr_wait_render_done()`
waits on a separate state and times out after 100 ms. Repeated waits can extend
the observation window, but a timeout still does not identify its cause.

**Test-only instrumentation (compiles out of ordinary builds):** Count submitted
vertex commands and headers; record relevant register positions and hardware
captures. Do not use synchronization timeouts alone as overflow detectors.

---

## 9. Stress workload design

The workload budgets in the original A–F plan used an incorrect 8,192-unit
capacity model and are superseded. For a stable-header workload at 640x480 VGA,
use the empirical estimate `20 * vertex_commands + 76` bytes against the active
524,288-byte TA vertex buffer. Header churn, other modes, and other geometry
patterns need separate hardware calibration; do not claim a safe percentage for
them from this single-header model. The previously documented A–F budget table
is retained below only as historical workload composition, not as a capacity
recommendation.

### 9.1 Workload A: stable untextured triangles

* **Purpose:** Maximize vertex traffic, minimize header churn.
* **Formula:** `1 + 3N` (1 cached header + 3 vertex commands per triangle).
* **Ordinary final load:** 2,300 triangles.
* **Total:** 6,900 vertex commands and one shared header. Under the measured single-header setup, the estimate is 138,076 bytes (26.3% of 524,288).
* **Ramp:** 256, 512, 1024, 2048, 2300.

### 9.2 Workload B: deliberately header-heavy textured quads

* **Purpose:** Force polygon-header recompilation and submission. State must actually change for every draw.
* **Formula:** `7N` (1 header + 6 vertex commands per quad; no shared header).
* **Ordinary final load:** 990 quads.
* **Total:** 5,940 vertex commands plus 990 header submissions. Header-churn storage is not calibrated by the single-header model.
* **Ramp:** 256, 512, 768, 900, 990.

### 9.3 Workload C: same-texture copies

* **Purpose:** Common sprite workload with header caching.
* **Formula:** `1 + 6N` (1 cached header + 6 vertex commands per copy).
* **Ordinary final load:** 1,150 copies.
* **Total:** 6,900 vertex commands. The estimate is 138,000 bytes for vertex commands, plus uncalibrated header/setup storage.
* **Ramp:** 256, 512, 1024, 1150.

### 9.4 Workload D: stable-state points

* **Purpose:** Renderer-specific expansion and degenerate-triangle representation.
* **Formula:** `1 + 6N` (1 cached header + 6 vertex commands per point).
* **Ordinary final load:** 1,150 points.
* **Total:** 6,900 vertex commands and one shared header. Under the measured single-header setup, the estimate is 138,076 bytes (26.3% of 524,288).
* **Ramp:** 256, 512, 1024, 1150.

### 9.5 Workload E: mixed realistic frame

* **Purpose:** Approximate an actual game frame with varied primitives and state changes.

**Fixed final-stage composition and order:**

| Component | Count | Per-unit cost | Subtotal |
|-----------|-------|---------------|----------|
| Clear (untextured, `NONE`) | 1 | 1 header + 4 vertices | 5 |
| Background textured quad (`NONE`) | 1 | 1 header + 6 vertices | 7 |
| Sprites (same texture/state as background, cached) | 400 | 6 vertices each | 2,400 |
| Translucent overlay quads (unique texture each draw) | 75 | 1 header + 6 vertices each | 525 |
| UI rectangles (untextured, `NONE`) | 25 | 1 shared header + 6 vertices per rect | 151 |
| Points (untextured, `NONE`, cached from UI) | 600 | 6 vertices each | 3,600 |
| **Total** | | | **6,610 vertex commands, 78 headers** |

The renderer has a single-entry header cache. In this exact command order, the clear's untextured `NONE` header is replaced by the textured background/sprite header and then by the overlay headers. The UI rectangles therefore require a new untextured `NONE` header, and the immediately following points reuse that UI header. The background and sprites use the same texture, blend mode, texture environment, and filter. Each overlay uses a distinct texture with `SDL_BLENDMODE_BLEND`, forcing one header per overlay.

For `S` sprites and `O` overlays, the legacy 32-byte input-command count is
`3763 + 6S + 7O`: `O + 3` headers and `3760 + 6S + 6O` vertex commands. This
count is not an estimate of TA-buffer bytes when headers change.

| Stage | Sprites (`S`) | Overlays (`O`) | Header submissions | Vertex commands | Legacy 32-byte command count |
|-------|---------------|----------------|--------------------|-----------------|-------------------------------|
| 1 | 100 | 15 | 18 | 4,450 | 4,468 |
| 2 | 200 | 30 | 33 | 5,140 | 5,173 |
| 3 | 300 | 45 | 48 | 5,830 | 5,878 |
| 4 | 350 | 60 | 63 | 6,220 | 6,283 |
| 5 (ordinary final) | 400 | 75 | 78 | 6,610 | 6,688 |

These mixed-frame header counts have not been converted to TA bytes. The
single-header storage model must not be applied to this table.

### 9.6 Workload F: boundary probe (compile-time opt-in)

* **Purpose:** Characterize the capacity boundary.
* **Formula:** `1 + 3N` (same as Workload A).
* **Capacity limit:** Not specified. The historical 2,730-triangle figure used the superseded 8,192-unit model.
* **Guard:** `DC_PVR_CAPACITY_BOUNDARY_PROBE` (compile-time define).
* **Maximum:** `DC_PVR_CAPACITY_BOUNDARY_MAX`. The historical 2,800-triangle ceiling is conservative relative to the single-header estimate, but has not itself been hardware-qualified as a general safe limit.
* **Ramp near boundary:** Do not use the historical ramp. Recalculate from a hardware-calibrated model before running a destructive boundary probe.
* **Stop condition:** First observable anomaly. Stop immediately.
* **Warnings:**
  * Print a prominent risk warning before starting.
  * Warn that console reset may be required.
  * Do not claim recovery or normal return is possible.
  * Do not claim `pvr_wait_ready()` or `pvr_wait_render_done()` identifies overflow.
* **Exclusion:** Not part of the ordinary test. Requires explicit operator opt-in via compile-time define.

### 9.7 Workload summary

| Workload | Final workload | Vertex commands | Header submissions | Capacity estimate |
|----------|----------------|-----------------|--------------------|-------------------|
| A | 2,300 triangles | 6,900 | 1 | 138,076 bytes under single-header model |
| B | 990 quads | 5,940 | 990 | Not calibrated for header churn |
| C | 1,150 copies | 6,900 | 1 | 138,000 vertex-command bytes; clear/header setup not calibrated |
| D | 1,150 points | 6,900 | 1 | 138,076 bytes under single-header model |
| E | 400 sprites, 75 overlays | 6,610 | 78 | Not calibrated for header churn |
| F | No ordinary run | Not applicable | Not applicable | No safe boundary inferred |

---

## 10. Validation policy

**Required order:**

1. Strict build and static checks (`-Wall -Wextra -Werror`, explicit local archive, linker map).
2. Physical Dreamcast hardware via BBA (`kos-tool -t 192.168.0.128 -x <elf> -m .`).
3. Serial-output review.
4. Capture and directly inspect the actual hardware framebuffer for every visually distinct workload.
5. Flycast only as an optional diagnostic aid.

Hardware is first and authoritative. Flycast is secondary.

### 10.1 Focused physical-hardware result (2026-07-29)

The ordinary A-E test passed on a real Dreamcast at 640x480 VGA. Every stage
reached presentation and its known-good recovery frame with the expected
command accounting. The previously reported totals (6,901 for A, 6,930 for B,
6,901 for C and D, and 6,688 for E) are legacy 32-byte command counts, not
capacity units or percentages. Workload F was absent, the program printed PASS,
and it returned zero. These visual results remain valid; the old capacity
interpretation does not.

Fourteen raw hardware framebuffer captures from a test-only
`DC_PVR_CAPACITY_HARDWARE_SCREENSHOTS` build cover the final A-D stress frames,
one recovery frame per workload, and all five E stages. Workload F remains
excluded. The capture helper waits three vblanks after presentation before
reading the displayed page; this is required by the PVR presentation-buffer
lifecycle. The ordinary build compiles out the capture helper, hardware paths,
and `vid_screen_shot`. Direct inspection shows complete geometry and textures,
stable recovery frames, progressive E load, and correct E translucency without
corruption, missing geometry, freeze, reset, tearing, or failed recovery. The
authoritative serial-log path, capture paths, and SHA-256 values are recorded in
`test/dreamcast/test/pvr_capacity_stress/README.md`.

---

## 11. Failure and recovery policy

* **Submission beyond the active TA capacity is unsafe.** The line probe observed the active vertex-buffer cursor reach its configured end and the scene fail to complete at 27,600 commands. The precise boundary and possible contribution from OPB/tile pressure were not isolated.
* **`pvr_wait_ready()` and `pvr_wait_render_done()` timeouts are not standalone overflow detectors.** A timeout can reflect overflow, a long render, corrupted state, or unrelated stalls; combine it with register observations and captures.
* **Recovery, if possible, may require `pvr_shutdown()` plus `pvr_init()`, but success is not guaranteed.**
* **Do not implement runtime overflow detection based on `pvr_wait_ready()` or `pvr_wait_render_done()` return values.** They produce false positives.
* **Do not generalize one probe's result to other scenes or video modes.** Its storage model was measured with one stable header at 640x480 VGA.

---

## 12. Renderer safeguard options

### Option A: No renderer change (recommendation for this increment)

Document the measured 640x480 VGA single-header estimate (`20 * vertex_commands + 76` bytes) and keep workloads below the configured buffer. Do not treat it as a universal budget for header churn or other modes. No runtime monitoring. No performance cost. No implementation complexity.

### Option B: Conservative command-count guard (deferred)

Estimate generated vertex/header usage and reject an oversized queue before scene submission. Not validated. Requires hardware characterization to confirm that conservative rejection prevents unsafe submission. Moderate implementation complexity.

### Option C: Scene splitting (deferred)

Not feasible with the current non-DMA architecture (Section 7). Not recommended.

### Option D: Adjusted PVR initialization buffers (deferred)

Changing `pvr_params` requires `pvr_shutdown()` plus `pvr_init()`, which may not recover from TA corruption. Not validated. Moderate hardware validation burden.

### Option E: Graceful fatal-state handling (deferred)

Detect overflow via `pvr_wait_ready()` / `pvr_wait_render_done()` return values, mark `pvr_failed`, recover at renderer teardown/recreation. Not recommended because timeouts cannot reliably detect overflow and produce false positives. Deferred until hardware characterization identifies a different reliable signal.

**Recommendation:** Option A for this increment. Defer Options B, C, D, and E.

---

## 13. Scope and implementation plan

### 13.1 Files created in this increment

* `test/dreamcast/test/pvr_capacity_stress/Makefile.dc`
* `test/dreamcast/test/pvr_capacity_stress/README.md`
* `test/dreamcast/test/pvr_capacity_stress/pvr_capacity_stress.c`

No renderer source file is modified in this increment.

The increment changes no PVR initialization, adds no runtime overflow detection, adds no scene splitting or partial scene submission, and does not automatically run Workload F.

### 13.2 Regression impact

No renderer code changes are implemented. Existing tests do not need to be rerun.

### 13.3 Deferred

* Partial texture uploads, DMA, twiddled/VQ textures, mipmaps, strided textures.
* Render targets.
* Non-VGA video-mode implementation.
* Multithreaded PVR submission.
* GLdc/FBO work.
* Renderer safeguards and deliberate boundary probing.

### 13.4 Next steps

Keep boundary probing deferred until the capacity model covers the workload's
header pattern and video mode. Workload F remains optional and must not be run
without explicit operator choice via `DC_PVR_CAPACITY_BOUNDARY_PROBE`.

## 14. Line workload timeout — hardware characterization (2026-07-31)

The geometry bench line workload timed out at high call counts. A bounded,
single-frame-per-count probe (`test/dreamcast/test/pvr_line_capacity_probe/`)
ran on physical 640x480 VGA hardware against both the pinned pre-fast-path
archive and the working-tree fast-path archive.

The archived probe logs predate correction of the printed model constant from
44 to 76 bytes. Their `predicted_usage` field is therefore 32 bytes below the
register-derived measurement; the recorded `vertbuf_bytes_reported` register
deltas are unaffected and are used below. The probe source now prints the
corrected prediction; this reporting fix was not rerun on hardware.

### 14.1 Reproducible threshold

Segment `(32,100) -> (606,200)`, 575 Bresenham pixels per call, parallel copies
offset by 24 pixels, one distinct color per call so dropped geometry is
measurable per call.

| Calls | Vertex commands submitted | Modeled bytes requested | Vertex-buffer high-water | Render | Geometry |
|---:|---:|---:|---:|---|---|
| 1 | 3,450 | 69,076 | 69,076 / 524,288 | completes ~12 ms | complete |
| 2 | 6,900 | 138,076 | 138,076 / 524,288 | completes ~13 ms | complete |
| 4 | 13,800 | 276,076 | 276,076 / 524,288 | completes ~8-15 ms | complete |
| 6 | 20,700 | 414,076 | 414,076 / 524,288 | completes ~15 ms | complete |
| 7 | 24,150 | 483,076 (92.1%) | 483,076 / 524,288 | completes ~13-16 ms | complete |
| 8 | 27,600 | 552,076 | **524,288 / 524,288 (register reaches end)** | **never completes** | no completed frame |

Seven calls is the last success and eight calls the first failure in this sweep.
At eight the vertex-buffer high-water mark equals its end exactly, the render
does not complete during ten 100 ms waits, the recovery frame fails, and PVR
shutdown times out on the Tile Accelerator. The failure was not recoverable
within this run.

### 14.2 Failure phase

The failure occurs after submission: no SDL API error occurs during draw calls.
`pvr_check_ready()` is `-1` before present and `0` after the failed wait. KOS
clears its TA-busy state after list registration completes and render submission
starts; the separate render-done wait still times out. Successful counts took
8-16 ms, while the failed count remained incomplete for about 1.016 seconds.
That makes ordinary render cost unlikely, but does not prove the failed scene
could not take longer.

### 14.3 Cause

The vertex-buffer cursor reaches its configured end at the first failing count.
Successful counts fit the empirical model `20 bytes per vertex command + 76
fixed bytes`. Extrapolating gives an estimated limit of 26,210 commands (about
4,368 six-vertex quads, or 7.6 calls at 575 pixels per call). The sweep did not
test that exact threshold; it brackets failure between 24,150 and 27,600
commands. At eight calls the model requests 552,076 bytes, 105.3% of the
configured buffer.

The probe does not rule out OPB/tile pressure as a simultaneous or alternative
cause. OPB storage is partitioned per tile, and aggregate triangle count does
not establish per-tile usage. The reported `PVR_TA_OPB_POS` was constant and did
not provide per-tile occupancy. The register evidence directly establishes that
the vertex-buffer cursor reached its end, but not that it was the only limit
reached.

### 14.4 Baseline versus fast path

At successful counts 1-7, per-call pixel counts match and captures are
byte-identical between the two archives. At failing count 8, captures are also
byte-identical, but both show the previous completed front buffer; they do not
compare geometry from the incomplete scene. The host differential harness
provides the emitted-vertex equivalence evidence. The measured difference is CPU
timing: the fast path submits during draw calls (~21.7 ms for eight calls),
while the baseline queues in ~1.4 ms and submits inside `SDL_RenderPresent`
(~103 ms). The baseline and fast-path runs have the same observed success/failure
boundary.

### 14.5 Decision

No fast-path-specific backend defect is reproduced: the baseline and fast-path
archives have the same success/failure boundary. The original 3,000-call bench
workload is far beyond the measured scene limit. SDL clips each line to the
640x480 viewport before Bresenham rasterization; applying that clipping to the
benchmark endpoints gives about 1,529,154 points, or 9,174,924 vertex commands
(about 350x the estimated 26,210-command limit). The current benchmark is
bounded to two line calls per frame.

Any future capacity guard would need a software high-water estimate against the
configured buffer size. The current model covers vertex commands with one stable
header only; header churn needs calibration. KOS direct-submit `pvr_prim()` has
no capacity return. A guard remains deferred.

---

**End of research.** This document records KOS capacity behavior and an
empirical 640x480 VGA single-header vertex-buffer model. The original A–F
workload percentages are historical and are not capacity guarantees. No
renderer capacity guard or scene splitting is currently implemented.
