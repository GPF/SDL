# Dreamcast Native PVR Renderer: TA/OPB Capacity and Command-Load Stress Research

**Status:** Research and architecture design complete.
**Date:** 2026-07-28
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
| `vertex_buf_size` | 512 KiB total, 256 KiB per frame | 8,192 total 32-byte units per scene (vertices + headers). Double-buffered; one frame submits, one renders. |
| `dma_enabled` | 0 | Direct submission via store queues. `pvr_prim()` writes directly to the TA input. |
| `autosort_disabled` | 1 | Translucent primitives processed in submission order. Preserves SDL painter order. |
| `opb_overflow_count` | 3 | Three extra OPB partitions beyond the base. PVR hardware handles overflow internally; KOS exposes no observable signal. |
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

**8,192 total 32-byte units per scene**, including polygon headers and vertices. This is the scene-wide capacity limit.

* **Fixed in hardware.** Selected during `pvr_init` via `vertex_buf_size`.
* **Available for each synchronized scene.** TA capacity becomes available for a later properly synchronized scene through hardware-managed buffer rotation. In direct-submission mode, `pvr_scene_begin()` does not perform a software reset of the TA or OPB pointers.
* **Double buffering does not protect against excessive submission within one scene.** It provides hardware-managed buffer rotation between properly synchronized scenes. Submission beyond the active TA capacity is unsafe. The observable hardware consequence is not established by the inspected KOS source and may include corruption, stalling, missing geometry, or failure to complete the scene. Hardware characterization is required.
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
* Each submitted header consumes one 32-byte TA command-buffer unit.
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
* **Critical finding:** `pvr_prim()` does not provide a useful capacity-exceeded result. It writes directly to the TA input. Submission beyond the active TA capacity is unsafe. The observable hardware consequence is not established by the inspected KOS source and may include corruption, stalling, missing geometry, or failure to complete the scene. Hardware characterization is required.

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

If more commands than 8,192 total 32-byte units are submitted before `pvr_scene_finish()`, `pvr_prim()` provides no capacity-exceeded result. Submission beyond the active TA capacity is unsafe. The observable hardware consequence is not established by the inspected KOS source and may include corruption, stalling, missing geometry, or failure to complete the scene. Hardware characterization is required. A later synchronization timeout would remain ambiguous.

**Recovery, if possible, may require `pvr_shutdown()` plus `pvr_init()`, but success is not guaranteed.**

### 3.7 Flycast versus hardware

Flycast may handle overflow differently than hardware (e.g., silently dropping primitives). Physical hardware enforces double-buffer synchronization strictly. Hardware is primary. Flycast is optional diagnostic aid.

---

## 4. Current renderer command expansion

| Command | Header (cached) | Vertices | Total units |
|---------|----------------|----------|-------------|
| Clear | 1 (untextured, NONE) | 4 (one four-vertex strip) | 5 |
| Draw points | 0 (cached) | 6 per point | 6 |
| Fill rects | 0 (cached) | 6 per rect | 6 |
| Geometry triangle | 0 (cached) | 3 | 3 |
| Geometry clipped triangle (worst) | 0 (cached) | 15 | 15 |
| Texture quad | 0 (cached) | 6 | 6 |

With cached headers, only vertices consume units. Uncached headers add 1 unit each.

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
* If the SDL queue produces more than 8,192 32-byte units, submission exceeds the active TA capacity and is unsafe; the observable result requires hardware characterization.

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

KOS does not expose TA or OPB usage counters, vertex-buffer pointers, interrupt/overflow flags, or hardware registers in ordinary builds. `pvr_prim()` returns -1 on error (no list open, unaligned) but not on overflow.

`PVR_KM_DBG` exposes memory allocation statistics in debug builds but does not expose TA/OPB usage.

**Test-only instrumentation (compiles out of ordinary builds):** Count submitted vertex commands and headers; log counts to serial output. Do not rely on `pvr_wait_ready()` or `pvr_wait_render_done()` return values as reliable overflow signals.

---

## 9. Stress workload design

All workloads use the canonical same-scene budget of 8,192 total 32-byte units (vertices + headers combined). Ordinary final loads target approximately 75–85% of the budget. Workload F is compile-time opt-in only.

### 9.1 Workload A: stable untextured triangles

* **Purpose:** Maximize vertex traffic, minimize header churn.
* **Formula:** `1 + 3N` (1 cached header + 3 vertex commands per triangle).
* **Ordinary final load:** 2,300 triangles.
* **Total:** `1 + 3 × 2300 = 6901` units (84.2%).
* **Ramp:** 256, 512, 1024, 2048, 2300.

### 9.2 Workload B: deliberately header-heavy textured quads

* **Purpose:** Force polygon-header recompilation and submission. State must actually change for every draw.
* **Formula:** `7N` (1 header + 6 vertex commands per quad; no shared header).
* **Ordinary final load:** 990 quads.
* **Total:** `7 × 990 = 6930` units (84.6%).
* **Ramp:** 256, 512, 768, 900, 990.

### 9.3 Workload C: same-texture copies

* **Purpose:** Common sprite workload with header caching.
* **Formula:** `1 + 6N` (1 cached header + 6 vertex commands per copy).
* **Ordinary final load:** 1,150 copies.
* **Total:** `1 + 6 × 1150 = 6901` units (84.2%).
* **Ramp:** 256, 512, 1024, 1150.

### 9.4 Workload D: stable-state points

* **Purpose:** Renderer-specific expansion and degenerate-triangle representation.
* **Formula:** `1 + 6N` (1 cached header + 6 vertex commands per point).
* **Ordinary final load:** 1,150 points.
* **Total:** `1 + 6 × 1150 = 6901` units (84.2%).
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
| **Total** | | | **6,688 units (81.6%)** |

The renderer has a single-entry header cache. In this exact command order, the clear's untextured `NONE` header is replaced by the textured background/sprite header and then by the overlay headers. The UI rectangles therefore require a new untextured `NONE` header, and the immediately following points reuse that UI header. The background and sprites use the same texture, blend mode, texture environment, and filter. Each overlay uses a distinct texture with `SDL_BLENDMODE_BLEND`, forcing one header per overlay.

For `S` sprites and `O` overlays, the exact load is `3763 + 6S + 7O`: `O + 3` headers and `3760 + 6S + 6O` vertices.

| Stage | Sprites (`S`) | Overlays (`O`) | Header units | Vertex units | Total units | Percentage of 8,192 |
|-------|---------------|----------------|--------------|--------------|-------------|---------------------|
| 1 | 100 | 15 | 18 | 4,450 | 4,468 | 54.5% |
| 2 | 200 | 30 | 33 | 5,140 | 5,173 | 63.1% |
| 3 | 300 | 45 | 48 | 5,830 | 5,878 | 71.8% |
| 4 | 350 | 60 | 63 | 6,220 | 6,283 | 76.7% |
| 5 (ordinary final) | 400 | 75 | 78 | 6,610 | 6,688 | 81.6% |

### 9.6 Workload F: boundary probe (compile-time opt-in)

* **Purpose:** Characterize the capacity boundary.
* **Formula:** `1 + 3N` (same as Workload A).
* **Theoretical maximum without exceeding 8,192 units:** 2,730 triangles (`1 + 3 × 2730 = 8191` units).
* **Guard:** `DC_PVR_CAPACITY_BOUNDARY_PROBE` (compile-time define).
* **Maximum:** `DC_PVR_CAPACITY_BOUNDARY_MAX`. Default must not exceed 2,800 triangles.
* **Ramp near boundary:** 2,680, 2,700, 2,720, 2,730, 2,740, 2,750, 2,775, 2,800.
* **Stop condition:** First observable anomaly. Stop immediately.
* **Warnings:**
  * Print a prominent risk warning before starting.
  * Warn that console reset may be required.
  * Do not claim recovery or normal return is possible.
  * Do not claim `pvr_wait_ready()` or `pvr_wait_render_done()` identifies overflow.
* **Exclusion:** Not part of the ordinary test. Requires explicit operator opt-in via compile-time define.

### 9.7 Workload summary

| Workload | Formula | Ordinary final workload | Final units | Percentage of 8,192 |
|----------|---------|-------------------------|-------------|---------------------|
| A | `1 + 3N` | 2,300 triangles | 6,901 | 84.2% |
| B | `7N` | 990 quads | 6,930 | 84.6% |
| C | `1 + 6N` | 1,150 copies | 6,901 | 84.2% |
| D | `1 + 6N` | 1,150 points | 6,901 | 84.2% |
| E | `3763 + 6S + 7O` | 400 sprites, 75 overlays | 6,688 | 81.6% |
| F | `1 + 3N` | No ordinary run; theoretical in-budget maximum is 2,730 triangles | 8,191 | 99.99% |

---

## 10. Validation policy

**Required order:**

1. Strict build and static checks (`-Wall -Wextra -Werror`, explicit local archive, linker map).
2. Physical Dreamcast hardware via BBA (`kos-tool -t 192.168.0.128 -x <elf> -m .`).
3. Serial-output review.
4. Hardware screenshot only when visual confirmation is needed.
5. Flycast only as an optional diagnostic aid.

Hardware is first and authoritative. Flycast is secondary.

---

## 11. Failure and recovery policy

* **Submission beyond the active TA capacity is unsafe.** The observable hardware consequence is not established by the inspected KOS source and may include corruption, stalling, missing geometry, or failure to complete the scene. Hardware characterization is required.
* **`pvr_wait_ready()` and `pvr_wait_render_done()` timeouts are ambiguous and are not reliable overflow detectors.** They cannot distinguish overflow from ordinary long renders, corrupted state, or unrelated stalls.
* **Recovery, if possible, may require `pvr_shutdown()` plus `pvr_init()`, but success is not guaranteed.**
* **Do not implement runtime overflow detection based on `pvr_wait_ready()` or `pvr_wait_render_done()` return values.** They produce false positives.
* **Do not claim a specific overflow result.** Hardware characterization is required.

---

## 12. Renderer safeguard options

### Option A: No renderer change (recommendation for this increment)

Document practical capacity limits (8,192 units per scene). Rely on applications to stay below them. No runtime monitoring. No performance cost. No implementation complexity.

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

Implement safe below-boundary stress characterization now; defer renderer safeguards and deliberate boundary probing. Workload F remains optional and must not be run without explicit operator choice via `DC_PVR_CAPACITY_BOUNDARY_PROBE`.

---

**End of research.** This document establishes the TA capacity limit (8,192 32-byte units per scene), documents KOS overflow behavior, defines canonical stress workloads (A–F), and recommends Option A (no renderer change) as the current approach. The stress test (Workloads A–E) validates behavior below capacity limits. Workload F requires explicit operator opt-in.
