# Dreamcast Native PVR Renderer: List Routing Research

**Status:** Research complete. Implementation added (2026-07-30) behind `DC_PVR_OPAQUE_PREFIX_ROUTING`.
**Date:** 2026-07-30
**Scope:** SDL painter-order contract, PVR list execution order, opaque/punch-through routing policies, alpha-test eligibility, state/lifecycle implications, exact safe eligibility predicates, and a focused first-increment recommendation.

This document answers the question: can the native PVR renderer safely route a narrow subset of SDL draws from its current single transparent-list path into PVR opaque and/or punch-through lists without violating SDL command ordering?

**Related design:** `AGENTS.md` records the overall renderer architecture, lifecycle, and validated phases. `PHASE0_SKETCH.md` records the Phase 0 contract. `PVR_CLIPPING_RESEARCH.md` records the CPU clipping contract. `PVR_TEXTURE_RESEARCH.md` records the texture subsystem contract. `PVR_STRIDED_TEXTURE_RESEARCH.md` and `PVR_STRIDED_TEXTURE_INTEGRATION_RESEARCH.md` record strided texture findings. `PVR_DMA_UPLOAD_RESEARCH.md` records DMA findings. `PVR_PARTIAL_UPLOAD_RESEARCH.md` records partial-upload findings. `PVR_RENDER_TARGET_RESEARCH.md` and `PVR_RENDER_TARGET_PRESERVATION_RESEARCH.md` record RTT findings. `PVR_ORDERED_RENDER_TARGET_RESEARCH.md` records ordered RTT findings. `PVR_CAPACITY_RESEARCH.md` records TA/OPB capacity findings. `PVR_POINTS_LINES_RESEARCH.md` records points/lines findings. `PVR_DT_TEXTURE_RESEARCH.md` records .dt loader findings. `PVR_BACKEND_FALLBACK_RESEARCH.md` records cross-backend fallback findings. `PVR_BLENDMODE_ADD_MOD_MUL_RESEARCH.md` records ADD/MUL blend findings.

Evidence labels used below:

- **Source-verified** means observed directly in the current SDL2, native PVR, or KOS source.
- **Hardware-verified** means already demonstrated on physical Dreamcast and recorded in the existing phase evidence.
- **Design conclusion** is an inference or recommendation based on that evidence, not current behavior.

---

## 1. Executive verdict

**A narrow opaque-prefix route can preserve SDL order only when every drawable
command before the first transparent-list submission is itself emitted through
the opaque list.**

**Routing `SDL_BLENDMODE_NONE` geometry into the punch-through list is not safe for arbitrary SDL ordering** because punch-through uses alpha testing (discard below threshold, opaque write above) rather than alpha blending, and its depth-write behavior and binary alpha threshold are not equivalent to SDL's `BLENDMODE_NONE` semantics.

**Routing `SDL_BLENDMODE_BLEND` geometry into the translucent list already happens** — the renderer currently uses `PVR_LIST_TR_POLY` exclusively. The only remaining question is whether **any** subset of opaque SDL draws can be safely routed to the opaque list within an otherwise translucent frame.

**The narrowest safe first increment** is an opaque prefix:

> Route a leading full-target clear and subsequent untextured
> `SDL_BLENDMODE_NONE` drawable geometry into `PVR_LIST_OP_POLY`. The prefix
> closes permanently at the first drawable that cannot use that list
> (including a textured `NONE` draw, `BLEND`, `ADD`, `MUL`, or any clear the
> implementation does not explicitly route opaque). After it closes, every
> remaining drawable is emitted through `PVR_LIST_TR_POLY`.

This preserves SDL painter order because PVR hardware processes the opaque list before the translucent list. Within each list, autosort is disabled, so submission order is preserved.

**Verdict: conditional GO for a guarded opaque-prefix feasibility prototype;
NO-GO for punch-through routing, arbitrary interleaved routing, trailing
opaque runs, or any routing that depends on depth-state tricks.**

---

## 2. Current renderer and PVR list behavior

### 2.1 Current list usage

**Source-verified:** The renderer uses exactly one list per scene:

```c
pvr_list_begin(PVR_LIST_TR_POLY) → ... → pvr_list_finish()
```

All SDL draws — including `SDL_BLENDMODE_NONE` — are submitted to `PVR_LIST_TR_POLY` with autosort disabled.

### 2.2 PVR initialization

**Source-verified:** The renderer's `pvr_params` (from `SDL_render_dreamcast_pvr.c`):

```c
static const pvr_init_params_t pvr_params = {
    { PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
    512 * 1024,   /* vertex_buf_size */
    0,            /* dma_enabled */
    0,            /* fsaa_enabled */
    1,            /* autosort_disabled */
    3,            /* opb_overflow_count */
    0             /* vbuf_doublebuf_disabled */
};
```

Key fields:

| Field | Value | Effect |
|-------|-------|--------|
| `opb_sizes[2]` | `PVR_BINSIZE_16` | Only `PVR_LIST_TR_POLY` is enabled (16-word / 64-byte bin size). All other lists are disabled. |
| `autosort_disabled` | 1 | Translucent primitives processed in submission order. |
| `opb_overflow_count` | 3 | Extra OPB partitions beyond the base. |
| `vertex_buf_size` | 512 KiB, double-buffered | 8192 total 32-byte units per scene. |

**Implication for routing:** Enabling `PVR_LIST_OP_POLY` requires a non-zero
`opb_sizes[0]` at `pvr_init()`. This is a renderer-lifetime configuration
change, but it reserves OPB resources and can reduce capacity available to the
existing translucent workload. It therefore requires a measured capacity
comparison; it is not a free per-frame optimization.

### 2.3 Scene and list lifecycle

**Source-verified:** One scene per presented frame. The renderer:

1. Calls `pvr_scene_begin()` (or `pvr_scene_begin_rtt()` for RTT).
2. Calls `pvr_list_begin(PVR_LIST_TR_POLY)`.
3. Walks SDL's command chain, emitting headers and vertices for each command.
4. Calls `pvr_list_finish()`.
5. Calls `pvr_scene_finish()`.
6. Calls `pvr_wait_render_done()`.

Queue callbacks remain CPU-only. Only `RunCommandQueue` begins the scene/list and submits PVR commands.

### 2.4 Current header emission

**Source-verified:** `DC_PVR_EmitHeader` emits a polygon header when texture or blend state changes. The header cache identity includes:

- texture pointer
- blend mode
- texture environment mode
- filter mode
- stride pixel width

For untextured geometry, the header is emitted once per distinct blend mode (NONE or BLEND).

### 2.5 Current blend-mode mapping

**Source-verified:**

| SDL blend mode | PVR src blend | PVR dst blend | List |
|---|---|---|---|
| `NONE` | `ONE` | `ZERO` | `TR_POLY` |
| `BLEND` | `SRCALPHA` | `INVSRCALPHA` | `TR_POLY` |
| `ADD` | `SRCALPHA` | `ONE` | `TR_POLY` |
| `MUL` | `DESTCOLOR` | `INVSRCALPHA` | `TR_POLY` |

All four are currently routed into `PVR_LIST_TR_POLY`.

---

## 3. SDL painter-order contract

### 3.1 SDL's ordering guarantee

SDL's renderer guarantees that all drawing commands within a frame are executed in **submission order**. This is fundamental to SDL's painter-model rendering. The application expects:

- Draw A (opaque) submitted first → appears below Draw B (translucent) submitted second.
- Draw A (translucent) submitted first → appears below Draw B (opaque) submitted second.
- Draw order is preserved regardless of blend mode, texture, or source.

### 3.2 Current compliance

**Source-verified:** The renderer preserves SDL painter order by:

1. Using a single `PVR_LIST_TR_POLY` with autosort disabled.
2. Within that list, primitives are processed in submission order.
3. All commands, regardless of blend mode, are submitted to the same list.

### 3.3 Routing challenge

When geometry is split across multiple PVR lists, **SDL painter order must be preserved by the order in which lists are processed by the PVR hardware, not by submission order within a single list.** This is the fundamental constraint.

---

## 4. KOS/PVR list-order evidence

### 4.1 PVR hardware list execution order

**Source-verified (KOS + PowerVR hardware documentation):**

The PowerVR Titan architecture processes rendering lists in a **fixed hardware order**:

```
PVR_LIST_OP_POLY (opaque) → PVR_LIST_PT_POLY (punch-through) → PVR_LIST_TR_POLY (translucent)
```

This order is **hardcoded in the TA (Tile Accelerator) firmware**. It is not configurable. It is the same regardless of the order in which lists are submitted to the TA via `pvr_list_begin()`/`pvr_list_finish()`.

**Source-verified (KOS `pvr_scene.c`):**

The KOS `pvr_scene_finish()` function iterates through all enabled lists and fills any unsubmitted list with a blank polygon header. This confirms that the PVR processes all enabled lists per frame, and the TA firmware determines execution order, not KOS.

**Source-verified (KOS `pvr_init_shutdown.c`):**

The `pvr_init_params_t.opb_sizes[]` array controls which lists are enabled and their bin sizes. `PVR_BINSIZE_16` at index 2 enables only `PVR_LIST_TR_POLY`. To enable opaque and punch-through lists, `opb_sizes[0]` and `opb_sizes[4]` must be set to a non-zero bin size.

### 4.2 KOS example evidence

**Source-verified (KOS `texture_render.c` example):**

The KOS `texture_render` example demonstrates the standard PVR ordering pattern:

```c
// First frame: opaque only
pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
pvr_poly_compile(&poly, &cxt);
pvr_scene_begin();
pvr_list_begin(PVR_LIST_OP_POLY);
pvr_prim(&poly, sizeof(poly));
draw_all_opaque_polys();
pvr_list_finish();
pvr_scene_finish();

// Second frame: RTT then screen
draw_frame(); // opaque pass to RTT
pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, ...);
pvr_poly_compile(&hdr, &cxt);
pvr_scene_begin();
pvr_list_begin(PVR_LIST_OP_POLY);
pvr_prim(&hdr, sizeof(hdr));
draw_all_textured_polys();
pvr_list_finish();
pvr_scene_finish();
```

This confirms: opaque lists are rendered first, then translucent. The example does not mix opaque and translucent geometry within a single scene.

### 4.3 Opaque vs. translucent list semantics

| Property | `OP_POLY` | `PT_POLY` | `TR_POLY` |
|---|---|---|---|
| Alpha blending | Configured by polygon context | Alpha test (discard below threshold) | Configured by polygon context |
| Depth write | Configured by polygon context | Configured by polygon context | Configured by polygon context |
| Depth test | Configured by polygon context | Configured by polygon context | Configured by polygon context |
| Depth clear | Depends on the chosen context | Depends on the chosen context | Depends on the chosen context |
| Blend factors | Not used by the proposed prefix | N/A | Configurable per header |
| texel alpha used | Out of scope: no textured OP draws | Yes (threshold test) | Depends on texture context |

### 4.4 Depth state implications

**Source-verified (current renderer):** Every current header, including
`BLENDMODE_NONE`, explicitly uses `PVR_DEPTHCMP_ALWAYS` and
`PVR_DEPTHWRITE_DISABLE`. The first opaque-list increment must retain those
settings rather than rely on a list default, so changing list routing alone
does not introduce a depth semantic change. Any later depth-write optimization
is a separate research and validation task.

---

## 5. Evaluated routing policies

### 5.1 Policy A: Leading opaque run (recommended)

Route an opaque prefix into `PVR_LIST_OP_POLY` only before the first drawable
that must use `PVR_LIST_TR_POLY`.

**Mechanism:**

- `RunCommandQueue` must stop eagerly opening `PVR_LIST_TR_POLY`; it opens the
  first required list lazily when it reaches a drawable command.
- It tracks `opaque_prefix_open`, initially true. State-only commands do not
  change it.
- A leading clear explicitly supported by the prototype, and untextured
  `SDL_BLENDMODE_NONE` geometry, use `PVR_LIST_OP_POLY` while that prefix is
  open.
- The first drawable that is not opaque-prefix eligible closes the prefix,
  finishes the opaque list if active, begins `PVR_LIST_TR_POLY`, and is
  submitted there. The prefix never reopens until the completed screen frame
  is presented.

**SDL painter-order compliance:**

- The opaque prefix is submitted first and rendered first → correct.
- Every later drawable is submitted and rendered in the translucent list in
  SDL submission order → correct.
- A textured `NONE` command is a boundary even though its blend mode is
  opaque: placing it in TR and then placing a later untextured draw in OP
  would invert their SDL order.

**Depth-state handling:** Both lists use the renderer's existing
`PVR_DEPTHCMP_ALWAYS` / `PVR_DEPTHWRITE_DISABLE` context settings. The
prototype changes list execution order, not depth semantics.

**Eligibility predicate:**

```c
// Within RunCommandQueue, after classifying the command:
if (opaque_prefix_open && is_supported_opaque_prefix_command) {
    // Submit to opaque list
} else {
    // Submit to translucent list
}
```

**Verdict: SAFE for the stated constraints. Narrow but useful.**

### 5.2 Policy B: Trailing opaque run

Route `SDL_BLENDMODE_NONE` geometry into `PVR_LIST_OP_POLY` **only when it appears as a contiguous run immediately before `SDL_RenderPresent`, after all translucent draws.**

**Problem:** PVR hardware processes the opaque list **before** the translucent list. If opaque geometry appears **after** translucent geometry in SDL submission order, it will still be rendered **before** the translucent geometry by the PVR — **reversing SDL painter order.**

**Verdict: UNSAFE. Cannot be used without a second scene pass.**

### 5.3 Policy C: Arbitrary interleaved routing

Route each draw to its appropriate list based on blend mode, regardless of SDL submission order.

**Problem:** If SDL submission order is: translucent A → opaque B → translucent C, the PVR will render: opaque B → translucent A → translucent C. This **reverses the order of A and B**, violating SDL painter order.

**Verdict: UNSAFE without per-frame scene splitting.**

### 5.4 Policy D: Per-frame scene splitting

Split the frame into two scenes:

1. First scene: opaque list for leading opaque run.
2. Second scene: translucent list for all remaining geometry.

**Problem:** Two scene passes double the TA overhead. Two `pvr_scene_begin()`/`pvr_scene_finish()` pairs per frame. Requires finishing the first scene (which commits to the framebuffer/RTT) before starting the second. This is the **ordered RTT** pattern from `PVR_ORDERED_RENDER_TARGET_RESEARCH.md` and introduces the same logical-screen preservation cost if the screen is the final destination.

**Verdict: Technically correct but expensive. Not appropriate for a first increment.**

### 5.5 Policy E: Punch-through routing

Route geometry with alpha-modulated vertex colors below a threshold into `PVR_LIST_PT_POLY`.

**Problem:** Punch-through uses **binary alpha testing** (alpha ≥ threshold = render opaque; alpha < threshold = discard). This is fundamentally different from SDL's alpha blending:

- Punch-through does not produce alpha-blended composites.
- Punch-through depth-writes for every surviving pixel (no partial alpha).
- Punch-through alpha threshold is a single configurable value, not per-pixel.
- Punch-through is designed for sprite alpha edges, not general alpha blending.

**Verdict: NOT EQUIVALENT to SDL `BLENDMODE_NONE` or `BLEND`. Do not route BLEND geometry to punch-through.**

### 5.6 Policy F: Z/depth tricks

Use depth-test-only opaque geometry with `BLENDMODE_NONE` semantics by setting a fixed depth and using depth comparison to control visibility.

**Problem:** This requires modifying vertex Z values, which conflicts with SDL's coordinate system. It also requires depth-buffer clearing between opaque and translucent passes. The depth state management becomes complex and error-prone.

**Verdict: UNSAFE for a first increment. Too many failure modes.**

### 5.7 Policy G: No routing optimization

Keep all geometry in the translucent list. This is the current behavior.

**Verdict: SAFE but no performance benefit.**

### 5.8 Policy comparison

| Policy | Correctness | Complexity | Performance | Verdict |
|---|---|---|---|---|
| A: Leading opaque run | Correct under constraint | Low | Opaque draw faster | **Recommended first increment** |
| B: Trailing opaque run | Incorrect (order reversal) | Low | — | **Reject** |
| C: Arbitrary interleaved | Incorrect (order reversal) | Medium | — | **Reject** |
| D: Per-frame scene split | Correct | High | Double scene overhead | **Deferred** |
| E: Punch-through | Not equivalent | Medium | — | **Reject** |
| F: Depth tricks | Error-prone | High | — | **Reject** |
| G: No routing | Correct | None | Baseline | **Keep as fallback** |

---

## 6. Exact safe eligibility predicates

### 6.1 Leading-run opaque eligibility

The following conditions must **all** be true for a draw to be routed to `PVR_LIST_OP_POLY`:

1. **The opaque prefix is open.** Any preceding drawable sent to TR closes it,
   whatever its blend mode. This includes textured `NONE`, `BLEND`, `ADD`,
   `MUL`, and unsupported clear variants.

2. **The command is either a specifically supported leading clear or
   untextured geometry with `SDL_BLENDMODE_NONE`.** All textured geometry
   stays in TR for the first increment.

3. **The opaque list is enabled in PVR initialization.** Currently
   `opb_sizes[0] = PVR_BINSIZE_0`; the guarded build changes it to a measured
   non-zero size.

4. **The scene is the physical screen.** Keep ordered RTT target scenes out
   of this first prototype to avoid expanding its validation surface. This is
   a scope restriction, not a claimed depth hazard.

**Predicate (pseudocode):**

```c
static SDL_bool CanRouteToOpaqueList(SDL_BlendMode blend,
                                      SDL_Texture *texture,
                                      SDL_bool opaque_prefix_open,
                                      SDL_bool is_supported_clear)
{
    if (!opaque_prefix_open) return SDL_FALSE;
    if (is_supported_clear) return SDL_TRUE;
    if (blend != SDL_BLENDMODE_NONE || texture != NULL) return SDL_FALSE;
#if defined(DC_PVR_RENDER_TARGETS_ENABLED)
    if (current_target != NULL) return SDL_FALSE;
#endif
    return SDL_TRUE;
}
```

### 6.2 Punch-through eligibility (for future, not first increment)

Punch-through eligibility requires:

1. **Vertex alpha below threshold** → not routable to punch-through (would be discarded).
2. **Vertex alpha above threshold** → routable to punch-through (would render opaque).
3. **Alpha threshold must be set** — the PVR alpha test threshold is a per-scene setting, not per-header. This means all punch-through geometry in a scene shares one threshold.

**Conclusion:** Punch-through is only viable for a uniform-alpha-threshold workload (e.g., all sprites with the same alpha edge). It is not a general-purpose substitute for `BLENDMODE_NONE`.

### 6.3 State-lifetime of routing eligibility

The routing eligibility state must be managed per-frame:

| State | Reset point |
|---|---|
| `opaque_prefix_open` | successful screen `RenderPresent` |
| `opaque_list_active` | When first opaque draw is submitted |
| `transparent_list_active` | When the opaque prefix closes or the first drawable is non-eligible |

### 6.4 Opaque list lifecycle within a frame

When the first eligible opaque draw is encountered:

1. If no opaque scene/list is active:
   - Call `pvr_list_begin(PVR_LIST_OP_POLY)`.
   - Mark `opaque_list_active = SDL_TRUE`.
2. Submit the opaque header and vertices to the active opaque list.
3. When the opaque prefix closes:
   - Call `pvr_list_finish()` for the opaque list.
   - Mark `opaque_list_flushed = SDL_TRUE`.
   - Switch subsequent opaque-eligible geometry to the translucent list.

---

## 7. State/lifecycle implications

### 7.1 Header-cache identity

**Current identity:** texture pointer, blend mode, texture environment, filter mode, stride.

**Impact of opaque routing:** The opaque list uses the same header structure (`pvr_poly_hdr_t`) and header compilation (`pvr_poly_compile`). The header cache must be aware of which list is active:

- A header valid for the translucent list is **not** automatically valid for the opaque list, because the `list_type` field in `pvr_poly_hdr_cmd` differs.
- The header cache must include the active list type in its identity, or the cache must be invalidated when switching between lists.

**Recommendation:** Include the active list type in the header cache identity. If the active list changes, invalidate the cache:

```c
// In DC_PVR_EmitHeader:
if (data->header_valid && data->header_list_type == active_list_type && ...) {
    return 0; // Cache hit
}
```

### 7.2 Scene/list lifecycle

**Current:** One scene, one list (`PVR_LIST_TR_POLY`) per frame.

**With opaque routing:** One scene, **two** lists per frame when opaque draws are present:

```
pvr_scene_begin()
  pvr_list_begin(PVR_LIST_OP_POLY)    ← first opaque draw triggers this
  ... opaque geometry ...
  pvr_list_finish()                    ← opaque prefix closes
  pvr_list_begin(PVR_LIST_TR_POLY)    ← first non-eligible drawable
  ... remaining geometry ...
  pvr_list_finish()
pvr_scene_finish()
```

**Impact:** The scene begin/finish calls are unchanged, but the existing eager
transparent-list start must become lazy. List begin/finish calls are chosen by
the queue walker; no scene split is required for the opaque-prefix policy.

### 7.3 Render-target target scenes

**Source-verified (current renderer):** When `DC_PVR_RENDER_TARGETS_ENABLED` is active, the renderer may begin a target scene via `pvr_scene_begin_rtt()`. The target's VRAM is the render destination.

**Recommendation:** Disable opaque routing for target scenes in the first
prototype. This deliberately avoids testing list transitions alongside target
preservation and ordered-RTT scene rules; it is not needed for screen-path
feasibility.

### 7.4 Strided texture state

**Source-verified (current renderer):** The frame stride is programmed once per frame when the first strided texture is encountered. It remains stable through the presented frame and is reset after `SDL_RenderPresent`.

**Impact of opaque routing:** Textured draws, including strided textures, are
outside the opaque prefix and therefore close it or remain in TR. Existing
frame-global stride programming is unchanged.

### 7.5 Partial/DMA texture updates

**Source-verified (current renderer):** Texture updates use either blocking DMA or synchronous store-queue upload. These are independent of the rendering list.

**Impact of opaque routing:** None. Texture upload happens before the frame's rendering begins (in `UpdateTexture`/`UnlockTexture`). The list routing decision is made during `RunCommandQueue`, after all pending texture uploads have completed.

### 7.6 Deferred VRAM cleanup

**Source-verified (current renderer):** During `RenderPresent`, deferred VRAM is released and texture references are cleared.

**Impact of opaque routing:** None. The opaque list does not change the deferred-free mechanism.

---

## 8. Edge-case matrix

### 8.1 Correct routing cases

| SDL command sequence | Opaque list | Translucent list | SDL painter order preserved? |
|---|---|---|---|
| NONE → NONE → BLEND | Both NONEs | BLEND | Yes (opaque renders first) |
| BLEND → NONE → BLEND | — | All three | Yes (all in TR) |
| NONE → BLEND → NONE | First NONE | BLEND + second NONE | Yes (all after first BLEND go to TR) |
| NONE → NONE → NONE | All three | — | Yes (all in OP, no TR) |
| BLEND → BLEND → NONE | — | All three | Yes (all in TR) |
| NONE (untex) → NONE (tex) → BLEND | First NONE (untex) | Second NONE (tex) + BLEND | Yes (textured NONE goes to TR) |
| NONE (untex) → BLEND → NONE (untex) | First NONE (untex) | BLEND + second NONE | Yes (second NONE goes to TR) |

### 8.2 Edge cases requiring rejection

| SDL command sequence | Routing result | Reason |
|---|---|---|
| NONE (tex) → BLEND → NONE (tex) | All → TR | The first textured draw closes the prefix |
| BLEND → NONE → BLEND | All → TR | The first BLEND draw closes the prefix |
| NONE → clip change → NONE → BLEND | Both NONE commands → OP | State-only commands preserve the opaque prefix |
| NONE (RTT) → BLEND | All → TR | RTT selected, opaque routing disabled |
| NONE → NONE (different clip) → BLEND | Both NONE commands → OP | Clip state alone does not close the prefix |

### 8.3 Depth-state edge cases

| Scenario | Depth issue | Mitigation |
|---|---|---|
| Opaque-prefix geometry then TR geometry | Both use always/no-write depth state | List order, not depth, preserves the allowed ordering |
| Leading clear then opaque-prefix geometry | Clear and geometry are both OP | Correct if the clear is explicitly routed OP and physically validated |
| Opaque-prefix geometry followed by a clear | Clear stays OP while the prefix remains open | Correct in submission order; a clear after TR stays TR |
| Textured `NONE` then untextured `NONE` | First draw is TR, so prefix is closed | Both draw through TR; no order inversion |

### 8.4 List-capacity edge cases

| Scenario | TA/OPB impact |
|---|---|
| All opaque draws in OP list | OP list bin size must be sufficient. `PVR_BINSIZE_16` (64 bytes) per list entry. |
| Mixed OP + TR lists | Both lists consume from the same vertex buffer. Total capacity is 512 KiB shared. |
| Many small opaque draws | Each opaque draw emits a header (32 bytes) + vertices (32 bytes per vertex). The header cache reduces header emissions. |
| OP list overflow | PVR hardware handles OPB overflow internally (3 extra partitions). No observable signal. |

---

## 9. Recommended first increment

### 9.1 Scope

**Route only an opaque prefix: a supported leading clear and untextured
`SDL_BLENDMODE_NONE` geometry, before any drawable that must use TR.**

### 9.2 Changes required

1. **PVR initialization:** Enable `PVR_LIST_OP_POLY` in `pvr_params.opb_sizes[0]` (change from `PVR_BINSIZE_0` to `PVR_BINSIZE_16` or `PVR_BINSIZE_4`).

2. **RunCommandQueue / scene manager:** Replace eager transparent-list start
   with lazy `EnsureList()` ownership; track `opaque_prefix_open`, opaque-list
   active, and transparent-list active state.

3. **EmitHeader:** Include active list type in header-cache identity.

4. **Eligibility predicate:** Implement the opaque-prefix predicate from
   Section 6.1 and close the prefix after every non-eligible drawable.

5. **Opaque list lifecycle:** Begin/finish the opaque list conditionally in `RunCommandQueue`.

6. **RenderPresent:** Reset routing state.

### 9.3 What is NOT included

- Textured opaque routing.
- Punch-through routing.
- Trailing opaque run optimization.
- Per-frame scene splitting.
- Depth-state management beyond the opaque list's default behavior.
- Render-target opaque routing.
- Textured or strided opaque routing.
- DMA-partial-upload interaction with opaque routing.
- Any change to the translucent list behavior.

### 9.4 Build-time guard

The opaque-list enablement should be guarded by a Dreamcast-specific define (e.g., `DC_PVR_OPAQUE_ROUTING_ENABLED`) so that:

- The opaque routing path is compiled out in the ordinary build.
- The renderer's PVR init params remain unchanged when the guard is off.
- The opaque list is never begun when the guard is off.

### 9.5 Hardware test plan

A focused physical-hardware test should verify:

1. **Leading opaque run:** Opaque rectangle → translucent rectangle. The opaque rectangle must appear below the translucent rectangle (correct painter order).

2. **Mid-frame opaque:** Opaque → translucent → opaque. The second opaque must appear below the translucent (both in translucent list).

3. **All-opaque:** All opaque geometry. All rendered in opaque list. No translucent list overhead.

4. **All-translucent:** All translucent geometry. No opaque list overhead.

5. **State-only commands:** Opaque → viewport/clip/draw-color changes →
   opaque. The second opaque remains eligible because state-only commands do
   not close the prefix.

6. **Opaque + textured boundary:** Opaque (untextured) → opaque (textured)
   → opaque (untextured). The textured and final opaque draws must both go to
   TR, preserving their order.

7. **Painter-order inversion probe:** Opaque red at (0,0) → translucent green alpha-50% at (0,0) → opaque blue at (0,0). Expected: red at bottom, green translucent in middle, blue on top.

8. **No-op check:** Verify that when opaque routing is disabled (guard off), the test produces identical output to the all-translucent baseline.

### 9.6 Expected visual results

| Test | Expected result |
|---|---|
| Leading opaque run | Opaque appears below translucent (correct) |
| Mid-frame opaque | Second opaque appears below translucent (correct) |
| All-opaque | All drawn opaque, no translucent artifacts |
| All-translucent | All drawn with alpha blending, no depth artifacts |
| State-only commands | Both opaque draws retain their correct prefix order |
| Opaque + textured boundary | Textured and following opaque draws both remain in TR |
| Painter-order probe | Red → green → blue (top to bottom visual stack) |

---

## 10. Focused physical-hardware validation plan

### 10.1 Test design

Create a single focused test (`test/dreamcast/test/pvr_list_routing`) with three pages:

**Page 1: Leading opaque run**
- Full-screen opaque black clear (routed to opaque list).
- Opaque red rectangle at (50, 50, 200, 150).
- Translucent green rectangle at (100, 100, 200, 150) with alpha modulation.
- Opaque blue rectangle at (150, 150, 200, 150) — after translucent, must go to TR.
- Expected: black background, red rectangle, green translucent overlap on red, blue on top of green (not on red).

**Page 2: Painter-order inversion probe**
- Opaque red at (0, 0, 300, 200).
- Translucent green alpha-128 at (100, 0, 300, 200).
- Opaque blue at (200, 0, 300, 200) — after translucent, goes to TR.
- Translucent yellow alpha-128 at (300, 0, 300, 200).
- Expected: red → green blend → blue (on top of green) → yellow blend (on top of blue).

**Page 3: Edge-case rejections**
- Opaque untextured at (0, 0, 100, 100) — routed to OP.
- Clip rect change to (50, 50, 300, 300).
- Opaque untextured at (50, 50, 100, 100) — remains OP (state-only command).
- Opaque textured quad — closes the prefix and goes to TR.
- Opaque untextured rectangle after the textured quad — stays TR.
- Expected: first two opaque shapes in OP; textured and final opaque shapes in
  TR; painter order preserved.

### 10.2 Capture requirements

- Raw framebuffer capture via `/pc` path (e.g., `vid_screen_shot("/pc/pvr_list_routing-page1.ppm")`).
- All three pages must be captured at 640×480 VGA.
- Program must return zero normally to `dc-load-ip`.
- SHA-256 hashes of all captured PPMs must be recorded.

### 10.3 Non-visual validation

- Linker/map inspection to confirm the opaque-routing symbols are present in the opt-in build and absent in the opt-out build.
- Serial output confirming the number of opaque and translucent draws executed.
- No KOS warnings or PVR errors in serial output.

---

## 11. Risks and stop conditions

### 11.1 Risks

1. **List-context regression:** A list-specific header or an accidental depth
   default could change visual behavior. **Mitigation:** explicitly retain the
   current always/no-write depth state in both contexts and capture overlap
   probes on hardware.

2. **List capacity:** Adding the opaque list reserves OPB space and can reduce
   translucent capacity. **Mitigation:** compare the guarded and ordinary
   capacity tests before calling the optimization viable; do not label this
   risk low without measurement.

3. **Header-cache collisions:** If the header cache is not list-type-aware, a
   valid translucent header may be reused for an opaque draw (or vice versa),
   producing incorrect rendering. **Mitigation:** Include list type in cache
   identity.

4. **Opaque list not enabled:** If the PVR init params do not enable the opaque list, `pvr_list_begin(PVR_LIST_OP_POLY)` will fail. **Mitigation:** Guard all opaque-list operations with the compile-time define. If the define is off, all opaque-eligible draws fall through to the translucent list (same as current behavior).

5. **Render-target interaction:** Target preservation and ordered-RTT scene
   transitions add an independent lifecycle surface. **Mitigation:** disable
   opaque routing for target scenes in the first prototype.

6. **Strided texture interaction:** Textured and strided draws are deliberately
   routed through TR in this prototype. **Mitigation:** retain existing
   frame-global stride handling unchanged.

### 11.2 Stop conditions

The first increment must be **stopped** (reverted) if physical-hardware testing reveals:

1. **Painter-order inversion:** Any case where an opaque draw that should appear below a translucent draw renders above it, or vice versa.
2. **Depth artifacts:** Visible z-fighting, incorrect depth testing, or depth-buffer corruption.
3. **PVR crash/reset:** Any PVR timeout, scene failure, or DC reset during opaque list rendering.
4. **List overflow:** Observable TA/OPB overflow (blanking, corruption, or lost geometry).
5. **Header-cache collision:** Visual artifacts caused by incorrect header reuse across list boundaries.
6. **Regression of validated paths:** Any regression in the existing validated 640×480 VGA path (lifecycle, viewport, clipping, points/lines, textures, blending, copy-ex).

### 11.3 Known limitations of the first increment

- Only untextured `BLENDMODE_NONE` geometry is routed.
- Only opaque-prefix routing is supported.
- Render-target opaque routing is disabled.
- State-only viewport, clip, and draw-color commands do not close the prefix.
- No punch-through routing.
- No per-draw depth-state management.

---

## 12. GO / NO-GO verdict

### GO for:

- **Opaque-prefix routing** of a supported leading clear and untextured
  `SDL_BLENDMODE_NONE` geometry, with the eligibility predicates defined in
  Section 6.1.
- **Compile-time guard** (`DC_PVR_OPAQUE_ROUTING_ENABLED`) so the opaque path is absent in the ordinary build.
- **Focused physical-hardware validation** with the three-page test matrix in Section 10.

### NO-GO for:

- **Punch-through routing** — not equivalent to SDL blend modes; binary alpha threshold is not a general-purpose substitute for alpha blending.
- **Arbitrary interleaved routing** — violates SDL painter order when opaque draws appear after translucent draws in SDL submission order.
- **Trailing opaque runs** — PVR processes opaque before translucent regardless of submission order; a trailing opaque run renders before preceding translucent draws, reversing SDL painter order.
- **Per-frame scene splitting** — correct but expensive; double scene overhead is not justified for a first increment.
- **Depth-state tricks** — too many failure modes; vertex Z manipulation conflicts with SDL's coordinate system.
- **Textured opaque routing** — deferred to a later increment after the untextured path is validated.
- **Render-target opaque routing** — disabled until RTT depth-state behavior is validated.

### Final verdict: **GO with narrow scope.**

The opaque-prefix route can preserve order because OP executes before TR and
the prefix is permanently closed before the first TR drawable. This is a
guarded feasibility prototype, not yet a demonstrated performance win: it must
retain current depth state, pass the focused hardware captures, and show an
acceptable OPB/capacity tradeoff before promotion.

This is the narrowest increment that provides a measurable performance benefit (fewer translucent draws, no alpha-blending overhead for opaque geometry) while preserving all validated SDL semantics.

---

## Implementation (2026-07-30)

A guarded implementation was added behind `DC_PVR_OPAQUE_PREFIX_ROUTING`.

### Files changed

- `src/render/dreamcast/SDL_render_dreamcast_pvr_data.h` — added
  `opaque_prefix_open`, `active_list_type`, and `header_list_type` to
  `DC_PVR_RenderData`.
- `src/render/dreamcast/SDL_render_dreamcast_pvr.h` — updated
  `DC_PVR_EmitHeader` so its cache identity receives the active list type.
- `src/render/dreamcast/SDL_render_dreamcast_pvr.c` — added the three opaque-
  prefix helper functions; modified `DC_PVR_EmitHeader` to accept and cache
  `list_type`; modified `DC_PVR_BeginFrame` to skip eager TR list init when
  the guard is on; modified `DC_PVR_RunCommandQueue` to route clears and
  untextured NONE through OP while the prefix is open; modified
  `DC_PVR_RenderPresent` to use `DC_PVR_FinishCurrentList` and reset the
  prefix state; modified `DC_PVR_CreateRenderer` to enable the OP list in
  `pvr_params` and initialize the new state fields.
- `src/render/dreamcast/SDL_render_dreamcast_pvr_geometry.c` — updated all
  three `DC_PVR_EmitHeader` callers to pass `data->active_list_type`.
- `CMakeLists.txt` — added `SDL_DC_PVR_OPAQUE_PREFIX_ROUTING` dep-option and
  `SDL_DC_PVR_OPAQUE_PREFIX_ROUTING` define.
- `test/dreamcast/test/pvr_list_routing/` — new focused test (C source,
  Makefile.dc, README).

### Exact routing state machine

```
State: opaque_prefix_open (bool, init TRUE)
       active_list_type   (int, init -1)
       list_active        (bool, inherited)

Per-frame:
  opaque_prefix_open = SDL_TRUE    (set at renderer create / RenderPresent)
  active_list_type = -1

Per drawable (clear or geometry):
  if (opaque_prefix_open && blend == NONE && texture == NULL)
      → DC_PVR_BeginList(PVR_LIST_OP_POLY)
      → opaque_prefix_open stays TRUE
  else
      → DC_PVR_BeginList(PVR_LIST_TR_POLY)
      → opaque_prefix_open = SDL_FALSE   (never reopens)

State-only commands (viewport, clip, draw-color, no-op, empty draw):
  → no list transition, no prefix closure

At RenderPresent (successful):
  → DC_PVR_FinishCurrentList()   (finishes whatever list is active)
  → pvr_scene_finish()
  → opaque_prefix_open = SDL_TRUE
  → active_list_type = -1

On destruction:
  → DC_PVR_FinishCurrentList() if list_active   (cleanup safe)
```

### Key design decisions

1. **Lazy list initialization** — `DC_PVR_BeginFrame` no longer eagerly
   starts the TR list when the guard is on. The first drawable opens the
   required list, avoiding an empty TR scene when only opaque draws are
   present.

2. **Header cache includes list type** — `header_list_type` is now part of
   the cache identity. Transitions between OP and TR invalidate the cache.

3. **`DC_PVR_FinishCurrentList`** — a unified helper that finishes whatever
   list is active. Safer than hardcoding `PVR_LIST_TR_POLY` in
   `RenderPresent`.

4. **Opaque prefix never reopens** — once `opaque_prefix_open` is set to
   `SDL_FALSE`, it stays false for the entire frame. This prevents painter-
   order inversion from any later opaque draw.

5. **OPB bin size** — `PVR_BINSIZE_16` (64 bytes per entry) is conservative.
   Measured capacity comparison is deferred.

### Build integration

Feature-on:
```sh
cmake -S . -B /tmp/sdl2-dc-opaque \
  -DCMAKE_TOOLCHAIN_FILE=/opt/toolchains/dc/kos/utils/cmake/kallistios.toolchain.cmake \
  -DSDL_VIDEO_RENDER_DREAMCAST_PVR=ON \
  -DSDL_DC_PVR_OPAQUE_PREFIX_ROUTING=ON \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON \
  -DCMAKE_C_FLAGS="-Wall -Wextra"
cmake --build /tmp/sdl2-dc-opaque -j2
```

Feature-off (ordinary, behaviorally identical to pre-routing baseline):
```sh
cmake -S . -B /tmp/sdl2-dc-opaque-off \
  -DCMAKE_TOOLCHAIN_FILE=/opt/toolchains/dc/kos/utils/cmake/kallistios.toolchain.cmake \
  -DSDL_VIDEO_RENDER_DREAMCAST_PVR=ON \
  -DSDL_DC_PVR_OPAQUE_PREFIX_ROUTING=OFF \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON \
  -DCMAKE_C_FLAGS="-Wall -Wextra"
cmake --build /tmp/sdl2-dc-opaque-off -j2
```

---

### Physical-hardware validation (2026-07-30)

The guarded feature-on build and the feature-off build both passed strict
warning compilation. The feature-on ELF was explicitly linked against
`/tmp/sdl2-dc-opaque/libSDL2.a` and passed on a 640×480 VGA Dreamcast. It
returned zero with no KOS PVR errors and wrote all three raw `/pc` captures.
Direct inspection confirms the leading opaque, textured-NONE boundary, and
state-only viewport/clip cases. Capture SHA-256 values are recorded in the
focused test README. This validates the narrow guarded path only; capacity,
performance, render-target interaction, and punch-through remain deferred.
