# Dreamcast Native PVR Renderer: Opaque-Prefix List-Routing Failure Analysis

**Status:** Resolved historical failure analysis; the corrected guarded
prototype subsequently passed its focused physical-hardware test.
**Date:** 2026-07-30
**Scope:** Diagnosis of the failed hardware run of `test/dreamcast/test/pvr_list_routing` (guarded feature-on build, `DC_PVR_OPAQUE_PREFIX_ROUTING`).

---

## 1. Executive Verdict

**Root cause: Under `DC_PVR_OPAQUE_PREFIX_ROUTING`, the prototype does not initialize `data->active_list_type` in `DC_PVR_CreateRenderer()`. `SDL_calloc()` zero-initializes all renderer data, so `data->active_list_type` starts at 0, which equals `PVR_LIST_OP_POLY` (the enum value 0). The first `DC_PVR_BeginList(renderer, PVR_LIST_OP_POLY)` call in `DC_PVR_RunCommandQueue()` sees `data->active_list_type == list_type` (0 == 0) and returns 0 early without calling `pvr_list_begin()` in KOS. `pvr_prim()` is then called with `pvr_state.list_reg_open == PVR_LIST_NONE`, producing "attempt to submit to unopened list."**

**The two logged errors correspond to:**

1. **Page 1, `SDL_RenderClear()`** — clear command routes to OP list (eligible), `DC_PVR_BeginList()` returns early (bogus match), `pvr_prim()` fails.
2. **Page 1, first `SDL_RenderFillRect()` (red rectangle)** — same opaque prefix match, same failure.

**The green rectangle (BLEND) and blue rectangle (NONE after BLEND) succeed** because they route to TR list, where `active_list_type` (0) does not match `PVR_LIST_TR_POLY` (2), so `pvr_list_begin()` is actually called and succeeds.

**Verdict: NO-GO for the current prototype design.** A single initialization fix would eliminate the logged errors, but the prototype's architectural approach — using `opaque_prefix_open` as an SDL painter-order proxy that must be closed by a textured draw — is fragile. The SDL command queue does not guarantee that a textured draw will appear before the next `SDL_RenderPresent()`. The prototype's opaque prefix state machine must survive `SDL_RenderPresent()` (which resets `opaque_prefix_open = SDL_TRUE`) and must handle the case where the prefix is never closed (multiple opaque draws followed by present). The current design does not handle this.

**A minimal source correction (initialize `active_list_type`) is provided below but is not sufficient for production use.**

**Resolution (2026-07-30):** The implementation now initializes
`active_list_type` to `PVR_LIST_NONE`, and the focused test checks draw
failures rather than printing PASS after them. A corrected feature-on run
produced all three captures with no KOS PVR errors and returned zero. The
original failure and captures remain useful diagnostic evidence; its NO-GO
conclusion is superseded for the narrow guarded feasibility path. Capacity,
performance, render-target interaction, and punch-through are still unvalidated.

---

## 2. Exact Failed Hardware Evidence

### 2.1 Serial Log (`test/dreamcast/test/pvr_list_routing/hardware-list-routing.log`)

```
INFO: === Page 1: Leading opaque run ===
pvr_prim: attempt to submit to unopened list
pvr_prim: attempt to submit to unopened list
vid_screen_shot: written to output file '/pc/pvr_list_routing-page1.ppm'
INFO: === Page 2: Textured-NONE boundary ===
vid_screen_shot: written to output file '/pc/pvr_list_routing-page2.ppm'
INFO: === Page 3: State-only and lifecycle ===
vid_screen_shot: written to output file '/pc/pvr_list_routing-page3.ppm'
vid_set_mode: 640x480 VGA with 1 framebuffers.
INFO: PASS
Program returned 0
```

**Key observations:**

- The two errors occur **immediately after** "Page 1" is printed, before any `SDL_RenderPresent()` call.
- The errors correspond to the **first `SDL_RenderClear()`** (clear command) and the **first `SDL_RenderFillRect()`** (red rectangle).
- Page 2 and page 3 produce **no KOS errors** — subsequent draws route to TR list where `active_list_type` (0) does not match `PVR_LIST_TR_POLY` (2), so `pvr_list_begin()` is actually called.
- The test prints PASS and returns 0 despite SDL submission failures. The test does not check `SDL_RenderClear()` or `SDL_RenderFillRect()` return values.

### 2.2 Page 1 Capture Analysis (`pvr_list_routing-page1.ppm`)

**SHA-256:** `078323173c40ecd2e9c39a8fc24fb36446f17c427d357a930e85912861db852c`

**Format:** Valid 640×480 Netpbm PPM (rawbits, pixmap).

**Visual evidence:** The captured framebuffer shows **green and blue panels** but **black where the opaque red rectangle should appear**. The red rectangle at (50, 50, 200, 150) is completely missing from the framebuffer.

**Confirmed:** The red rectangle's `pvr_prim()` submission failed and was silently dropped by KOS.

### 2.3 Page 2 and Page 3 Captures

**Page 2 SHA-256:** `647d0487b2933ebf1a4d6322b70620f12d8fbe1a91dfe03a54bf241ea535ab10`
**Page 3 SHA-256:** `35c80b2222c399bdac1c1cb11dc9e08ee481c1cb11dc9e08ee`

Both are valid 640×480 PPMs. Page 2 and page 3 produced **no KOS errors**.

### 2.4 Invalid PASS

The test's PASS is invalid:

1. SDL submission errors during `SDL_RenderClear()` were not checked or propagated.
2. The test printed PASS based on its own return-value check, not on framebuffer inspection.
3. The program returned 0 despite the red rectangle being silently dropped.

---

## 3. KOS List-State Model

### 3.1 Scene State (`pvr_state` fields)

**Source-verified (KOS `pvr_scene.c` and `pvr_internal.h`):**

| Field | Type | Purpose |
|-------|------|---------|
| `pvr_state.lists_enabled` | `uint32_t` | Bitmask of enabled list types (set by `pvr_init()`) |
| `pvr_state.list_reg_open` | `pvr_list_t` | Currently open list type (`PVR_LIST_NONE` = none) |
| `pvr_state.lists_closed` | `uint32_t` | Bitmask of closed lists (set by `pvr_list_finish()`) |
| `pvr_state.ta_checked_ready` | `int` | TA readiness flag |
| `pvr_state.ta_busy` | `int` | Scene-in-progress flag |
| `pvr_state.render_busy` | `int` | Render-in-progress flag |

### 3.2 `pvr_scene_begin()` — Scene Reset

**Source-verified (KOS `pvr_scene.c` line ~100):**

```c
void pvr_scene_begin(void) {
    pvr_state.next_to_texture = 0;
    pvr_state.ta_checked_ready = 0;
    pvr_state.lists_closed = 0;          // RESETS closed-list bitmask
    pvr_state.list_reg_open = PVR_LIST_NONE;  // RESETS open-list
    // ... DMA buffer resets ...
}
```

**Critical:** `pvr_scene_begin()` **resets `pvr_state.lists_closed` to 0** and `pvr_state.list_reg_open` to `PVR_LIST_NONE`. This is the scene's "fresh start" — it forgets which lists were closed in the previous scene.

**Note:** `pvr_scene_begin()` returns `void`. No error propagation is possible.

### 3.3 `pvr_list_begin()` — List Opening

**Source-verified (KOS `pvr_scene.c` line ~194):**

```c
int pvr_list_begin(pvr_list_t list) {
    // Check if already closed
    if(PVR_DEBUG && !pvr_state.dma_mode && pvr_state.lists_closed & BIT(list)) {
        dbglog(DBG_WARNING, "pvr_list_begin: attempt to open already closed list\n");
        return -1;
    }

    // If another list is already open, close it first
    if(pvr_state.list_reg_open != PVR_LIST_NONE && pvr_state.list_reg_open != list)
        pvr_list_finish();

    pvr_state.list_reg_open = list;
    return 0;
}
```

**Key rules:**

1. A list that is in `pvr_state.lists_closed` **cannot be reopened** within the same scene. Returns -1.
2. If another list is already open, `pvr_list_finish()` is called implicitly to close it.
3. `pvr_state.list_reg_open` is set to the new list type.

### 3.4 `pvr_list_finish()` — List Closing

**Source-verified (KOS `pvr_scene.c` line ~225):**

```c
int pvr_list_finish(void) {
    // Check if list is already closed
    if(PVR_DEBUG && !pvr_state.dma_mode && pvr_state.list_reg_open == PVR_LIST_NONE) {
        dbglog(DBG_WARNING, "pvr_list_finish: attempt to close unopened list\n");
        return -1;
    }

    if(!pvr_list_dma) {
        pvr_blank_polyhdr(pvr_state.list_reg_open);
        sq_unlock();
        pvr_state.lists_closed |= BIT(pvr_state.list_reg_open);
        pvr_sq_set32((void *)0, 0, 32, PVR_DMA_TA);
    }

    pvr_state.list_reg_open = PVR_LIST_NONE;
    return 0;
}
```

**Critical rules:**

1. If `pvr_state.list_reg_open == PVR_LIST_NONE` (no list is open), returns -1.
2. Sets `pvr_state.lists_closed |= BIT(active_list)`.
3. Resets `pvr_state.list_reg_open` to `PVR_LIST_NONE`.

### 3.5 `pvr_prim()` — Primitive Submission

**Source-verified (KOS `pvr_prim.c` line ~35):**

```c
int pvr_prim(const void *data, size_t size) {
    if(PVR_DEBUG && pvr_state.list_reg_open == PVR_LIST_NONE) {
        dbglog(DBG_WARNING, "pvr_prim: attempt to submit to unopened list\n");
        return -1;
    }
    // ... submit via SQ or DMA ...
}
```

**The "attempt to submit to unopened list" error:** Occurs when `pvr_prim()` is called and `pvr_state.list_reg_open == PVR_LIST_NONE`. No list has been opened in the current scene (or the open list was closed without a new one being opened).

### 3.6 `pvr_scene_finish()` — Unsubmitted List Handling

**Source-verified (KOS `pvr_scene.c` line ~260):**

```c
int pvr_scene_finish(void) {
    // ... close open list if any ...

    // Fill unsubmitted lists with blank headers
    for(i = 0; i < PVR_OPB_COUNT; i++) {
        if((pvr_state.lists_enabled & BIT(i)) && (!(pvr_state.lists_closed & BIT(i)))) {
            pvr_list_begin(i);
            pvr_blank_polyhdr(i);
            pvr_list_finish();
        }
    }
    // ...
}
```

**Critical:** `pvr_scene_finish()` fills **any enabled list that was not submitted** with a blank polygon header. This is a safety net — but it only works if `pvr_state.lists_closed` is valid.

### 3.7 KOS `pvr_list_t` Enum

**Source-verified (KOS `pvr_header.h` line 61):**

```c
typedef enum pvr_list_type {
    PVR_LIST_OP_POLY,           // 0 — Opaque polygon list
    PVR_LIST_OP_MOD,            // 1 — Opaque modifier list
    PVR_LIST_TR_POLY,           // 2 — Translucent polygon list
    PVR_LIST_TR_MOD,            // 3 — Translucent modifier list
    PVR_LIST_PT_POLY,           // 4 — Punch-thru polygon list
    PVR_LIST_NONE = -1          // Used internally to signal unset values
} pvr_list_t;
```

**Critical: `PVR_LIST_OP_POLY` is 0. `SDL_calloc()` zero-initializes all memory.**

---

## 4. Prototype State-Transition Audit

### 4.1 The Prototype's `DC_PVR_CreateRenderer()` Bug

**Source-verified (uncommitted diff):**

```c
#if defined(DC_PVR_OPAQUE_PREFIX_ROUTING)
    data->opaque_prefix_open = SDL_TRUE;
#else
    data->active_list_type = PVR_LIST_TR_POLY;
#endif
```

**Under `DC_PVR_OPAQUE_PREFIX_ROUTING`, `data->active_list_type` is NOT set.** `SDL_calloc()` zero-initializes it, so `data->active_list_type = 0 = PVR_LIST_OP_POLY`.

**This is the root cause.** The first `DC_PVR_BeginList(renderer, PVR_LIST_OP_POLY)` call sees `data->active_list_type == list_type` (0 == 0) and returns 0 early without calling `pvr_list_begin()`.

### 4.2 The Prototype's `DC_PVR_BeginList()`

**Source-verified (uncommitted diff):**

```c
static int DC_PVR_BeginList(SDL_Renderer *renderer, int list_type)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (data->active_list_type == list_type)
        return 0;  // <-- Returns early if values match

    if (data->list_active) {
        if (pvr_list_finish() < 0) {
            data->pvr_failed = SDL_TRUE;
            return SDL_SetError("...");
        }
        data->list_active = SDL_FALSE;
        data->active_list_type = PVR_LIST_NONE;
    }

    data->header_valid = SDL_FALSE;

    if (pvr_list_begin(list_type) < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("...");
    }
    data->list_active = SDL_TRUE;
    data->active_list_type = list_type;
    return 0;
}
```

**Bug:** The early return `if (data->active_list_type == list_type)` does NOT check whether the list is actually open in KOS. If `active_list_type` matches but `pvr_state.list_reg_open != list_type`, the list is not open and `pvr_prim()` will fail.

### 4.3 The Prototype's `DC_PVR_RenderPresent()`

**Source-verified (uncommitted diff):**

```c
if (DC_PVR_BeginFrame(renderer) < 0) {
    return -1;
}
if (DC_PVR_FinishCurrentList(renderer) < 0) {
    return -1;
}
if (pvr_scene_finish() < 0) {
    data->pvr_failed = SDL_TRUE;
    return SDL_SetError("...");
}
data->scene_active = SDL_FALSE;
data->header_valid = SDL_FALSE;
#if defined(DC_PVR_OPAQUE_PREFIX_ROUTING)
    data->opaque_prefix_open = SDL_TRUE;
    data->active_list_type = -1;
#endif
```

**Order:** `BeginFrame()` → `FinishCurrentList()` → `pvr_scene_finish()`.

**After present:** `opaque_prefix_open = SDL_TRUE` (restart prefix) and `active_list_type = -1` (reset).

**Note:** `active_list_type` is reset to -1 AFTER `pvr_scene_finish()`. This is correct for the next frame, but the initialization bug in `CreateRenderer()` means the first frame starts with `active_list_type = 0`.

### 4.4 The Prototype's `DC_PVR_BeginFrame()`

**Source-verified (uncommitted diff):**

```c
if (!data->scene_active) {
    if (pvr_scene_begin() < 0) {  // <-- RETURNS VOID, check is meaningless
        // NO ERROR HANDLING
    }
    data->scene_active = SDL_TRUE;
    data->header_valid = SDL_FALSE;
}
#if !defined(DC_PVR_OPAQUE_PREFIX_ROUTING)
    if (!data->list_active) {
        pvr_list_begin(PVR_LIST_TR_POLY);
        data->list_active = SDL_TRUE;
        data->active_list_type = PVR_LIST_TR_POLY;
    }
#endif
return 0;
```

**Under `DC_PVR_OPAQUE_PREFIX_ROUTING`, eager TR list init is SKIPPED.** The prototype relies on `DC_PVR_BeginList()` to open the correct list at draw time.

### 4.5 The Fatal Sequence (Page 1)

**Step-by-step trace:**

1. **Test calls `SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255)`.** Sets draw color (state-only).

2. **Test calls `SDL_RenderClear(renderer)`.** SDL internally calls `SDL_RenderFlush()` → `DC_PVR_RunCommandQueue()`.

3. **`DC_PVR_RunCommandQueue()` for the clear command:**
   - `data->scene_active = SDL_FALSE`
   - `data->list_active = SDL_FALSE`
   - `data->active_list_type = 0` (from `SDL_calloc`)
   - `data->opaque_prefix_open = SDL_TRUE`

4. **`DC_PVR_IsOpaquePrefixEligible(SDL_BLENDMODE_NONE, NULL, SDL_TRUE)`** returns `SDL_TRUE`.

5. **`DC_PVR_BeginList(renderer, PVR_LIST_OP_POLY)`:**
   - `data->active_list_type == list_type` → `0 == 0` → **TRUE**
   - **Returns 0 early without calling `pvr_list_begin()`.**
   - `data->list_active` remains `SDL_FALSE`.
   - `pvr_state.list_reg_open` remains `PVR_LIST_NONE`.

6. **`DC_PVR_EmitFullTargetClear(renderer, cmd)`:**
   - `pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY)`
   - `pvr_poly_compile(&poly, &context)`
   - **`pvr_prim(&poly, sizeof(poly))`:**
     - `pvr_state.list_reg_open == PVR_LIST_NONE`
     - **Prints "pvr_prim: attempt to submit to unopened list"**
     - Returns -1

7. **`DC_PVR_RunCommandQueue()` returns -1.** SDL sets error. `SDL_RenderClear()` returns -1. **Test does not check return value.**

8. **Test calls `SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255)`.** Sets draw color.

9. **Test calls `SDL_RenderFillRect(renderer, &r)` (red rectangle):**
   - `DC_PVR_RunCommandQueue()` is called.
   - `data->active_list_type` is still **0** (never updated because step 5 returned early).
   - `DC_PVR_IsOpaquePrefixEligible()` returns `SDL_TRUE`.
   - `DC_PVR_BeginList(renderer, PVR_LIST_OP_POLY)`:
     - `data->active_list_type == list_type` → `0 == 0` → **TRUE**
     - **Returns 0 early without calling `pvr_list_begin()`.**
   - **`pvr_prim()` fails again.** Second error logged.

10. **Test calls `SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)`.** Sets blend mode.

11. **Test calls `SDL_RenderFillRect(renderer, &r)` (green rectangle):**
    - `DC_PVR_IsOpaquePrefixEligible(SDL_BLENDMODE_BLEND, NULL, SDL_TRUE)` returns `SDL_FALSE` (blend != NONE).
    - `DC_PVR_BeginList(renderer, PVR_LIST_TR_POLY)`:
      - `data->active_list_type == list_type` → `0 == 2` → **FALSE**
      - Proceeds to `pvr_list_begin(PVR_LIST_TR_POLY)`.
      - **`pvr_state.list_reg_open` is set to `PVR_LIST_TR_POLY`.**
      - Returns 0.
    - **`pvr_prim()` succeeds.** No error.
    - `data->opaque_prefix_open = SDL_FALSE` (prefix closed).

12. **Test calls `SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE)`.** Sets blend mode back to NONE.

13. **Test calls `SDL_RenderFillRect(renderer, &r)` (blue rectangle):**
    - `DC_PVR_IsOpaquePrefixEligible(SDL_BLENDMODE_NONE, NULL, SDL_FALSE)` returns `SDL_FALSE` (prefix closed).
    - `DC_PVR_BeginList(renderer, PVR_LIST_TR_POLY)`:
      - `data->active_list_type` is still 0 (never updated).
      - `0 == 2` → FALSE.
      - `pvr_list_begin(PVR_LIST_TR_POLY)` succeeds (same list already open).
    - **`pvr_prim()` succeeds.** No error.

14. **Test calls `SDL_RenderPresent(renderer)`:**
    - `DC_PVR_FinishCurrentList()` closes the TR list.
    - `pvr_scene_finish()` fills unsubmitted lists with blank headers.
    - `data->opaque_prefix_open = SDL_TRUE`, `data->active_list_type = -1`.

15. **Test goes to page 2.** `data->active_list_type = -1`. No more early-match bugs.

### 4.6 Why Only Two Errors?

The two errors correspond to the clear and the first red rectangle, both of which:
- Are eligible for OP prefix (`SDL_BLENDMODE_NONE`, no texture, prefix open).
- Match `data->active_list_type == PVR_LIST_OP_POLY` (0 == 0).
- Return early from `DC_PVR_BeginList()` without opening the list.

The green rectangle (BLEND) routes to TR, where `active_list_type` (0) does not match `PVR_LIST_TR_POLY` (2), so `pvr_list_begin()` is called and succeeds. The blue rectangle (NONE after BLEND) also routes to TR because the prefix was closed by the green draw.

---

## 5. Source-Verified Facts

### 5.1 `PVR_LIST_OP_POLY` is 0

**Source:** KOS `pvr_header.h` line 61. Confirmed by enum ordering.

### 5.2 `SDL_calloc()` Zero-Initializes

**Source:** SDL documentation. `SDL_calloc(n, size)` allocates and zero-fills memory.

### 5.3 Prototype Does Not Initialize `active_list_type` Under Guard

**Source:** Uncommitted diff, `DC_PVR_CreateRenderer()` lines. Under `DC_PVR_OPAQUE_PREFIX_ROUTING`, only `opaque_prefix_open = SDL_TRUE` is set. `active_list_type` is not initialized.

### 5.4 `DC_PVR_BeginList()` Early-Return Does Not Verify KOS State

**Source:** Uncommitted diff. The early return `if (data->active_list_type == list_type)` does not check `pvr_state.list_reg_open`. It assumes renderer-local state matches KOS state.

### 5.5 `pvr_scene_begin()` Returns Void

**Source:** KOS `pvr_scene.c` line ~100. No error propagation possible.

### 5.6 Test Does Not Check SDL Render Return Values

**Source:** `pvr_list_routing.c`. `SDL_RenderClear()` and `SDL_RenderFillRect()` return values are not checked.

---

## 6. Inference

### 6.1 The Initialization Bug Is Necessary But Not Sufficient

Fixing `active_list_type` initialization to `PVR_LIST_NONE` (-1) would eliminate the logged errors. The first OP list open would proceed correctly. However, the prototype's architectural approach remains fragile:

1. **The opaque prefix must survive `SDL_RenderPresent()`.** The prototype resets `opaque_prefix_open = SDL_TRUE` after each present. This is correct for a fresh frame but means the prefix is always open at the start of a frame.

2. **The prefix is closed by a textured draw.** If the frame contains only opaque untextured draws (clear + rectangles), the prefix is never closed. At `SDL_RenderPresent()`, `DC_PVR_FinishCurrentList()` closes the last active list (OP). Then `pvr_scene_finish()` fills unsubmitted TR lists with blank headers. This is acceptable but wastes a TR list with a blank header.

3. **State-only commands must not close the prefix.** The prototype relies on `DC_PVR_IsOpaquePrefixEligible()` to check `opaque_prefix_open`. If `opaque_prefix_open` is TRUE and the draw is NONE/untextured, the prefix stays open. This is correct.

4. **The prefix cannot be reopened once closed.** `opaque_prefix_open` is set to SDL_FALSE when the first textured draw is encountered. It is never set back to TRUE until the next `SDL_RenderPresent()`. This is correct for SDL painter order (opaque prefix must come first in the frame).

### 6.2 The Prototype's Approach Is Fundamentally Flawed

The opaque-prefix approach assumes that SDL's command queue will emit opaque draws before translucent draws within the same frame. This is true for the current SDL implementation but is not a guaranteed contract. The prototype's state machine must handle:

- Multiple `SDL_RenderPresent()` calls without draws (empty present).
- Draws that span multiple flushes (state changes between draws).
- Target texture switching (which flushes the queue and changes the render target).
- Viewport/clip changes that do not affect the prefix but affect draw coordinates.

The current prototype handles some of these cases but not all. The initialization bug is the most immediate cause, but the architectural approach is fragile.

---

## 7. Minimal Source Correction

### 7.1 Fix `active_list_type` Initialization

In `DC_PVR_CreateRenderer()`, under `DC_PVR_OPAQUE_PREFIX_ROUTING`:

```c
#if defined(DC_PVR_OPAQUE_PREFIX_ROUTING)
    data->opaque_prefix_open = SDL_TRUE;
    data->active_list_type = PVR_LIST_NONE;  // ADD THIS LINE
#else
    data->active_list_type = PVR_LIST_TR_POLY;
#endif
```

### 7.2 Fix `DC_PVR_BeginList()` Early-Return Verification

Add a KOS state check to the early return:

```c
static int DC_PVR_BeginList(SDL_Renderer *renderer, int list_type)
{
    DC_PVR_RenderData *data = (DC_PVR_RenderData *)renderer->driverdata;

    if (data->active_list_type == list_type) {
        // Verify KOS state matches renderer-local state
        if (pvr_state.list_reg_open == list_type)
            return 0;  // Already open in KOS
        // Mismatch — fall through to open the list
    }

    if (data->list_active) {
        if (pvr_list_finish() < 0) {
            data->pvr_failed = SDL_TRUE;
            return SDL_SetError("Could not finish the Dreamcast PVR list during opaque-prefix transition");
        }
        data->list_active = SDL_FALSE;
        data->active_list_type = PVR_LIST_NONE;
    }

    data->header_valid = SDL_FALSE;

    if (pvr_list_begin(list_type) < 0) {
        data->pvr_failed = SDL_TRUE;
        return SDL_SetError("Could not begin the Dreamcast PVR list");
    }
    data->list_active = SDL_TRUE;
    data->active_list_type = list_type;
    return 0;
}
```

### 7.3 Fix Test Return Value Checking

In `pvr_list_routing.c`, check `SDL_RenderClear()` and `SDL_RenderFillRect()` return values:

```c
if (SDL_RenderClear(renderer) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RenderClear failed: %s", SDL_GetError());
    return -1;
}
```

---

## 8. Recommended Path Forward

### 8.1 Immediate: NO-GO for Current Prototype

The current prototype should not be merged. The initialization bug is a clear defect, but the architectural approach has fundamental flaws:

1. **SDL painter order is not guaranteed across flush boundaries.** The opaque prefix assumes all opaque draws come before translucent draws within a single frame. SDL's command queue does not guarantee this across `SDL_RenderPresent()` calls.

2. **The prefix state machine is fragile.** If the frame contains no textured draws, the prefix is never closed. If the frame contains a textured draw followed by an opaque draw, the opaque draw must go to TR (correct) but the prefix was already closed (also correct). The state machine handles this but only by accident.

3. **The prototype does not handle empty presents.** Multiple `SDL_RenderPresent()` calls without draws must still produce a valid frame. The prototype's `DC_PVR_FinishCurrentList()` handles this by closing the active list and calling `pvr_scene_finish()`, but the opaque prefix state is reset without a corresponding frame boundary.

### 8.2 Alternative Architectures

**Option A: Opaque prefix per flush, not per frame.**
- Open OP for the first opaque draw in each flush.
- Close OP at the end of the flush (before returning from `RunCommandQueue`).
- Open TR for translucent draws in the same flush.
- **Problem:** Each flush would need to close and reopen the OP list, which is expensive.

**Option B: Opaque prefix per `SDL_RenderPresent()`.**
- Track the prefix open state per frame, not per flush.
- Reset the prefix at `SDL_RenderPresent()`.
- **Problem:** SDL's command queue does not provide a "frame boundary" callback. The prefix must be managed from `RunCommandQueue()` and `RenderPresent()`.

**Option C: No opaque prefix. Route all draws through TR.**
- Skip the opaque prefix entirely.
- Route all draws through TR with depth comparison ALWAYS and depth writes disabled.
- **Problem:** This loses the performance benefit of the OP list (no depth writes, earlier culling).

**Option D: Redesign the opaque prefix state machine.**
- Track the prefix open state explicitly.
- Close the prefix when a translucent draw is encountered.
- Reopen the prefix at `SDL_RenderPresent()` (start of new frame).
- Handle the case where no draws occur (empty present).
- Handle the case where the prefix is already open (no-op).
- **This is the most complex option but the most correct.**

### 8.3 Recommendation

**Do not merge the current prototype.** The initialization bug is a clear defect, but the architectural approach has fundamental flaws that require a redesign. The opaque prefix concept is sound (PVR OP list has real performance benefits), but the SDL painter-order-preserving opaque prefix requires a more robust state machine.

**Recommended next steps:**

1. **Fix the initialization bug** (`active_list_type = PVR_LIST_NONE`) as a minimal correction.
2. **Add KOS state verification** to `DC_PVR_BeginList()` to prevent future mismatches.
3. **Fix the test** to check SDL render return values.
4. **Rethink the opaque prefix state machine** before any further development.

---

## 9. Remaining Open Questions

1. **Does `pvr_scene_finish()` handle the case where the OP list is the last-opened list?** If the prefix is never closed (no textured draws in the frame), `DC_PVR_FinishCurrentList()` closes the OP list. `pvr_scene_finish()` fills unsubmitted TR lists with blank headers. This is acceptable but wastes a TR list.

2. **Does the prototype handle the case where `SDL_RenderPresent()` is called with no draws?** `DC_PVR_FinishCurrentList()` returns 0 (no active list). `pvr_scene_finish()` fills unsubmitted lists with blank headers. This is acceptable.

3. **What is the TA/OPB capacity impact of the OP list?** The prototype enables `PVR_BINSIZE_16` for the OP list. A focused capacity test should measure the impact.

4. **Does the prototype handle target texture switching correctly?** Target texture switching flushes the queue and changes the render target. The prototype must handle this without corrupting the opaque prefix state.

---

## 10. Handoff Summary

**Root cause:** `data->active_list_type` is not initialized under `DC_PVR_OPAQUE_PREFIX_ROUTING`. `SDL_calloc()` zeros it to 0, which equals `PVR_LIST_OP_POLY`. The first `DC_PVR_BeginList(renderer, PVR_LIST_OP_POLY)` call sees a false match and returns early without opening the list in KOS. `pvr_prim()` fails with "attempt to submit to unopened list."

**Immediate fix:** Initialize `data->active_list_type = PVR_LIST_NONE` in `DC_PVR_CreateRenderer()` under the guard.

**Post-fix verdict:** GO for the narrow guarded prototype only. The corrected
state machine passed the focused physical test; broader capacity, performance,
render-target, and punch-through questions remain deferred.

**Next steps:** Measure OPB/TA capacity and performance, then add separately
guarded lifecycle and render-target interaction coverage before considering
promotion.
