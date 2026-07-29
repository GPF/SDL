# Dreamcast Native PVR Renderer: Phase 5 Render-to-Texture Research

**Status:** Guarded prototype physically tested and rejected. Real Dreamcast
captures prove that both screen and RTT destination contents are replaced at
new PVR scene boundaries. Full SDL target-texture capability now requires an
exact preservation design before another coding pass.
**Date:** 2026-07-29
**Scope:** SDL target-texture contract, KOS PVR RTT API, scene ownership, texture representation, failure/lifecycle behavior, cross-backend review, implementation blueprint, validation design, and verdict.

This document defines the smallest correct Phase 5 render-to-texture
implementation for the native Dreamcast PVR renderer. It uses only public
KallistiOS/PVR APIs and does not route through OpenGL or GLdc.

**Related design:** `PVR_TEXTURE_RESEARCH.md` defines the established Phase 3
texture subsystem that Phase 5 extends with the `SDL_TEXTUREACCESS_TARGET`
access path. `PVR_BACKEND_FALLBACK_RESEARCH.md` defines the cross-backend
fallback policy and explicitly non-recommended techniques. `PVR_BLENDMODE_ADD_MOD_MUL_RESEARCH.md` defines the current RGB565 blend-mode contract that
Phase 5 must preserve for target destinations. `PHASE0_SKETCH.md` retains the
established Phase 0 invariants that Phase 5 must extend.

---

## 0. Independent-review corrections

The first draft established that KOS can render an RGB565 scene into texture
VRAM, but it overreached from that fact to complete SDL target semantics. The
following corrections supersede conflicting statements later in the original
research:

1. SDL does **not** reset the render target when a draw samples the selected
   target. The originally cited reset is in `SDL_DestroyTexture`, not
   `SDL_RenderCopy`. The native backend must reject direct feedback before it
   queues geometry.
2. SDL assigns `renderer->target` before calling the backend
   `SetRenderTarget`, and it does not restore the old pointer if the callback
   fails. The backend must validate a requested target before destroying the
   old usable state. A failure after a PVR scene transition is a renderer-fatal
   condition, not a normal rollback.
3. `pvr_scene_begin()` returns `void`. Only `pvr_scene_begin_rtt()` returns an
   error status.
4. An RTT scene must be finished when `SDL_RenderPresent` flushes drawing while
   a target remains selected. Finishing it does not present it to the display;
   it commits the queued target rendering. The selected target remains selected
   and the next draw begins another RTT scene lazily.
5. KOS already serializes the single render core. The next scene can be built
   by the TA while the previous RTT renders, but it cannot start rendering
   until `render_busy` clears. The KOS RTT examples do not insert
   `pvr_wait_render_done()` between their RTT and screen scenes. Do not add that
   unconditional stall without hardware evidence.
6. Most importantly, a new PVR screen or RTT scene has a new background plane.
   The first draft did not prove that prior destination contents survive
   `screen -> RTT -> screen`, target reselection, or a present while a target
   stays selected. Exact SDL semantics require destination preservation across
   those boundaries. This is now the primary hardware-prototype question.
7. SDL queues the new or restored viewport and clip immediately after the
   target callback. Because no draw occurs between the callback and those
   commands, separate backend copies of screen and target viewport/clip state
   are not initially required. The existing command-state capture remains the
   source of truth.

The safe next step is therefore a first coding pass that implements the basic
RGB565 RTT path and a focused persistence probe without treating the public
capability as complete or ready to commit.

---

## 1. SDL semantics and callback flow

### 1.1 Target-texture creation

**File:** `src/render/SDL_render.c` lines 1380-1450 (`SDL_CreateTexture`)

When `access == SDL_TEXTUREACCESS_TARGET`, SDL:

1. Validates `access == SDL_TEXTUREACCESS_TARGET` (line 1385).
2. Calls `IsSupportedFormat(renderer, format)` — returns `SDL_FALSE` if
   unsupported, triggering conversion-path handling.
3. Calls backend `CreateTexture` callback.
4. For native target textures, SDL stores `texture->access = SDL_TEXTUREACCESS_TARGET` and sets `texture->target = SDL_TRUE`.
5. For conversion-path targets (non-advertised format), SDL creates a native
   wrapper texture with matching alpha presence and stores `texture->native`.

**Implication for the PVR backend:** The backend receives `texture->access ==
SDL_TEXTUREACCESS_TARGET` directly. Reject `SDL_TEXTUREACCESS_TARGET`
explicitly with a useful error until Phase 5 is implemented.

### 1.2 SDL_SetRenderTarget flow

**File:** `src/render/SDL_render.c` lines 2303-2369 (`SDL_SetRenderTarget`)

The public behavior:

1. Calls `SDL_RenderTargetSupported(renderer)` — returns `SDL_Unsupported()` if
   `renderer->info.flags & SDL_RENDERER_TARGETTEXTURE` is not set or
   `renderer->SetRenderTarget` is NULL.
2. Validates `texture == NULL` is legal (means reset to window).
3. Validates `texture->renderer == renderer` and `texture->access ==
   SDL_TEXTUREACCESS_TARGET`.
4. If `texture->native` exists (conversion wrapper), replaces `texture` with
   `texture->native` (line 2320).
5. If `texture == renderer->target`, returns 0 immediately (no-op).
6. Calls `FlushRenderCommands(renderer)` — submits all queued geometry before
   the transition (line 2327).
7. Acquires `renderer->target_mutex`.
8. If transitioning from screen to target (`texture && !renderer->target`):
   - Backs up viewport, clip_rect, clipping_enabled, scale, logical_w, logical_h.
9. Sets `renderer->target = texture`.
10. Calls `renderer->SetRenderTarget(renderer, texture)` backend callback.
11. On success:
    - If target: sets viewport to `(0, 0, texture->w, texture->h)`, clears clip,
      disables clipping, sets scale to `(1.0, 1.0)`, sets logical dimensions to
      texture dimensions.
    - If NULL (reset to screen): restores backed-up viewport, clip, scale, and
      logical dimensions.
12. Releases mutex.
13. Calls `QueueCmdSetViewport` and `QueueCmdSetClipRect` — queuing viewport/
    clip changes for execution.
14. Calls `FlushRenderCommandsIfNotBatching(renderer)` — submits queued changes.

**Key invariant:** SDL flushes **before** the backend callback. The backend
callback runs with an empty command queue and no pending geometry.

**Failure correction:** SDL has already assigned `renderer->target` when the
callback runs. If the callback returns an error, SDL unlocks the mutex and
returns without restoring the previous pointer or state. The backend must do
all validation that can fail before finishing the old scene. A PVR failure
after the transition begins must mark the renderer failed; it cannot be
described as an ordinary reversible callback failure.

**Public behavior for `SDL_SetRenderTarget(NULL)`:** Restores the backed-up
screen viewport, clip, scale, and logical dimensions. This is a target-to-
screen transition.

### 1.3 Target-switch representation in the command stream

Target switches are **not** represented as a single SDL render command. They
occur through:

1. `FlushRenderCommands` — immediate submission of queued geometry.
2. Backend `SetRenderTarget` callback — backend state transition.
3. `QueueCmdSetViewport` and `QueueCmdSetClipRect` — queuing viewport/clip
   changes that follow the switch.

The backend must handle the transition as a **lifecycle event**, not as a
queued command. The current `RunCommandQueue` handles `SDL_RENDERCMD_SETVIEWPORT`
and `SDL_RENDERCMD_SETCLIPRECT` but does not handle a "target switch" command
because none exists.

### 1.4 Viewport and clip restoration

**File:** `src/render/SDL_render.c` lines 2336-2363

When switching from target back to screen:

- `renderer->viewport` is restored to `viewport_backup`.
- `renderer->clip_rect` is restored to `clip_rect_backup`.
- `renderer->clipping_enabled` is restored to `clipping_enabled_backup`.
- `renderer->scale` is restored to `scale_backup`.
- `renderer->logical_w` and `logical_h` are restored to `logical_w_backup` and
  `logical_h_backup`.

**Implication for the PVR backend:** The current `DC_PVR_RunCommandQueue`
captures `data->view_x/y/w/h` and `data->clip_rect` from
`SDL_RENDERCMD_SETVIEWPORT` and `SDL_RENDERCMD_SETCLIPRECT`. SDL queues the new
target state or restored screen state immediately after the callback, before
any application draw can be queued. The initial implementation should keep the
existing command-state model; it does not need separate backend copies of the
screen and target viewport/clip.

**Draw-color state:** SDL does **not** back up or restore draw color across
target switches. The current draw color remains unchanged. The backend should
not modify draw color during target transitions.

### 1.5 Destruction of a selected target

**File:** `src/render/SDL_render.c` lines 1495-1510 (`SDL_DestroyTexture`)

When destroying a texture that is currently selected as the render target:

1. Calls `FlushRenderCommands(renderer)` — submits any queued geometry.
2. If `texture == renderer->target`, calls `SDL_SetRenderTarget(renderer, NULL)`
   to reset the target to the screen (line 1498-1499).
3. Calls `renderer->DestroyTexture(renderer, texture)` backend callback.

**Implication:** The backend's `SetRenderTarget(NULL)` transition runs **before**
the destroy callback. The backend can assume the target is no longer selected
when `DestroyTexture` runs.

### 1.6 Destruction of a target with pending commands

**File:** `src/render/SDL_render.c` lines 1495-1510

SDL flushes before destruction. The backend can assume no queued commands
reference the texture being destroyed.

### 1.7 Self-sampling feedback

The first draft misidentified the selected-target branch in
`SDL_DestroyTexture` as an `SDL_RenderCopy` guard. The draw paths do not contain
an equivalent check. A selected target can reach `QueueGeometry` as the sampled
texture through `SDL_RenderCopy`, `SDL_RenderCopyEx`, or
`SDL_RenderGeometry`.

Reading and writing the same PVR texture in one RTT scene is undefined feedback
and is not part of the initial contract. `DC_PVR_QueueGeometry` must reject
`texture == renderer->target` before allocating command vertices, with a stable
error such as:

```text
Dreamcast PVR cannot sample the active render target
```

This is a backend validation rule, not an automatic switch to the screen.

### 1.8 Capability advertisement

**File:** `src/render/SDL_render.c` lines 2292-2297 (`SDL_RenderTargetSupported`)

```c
SDL_bool SDL_RenderTargetSupported(SDL_Renderer *renderer)
{
    if (!renderer || !renderer->SetRenderTarget) {
        return SDL_FALSE;
    }
    return (renderer->info.flags & SDL_RENDERER_TARGETTEXTURE) != 0;
}
```

SDL checks two things:

1. `renderer->SetRenderTarget` is installed (not NULL).
2. `renderer->info.flags & SDL_RENDERER_TARGETTEXTURE` is set.

**Implication:** The backend must install the `SetRenderTarget` callback and
advertise `SDL_RENDERER_TARGETTEXTURE` in `DC_RenderDriver.info.flags` only
after Phase 5 is implemented. Until then, reject `SDL_TEXTUREACCESS_TARGET` in
`CreateTexture` and do not advertise the flag.

---

## 2. KallistiOS/PVR RTT contract

### 2.1 API documentation

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr.h` lines 972-998

```c
int pvr_scene_begin_rtt(pvr_ptr_t txr, uint32_t render_w,
                        uint32_t render_h, uint32_t stride_px);
```

**Parameters:**

- `txr`: The texture to render to (VRAM pointer).
- `render_w`: Width of the render area (in pixels).
- `render_h`: Height of the render area (in pixels).
- `stride_px`: Backing texture pitch (in pixels). Must be `>= render_w`.

**Return value:**

- `0`: On success.
- `-1`: If the specified arguments are invalid.

**Documentation states:**

> The PVR will render into a region of render_w by render_h pixels, using
> stride_px pixels as the backing memory pitch.
>
> stride_px must be greater than or equal to render_w. For the initial 16-bit
> render target implementation, stride_px must also be a multiple of 4 pixels.
>
> Initial support is intended for 16-bit render targets, matching the
> deprecated pvr_scene_begin_txr() compatibility wrapper behavior.

**Key constraints:**

1. `stride_px >= render_w`.
2. `stride_px` must be a multiple of 4 pixels (16-bit = 2 bytes per pixel, so
   stride in bytes must be a multiple of 8 bytes).
3. 16-bit only (RGB565, ARGB1555, ARGB4444).
4. `pvr_scene_begin_txr()` is deprecated; use `pvr_scene_begin_rtt()` instead.

### 2.2 Implementation

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_scene.c` lines 200-220

```c
int pvr_scene_begin_rtt(pvr_ptr_t txr, uint32_t render_w,
                        uint32_t render_h, uint32_t stride_px) {
    if(!txr || render_w == 0 || render_h == 0 || stride_px < render_w ||
            (stride_px & 3))
        return -1;

    /* The existing render-to-texture path programs a 16-bit render pitch in
       64-bit units. Keep that behavior explicit for the sized RTT API. */
    pvr_state.next_to_txr_rp = stride_px * 2 / 8;
    pvr_state.next_to_txr_w = render_w;
    pvr_state.next_to_txr_h = render_h;
    pvr_state.next_to_txr_stride_px = stride_px;
    pvr_state.next_to_txr_addr = (uint32_t)(txr) - PVR_RAM_INT_BASE;

    pvr_scene_begin();

    pvr_state.next_to_texture = 1;

    return 0;
}
```

**Validation:**

- `txr != NULL`.
- `render_w > 0`, `render_h > 0`.
- `stride_px >= render_w`.
- `stride_px & 3 == 0` (stride must be multiple of 4 pixels).

**Internal state set:**

- `next_to_txr_rp`: Render pitch in 64-bit units = `stride_px * 2 / 8`. For
  16-bit, this is `stride_px / 4`.
- `next_to_txr_w`: Render width in pixels.
- `next_to_txr_h`: Render height in pixels.
- `next_to_txr_stride_px`: Stride in pixels.
- `next_to_txr_addr`: Texture address relative to PVR RAM base.
- `next_to_texture`: Flag indicating RTT mode is active.

**Key observation:** The implementation calls `pvr_scene_begin()` internally.
The RTT scene begins with the same call as the screen scene. The
`next_to_texture` flag distinguishes the two modes.

### 2.3 Pitch/stride requirements

**From implementation:**

- `stride_px` must be a multiple of 4 pixels.
- For 16-bit: stride in bytes = `stride_px * 2`, which must be a multiple of
  8 bytes.
- `stride_px >= render_w`.

**From KOS examples:**

**File:** `/opt/toolchains/dc/kos/examples/dreamcast/pvr/texture_render/texture_render.c` lines 180-182

```c
pvr_scene_begin_rtt(d_texture, 640, 480, tx_x);
```

Where `tx_x = 1024` (POT backing for 640x480).

**File:** `/opt/toolchains/dc/kos/examples/dreamcast/pvr/pvr_rtt_dt/pvr_rtt_dt.c` lines 27-29

```c
#define RTT_W 128
#define RTT_H 128
#define RTT_STRIDE_PX 128
```

Where RTT is 128x128 with stride 128 (POT backing, no padding).

**Implication:** The stride equals the backing width (POT). The render area
can be smaller than the backing (NPOT logical size within POT backing).

### 2.4 Destination alignment

**From implementation:** No explicit alignment check beyond `stride_px >= render_w`
and `stride_px % 4 == 0`. The texture pointer `txr` is not aligned-checked here
because `pvr_mem_malloc` returns 32-byte-aligned pointers.

**Implication:** The backend can use `pvr_mem_malloc` for RTT allocations. The
pointer is 32-byte aligned, which satisfies the PVR's alignment requirements.

### 2.5 Texture format/layout

**From documentation and implementation:**

- 16-bit only (RGB565, ARGB1555, ARGB4444).
- Nontwiddled (`PVR_TXRFMT_NONTWIDDLED`) is the initial supported layout.
- Twiddled textures are not explicitly tested but may work (Phase 6 optimization).

**From KOS example:**

**File:** `/opt/toolchains/dc/kos/examples/dreamcast/pvr/texture_render/texture_render.c` lines 117-120

```c
pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
                 PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, 1024, 512,
                 d_texture, PVR_FILTER_NONE);
```

The sampling context uses `PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED` for an
1024x512 texture (POT backing for 640x480 screen).

**Implication:** The backend should use `PVR_TXRFMT_NONTWIDDLED` for RTT targets.
The format matches the texture's SDL format (RGB565, ARGB1555, or ARGB4444).

### 2.6 Synchronization

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr.h` lines 1189-1212

```c
int pvr_wait_ready(void);
int pvr_wait_render_done(void);
```

- `pvr_wait_ready()`: Blocks until the PVR is ready for another frame. Waits
  for the rendered frame to complete and a vertical blank to happen.
- `pvr_wait_render_done()`: Blocks until the PVR has finished rendering the
  previous frame. Useful for updating textures after rendering.

**From KOS example:**

**File:** `/opt/toolchains/dc/kos/examples/dreamcast/pvr/pvr_rtt_dt/pvr_rtt_dt.c` lines 432-433

```c
pvr_wait_ready();
pvr_wait_render_done();
```

The example calls both after the main loop. `pvr_wait_ready()` waits for the
final frame to be displayed. `pvr_wait_render_done()` waits for the PVR to
finish rendering before cleanup.

**Corrected implication:** Do not insert an unconditional
`pvr_wait_render_done()` between an RTT scene and the screen scene that samples
it. `pvr_start_ta_rendering()` calls `pvr_wait_ready()` when the first list of
the next scene opens. The PVR IRQ path permits the TA to prepare that next
scene, but `pvr_render_ready()` will not start its render while
`render_busy` is true. The KOS `texture_render` and `pvr_rtt_dt` examples submit
RTT followed by screen without an explicit render-done wait. Keep the existing
`RenderPresent`/destruction wait for CPU-side resource safety; add another wait
only if the physical persistence probe demonstrates a real hazard.

### 2.7 Orientation and UV flip

**From KOS implementation:**

The PVR does not explicitly flip the RTT output vertically. The render area
is drawn with the same coordinate system as the screen (y-down). Sampling the
completed target should use the same UV convention as sampling a normal texture.

**From KOS example:**

**File:** `/opt/toolchains/dc/kos/examples/dreamcast/pvr/texture_render/texture_render.c` lines 76-85

```c
vert.u = 0.0f;
vert.v = 480.0f / 512.0f;
...
vert.u = 640.0f / 1024.0f;
vert.v = 480.0f / 512.0f;
```

The example samples the RTT texture with UVs in `[0,1]` range, with `v =
480/512` for the bottom edge. This suggests no vertical UV flip is needed.

**Implication:** The backend can sample the completed RTT target with standard
`[0,1]` UVs. No vertical flip is required.

### 2.8 Multiple screen→target→screen transitions per frame

**From KOS documentation and examples:**

The PVR supports multiple scene transitions within a single application frame.
The KOS example `pvr_rtt_dt.c` renders an RTT scene, then a screen scene,
within the same loop iteration. The PVR's double-buffering allows this.

**Corrected implication:** KOS can submit RTT and screen scenes consecutively,
but this proves scheduling only. It does not prove SDL destination persistence.
Each new scene has a background plane, so a second screen scene may replace the
visible result of the first instead of continuing it. Likewise, beginning a
new RTT scene for an existing target may replace its previous contents.

The first prototype may switch screen → target → screen mechanically. It must
capture and test whether earlier screen pixels and earlier target pixels remain
correct. Until that passes or an explicit preservation strategy is designed,
the transition is not a validated SDL feature. Mechanically, each transition
requires:

1. Finish the current scene/list.
2. Begin the next scene (screen or RTT).
3. Wait for completion if sampling the completed target.

---

## 3. Scene ownership redesign

### 3.1 Current invariants

**File:** `PHASE0_SKETCH.md`

- One scene per frame (begin → lists → finish → sync).
- One transparent polygon list (`PVR_LIST_TR_POLY`).
- `RunCommandQueue` owns scene begin/list/finish.
- `RenderPresent` owns sync/presentation.
- Queue callbacks are CPU-only; only `RunCommandQueue` submits PVR commands.
- Single autosort-disabled transparent list preserves SDL painter order.

### 3.2 Lazy RTT integration

Keep one lifecycle owner and extend it with only:

- `current_target`: backend record of the selected target, or NULL for screen;
- `scene_target`: target used by the active scene, or NULL for screen; and
- the existing `scene_active` and `list_active` flags.

An extra `is_rtt` flag is redundant when `scene_target != NULL` expresses the
same fact. Do not begin a new scene or polygon list inside `SetRenderTarget`.
That callback runs during SDL state transition, before SDL queues the new
viewport and clip. It should finish the old active scene, update the selected
target, invalidate the header cache, and return. The next nonempty
`RunCommandQueue` begins the correct scene lazily after consuming the queued
state commands.

### 3.3 Corrected state machine

```text
Initial:
  current_target = NULL
  scene_target = NULL
  !scene_active, !list_active

First screen draw:
  pvr_scene_begin()
  scene_target = NULL
  begin transparent list lazily

SetRenderTarget(target):
  prevalidate target completely
  finish active list/scene, if any
  current_target = target
  leave !scene_active and !list_active

First target draw:
  pvr_scene_begin_rtt(target->vram, logical_w, logical_h, backing_w)
  scene_target = target
  begin transparent list lazily

SetRenderTarget(NULL) or target B:
  finish active list/scene, if any
  mark the completed RTT allocation sampleable
  current_target = NULL or B
  leave the next scene unopened

RenderPresent with screen selected:
  finish the active screen scene, creating an empty screen scene only when
  needed to preserve the renderer's existing present behavior
  wait for render completion and release deferred resources

RenderPresent with a target selected:
  finish an active RTT scene so its queued work commits
  do not begin or present a screen scene
  wait for resource safety, but keep current_target selected
  the next target draw begins another RTT scene lazily
```

`pvr_scene_begin()` is `void`; do not test a return value. Validate the RTT
pointer, dimensions, and stride before ending the previous scene, then check
the return from `pvr_scene_begin_rtt()` when the lazy begin occurs.

### 3.4 Scene-finalization helper

Create one internal helper that:

1. finishes the list if `list_active`;
2. finishes the scene if `scene_active`;
3. records an RTT scene as completed/sampleable;
4. clears `list_active`, `scene_active`, and `scene_target`; and
5. invalidates the compiled-header cache.

The helper must not begin the next scene. A failure after PVR state has begun
changing sets `pvr_failed`; normal drawing must fail closed afterward.

### 3.5 Synchronization model

Opening the first list of the next scene reaches KOS
`pvr_start_ta_rendering()`, which performs the normal `pvr_wait_ready()` gate.
The render core starts queued scenes serially. Do not call
`pvr_wait_render_done()` on every target switch. Retain a final render-done
wait in `RenderPresent` and destruction before freeing allocations referenced
by completed scenes.

### 3.6 Unresolved destination persistence

Lazy scene ownership solves empty-scene and state-ordering problems, but not
destination preservation. The first coding pass must instrument and capture:

- screen draw S1 → target draw → screen draw S2, checking that S1 and S2 both
  survive in the final screen image;
- target draw T1 → switch away → select the target again → draw T2, checking
  that T1 and T2 both survive in the sampled target; and
- target draw T1 → `SDL_RenderPresent` while target remains selected → draw T2
  → switch to screen, checking target persistence across present.

If any earlier contents disappear, stop capability work and record the exact
hardware evidence. Candidate exact solutions include per-destination ping-pong
accumulation or a separately validated continuation mechanism, but neither is
part of the first coding pass and neither may be assumed feasible. Do not
silently weaken SDL's persistence semantics.

---

## 4. Texture representation

### 4.1 DC_PVR_TextureData additions

```c
typedef struct DC_PVR_TextureData
{
    ... existing fields ...
    SDL_TextureAccess access;
    ... existing fields ...
    int rtt_render_w;             // NEW: Render area width (may be < backing_w)
    int rtt_render_h;             // NEW: Render area height (may be < backing_h)
    int rtt_stride_px;            // NEW: RTT stride in pixels (>= render_w, multiple of 4)
    SDL_bool rtt_completed;       // NEW: Whether RTT scene has been finished
} DC_PVR_TextureData;
```

`access == SDL_TEXTUREACCESS_TARGET` already distinguishes target textures, and
the existing `vram` field owns the single prototype allocation. Do not add
redundant `is_target` or `rtt_vram` fields.

**New-field explanations:**

- `rtt_render_w/h`: The render area dimensions (may be NPOT within POT backing).
- `rtt_stride_px`: The backing stride in pixels (POT backing width, multiple of 4).
- `rtt_completed`: Tracks whether the RTT scene has been finished (for
  synchronization).

### 4.2 DC_PVR_RenderData additions

```c
typedef struct DC_PVR_RenderData
{
    ... existing fields ...
    SDL_Texture *current_target;  // NEW: Selected destination, NULL for screen
    SDL_Texture *scene_target;    // NEW: Destination of the active scene
} DC_PVR_RenderData;
```

**Field explanations:**

- `current_target`: Retains the old backend selection because SDL overwrites
  `renderer->target` before invoking `SetRenderTarget`.
- `scene_target`: Identifies the destination of an active scene. A non-NULL
  value means RTT; no separate `is_rtt` flag is needed.

### 4.3 Logical versus POT backing dimensions

**For target textures:**

- `logical_w/h`: The dimensions requested by `SDL_CreateTexture(access=TARGET, w, h)`.
- `backing_w/h`: POT backing dimensions (`DC_PVR_NextPowerOfTwo(w/h)`).
- `rtt_render_w/h`: The render area dimensions. For the initial implementation,
  `rtt_render_w == logical_w` and `rtt_render_h == logical_h`.
- `rtt_stride_px`: The backing stride in pixels. For POT backing,
  `rtt_stride_px == backing_w`.

**UV scaling:**

- Standard texture UV scaling: `u = sdl_u * logical_w / backing_w`, `v = sdl_v * logical_h / backing_h`.
- RTT sampling UV scaling: Same as standard texture (no special handling needed).

### 4.4 Allocation size and pitch

**For target textures:**

- `allocation_size = backing_w * backing_h * 2` (16-bit).
- `cpu_pitch = backing_w * 2` (bytes per row).
- `vram_pitch = cpu_pitch`.
- `rtt_stride_px = backing_w` (POT backing, multiple of 4 for backing_w >= 8).

**KOS constraint check:**

- `stride_px >= render_w`: `backing_w >= logical_w` (true by POT backing).
- `stride_px % 4 == 0`: `backing_w % 4 == 0` (true for backing_w >= 8).

### 4.5 CPU staging storage

**Recommendation:** Target textures do **not** need CPU staging storage.

**Rationale:**

- Target textures are written to by the PVR, not by CPU updates.
- `UpdateTexture` and `LockTexture` are not used for target textures (SDL
  prevents this).
- The backend does not need to upload target texture contents; the PVR writes
  them directly.

**Exception:** If the backend needs to read back target texture contents
(`RenderReadPixels`), CPU staging may be needed. This is out of scope for
Phase 5.

The creation path must branch before allocating staging memory. A target's
initial contents are undefined by this prototype. Set `uploaded`/sampleable
state consistently so `DC_PVR_EmitHeader` can sample a completed target without
requiring a CPU upload; do not leave target textures permanently rejected by
the existing `!uploaded` check.

### 4.6 UpdateTexture, LockTexture, and target access rules

**For target textures:**

- `UpdateTexture`: SDL does not reject target access. The call can reach the
  backend after flushing commands that reference the texture. The staging-free
  prototype must explicitly reject target updates with a stable error; it must
  not dereference a NULL staging pointer. Full support is a separate design
  problem because a partial CPU update after RTT must preserve GPU-rendered
  texels outside the rectangle while the CPU has no current mirror.
- `LockTexture`: Not used. SDL prevents calling `SDL_LockTexture` on a target
  texture with `SDL_TEXTUREACCESS_STREAMING` (target textures use
  `SDL_TEXTUREACCESS_TARGET`, not `SDL_TEXTUREACCESS_STREAMING`).
- **Self-sampling guard:** `DC_PVR_QueueGeometry` rejects the sampled texture
  when it equals `renderer->target`. SDL does not perform this check.

The first prototype may reject target updates, but ordinary capability
advertisement must document and review this SDL-visible limitation. Do not
claim that SDL core prevents the call.

### 4.7 Header-cache invalidation

**Across target switches:** Explicitly clear `header_valid` whenever the active
scene is finalized or the selected target changes. The cache tracks the sampled
texture, not the destination, so changing the render target does not
automatically invalidate it.

**After completed RTT writes:**

- When sampling a completed RTT target, the backend must ensure the header
  cache does not reference stale state.
- The header cache is invalidated when the target texture is destroyed or when
  its state (filter, blend) changes.

### 4.8 ADD, BLEND, and MUL on RGB565 targets

**From `PVR_BLENDMODE_ADD_MOD_MUL_RESEARCH.md`:**

- **ADD:** `SRCALPHA / ONE` — exact for RGB565 destinations.
- **BLEND:** `SRCALPHA / INVSRCALPHA` — exact for RGB565 destinations (alpha
  equation irrelevant).
- **MUL:** `DSTCOLOR / INVSRCALPHA` — exact for RGB565 destinations (algebraically
  preserves destination alpha).
- **MOD:** Rejected (missing `SRCCOLOR` factor).

**Recommendation:** The current blend-mode mappings remain valid for RGB565
targets. No changes to `DC_PVR_EmitHeader` are needed for Phase 5.

### 4.9 ARGB1555/ARGB4444 target support

**Recommendation:** Phase 5 supports **RGB565 targets only**.

**Rationale:**

- The initial implementation locks target formats to RGB565 to preserve ADD
  and current BLEND semantics.
- ARGB1555/ARGB4444 targets would require alpha-equation validation, which is
  out of scope for Phase 5.
- Future increments can add ARGB target support after alpha-equation validation.

---

## 5. Failure and lifecycle behavior

### 5.1 Target allocation failure

**Scenario:** `pvr_mem_malloc` fails for a target texture.

**Rollback:**

1. `DC_PVR_CreateTexture` returns `SDL_OutOfMemory()`.
2. `texture->driverdata` is NULL (allocation failed before setting it).
3. SDL does not call `DestroyTexture` (driverdata is NULL).
4. No leaked allocations.

### 5.2 Scene-start failure

**Scenario:** `pvr_scene_begin_rtt` returns -1.

SDL does not restore `renderer->target` after a callback failure. The corrected
design therefore prevalidates the target pointer, access, format, dimensions,
stride, and backend data before finalizing the old scene. `SetRenderTarget`
then records the selection without starting a scene. If the later lazy
`pvr_scene_begin_rtt` unexpectedly fails, mark `pvr_failed` and fail closed;
there is no safe claim that the old scene was restored.

### 5.3 Destruction of selected target

**From SDL flow (Section 1.5):**

1. SDL calls `SDL_SetRenderTarget(renderer, NULL)` to reset target to screen.
2. Backend's `SetRenderTarget(NULL)` transition runs (finishes RTT scene, begins screen scene).
3. SDL calls `renderer->DestroyTexture(renderer, texture)`.
4. Backend destroys the texture (frees VRAM, invalidates header cache).

**Implication:** The backend can assume the target is no longer selected when
`DestroyTexture` runs.

### 5.4 Destruction of pending/completed targets

**Pending target (RTT scene not yet finished):**

- Backend finishes the RTT scene during the target-to-screen transition.
- Texture destruction runs after the transition (see Section 5.3).

**Completed target (RTT scene finished, not yet sampled):**

- Texture destruction runs normally (frees VRAM, invalidates header cache).
- No special handling needed.

### 5.5 Renderer destruction with active RTT scene

**Scenario:** Renderer is destroyed while an RTT scene is active.

**Rollback:**

1. `DC_PVR_DestroyRenderer` finishes the active scene/list (if any).
2. `DC_PVR_DestroyRenderer` releases deferred textures/VRAM.
3. `DC_PVR_DestroyRenderer` shuts down PVR.

**Implication:** Reuse the corrected common scene-finalization helper from
destruction. It must understand `scene_target`, finish an RTT scene without
opening a screen scene, and wait before freeing target VRAM.

### 5.6 Repeated renderer creation/destruction

**Scenario:** Renderer is created, used for RTT, destroyed, recreated.

**Behavior:**

1. First renderer: RTT works, destruction cleans up.
2. Second renderer: PVR is reinitialized, RTT works again.

**Implication:** No special handling needed. The current lifecycle stress test
(validated in Phase 3.3) covers this.

### 5.7 VRAM exhaustion

**Scenario:** `pvr_mem_malloc` fails for a target texture due to VRAM exhaustion.

**Behavior:**

1. `DC_PVR_CreateTexture` returns `SDL_OutOfMemory()`.
2. Application can free existing textures and retry.
3. No corruption to existing textures.

**Implication:** The current allocation-failure handling (validated in Phase 3.3)
covers target textures.

### 5.8 Unsupported feedback loops

**Direct self-sampling:** Reject in `DC_PVR_QueueGeometry` when the sampled
texture equals `renderer->target`. This covers copies, copy-ex, and public
geometry after SDL translates them to the common callback.

**Indirect feedback:** Target A may sample a completed target B. That is valid
after B's scene is finalized. Target A must not alias B's VRAM allocation.

**Target persistence:** Selecting the same target again is not feedback, but it
requires prior contents to survive. This remains a hardware-prototype gate,
not an implicitly supported case.

---

## 6. Cross-backend review

### 6.1 OpenGL/GLES

**File:** `src/render/opengl/SDL_render_gl.c` lines 1580-1610 (`GL_SetRenderTarget`)

**Pattern:**

1. `GL_SetRenderTarget` binds/unbinds FBO.
2. On target switch, marks viewport dirty (`data->drawstate.viewport_dirty = SDL_TRUE`).
3. On target creation, creates FBO and attaches texture.
4. On target destruction, deletes FBO.

**Useful ideas:**

- Viewport dirty flag triggers viewport re-application at next draw.
- FBO lifecycle matches texture lifecycle (create/destroy).

**Do not copy:**

- OpenGL FBO API (PVR has no FBO equivalent).
- OpenGL texture format validation (PVR validates in `pvr_scene_begin_rtt`).

### 6.2 D3D9/D3D11/D3D12

**Pattern:**

- Render target is a surface/slot binding.
- On target switch, ends current render pass and begins new one.
- On target creation, creates render-target view.
- On target destruction, releases render-target view.

**Useful ideas:**

- Render pass lifecycle matches target lifecycle.
- Target switch requires ending/committing current pass.

**Do not copy:**

- D3D render-pass API (PVR has no render-pass equivalent).
- D3D render-target view (PVR uses direct texture binding).

### 6.3 Metal

**Pattern:**

- Render encoder ends/commits on target switch.
- New render encoder begins for new target.
- Target switch is a lifecycle operation, not a queued command.

**Useful ideas:**

- Target switch is a lifecycle operation (matches PVR scene ownership).
- Encoder lifecycle matches target lifecycle.

**Do not copy:**

- Metal render encoder API (PVR has no encoder equivalent).

### 6.4 PSP

**File:** `src/render/psp/SDL_render_psp.c` lines 950-970 (`PSP_SetRenderTarget`)

**Pattern:**

- `PSP_SetRenderTarget` returns 0 immediately (stub).
- Target textures are not actually implemented.
- Renderer advertises `SDL_RENDERER_TARGETTEXTURE` but does not handle it.

**Useful ideas:** None (stub implementation).

**Do not copy:**

- PSP's advertised-but-unimplemented target support.
- PSP's lack of target lifecycle handling.

### 6.5 PS2

**Pattern:**

- Similar to PSP (approximate target support).
- Does not fully support MOD/MUL blend modes.

**Useful ideas:** None (approximate implementation).

**Do not copy:**

- PS2's approximate blend mappings.
- PS2's incomplete target support.

### 6.6 Vita GXM

**Pattern:**

- Render target is a surface binding.
- On target switch, ends current render and begins new one.
- On target creation, creates surface and registers it.
- On target destruction, releases surface.

**Useful ideas:**

- Target switch requires ending/committing current render.
- Surface lifecycle matches texture lifecycle.

**Do not copy:**

- Vita GXM surface API (PVR has no surface equivalent).

### 6.7 Summary of useful patterns

1. **Target switch is a lifecycle operation:** Not a queued command. Backend
   must handle it as a scene/list transition.
2. **Viewport/clip restoration:** SDL backs up, restores, and requeues this
   state across target switches. The backend should continue consuming those
   commands rather than maintaining a second restoration system.
3. **Texture lifecycle matches target lifecycle:** Create/destroy target
   textures manages FBO/surface/texture resources.
4. **Render pass/end/commit on switch:** Backend must finish current scene/list
   before switching targets.

### 6.8 Explicit non-recommendations

1. **Do not copy OpenGL/D3D/Metal/Vita APIs:** PVR has no FBO, render pass,
   encoder, or surface equivalent.
2. **Do not copy PSP/PS2 stubs:** Advertised-but-unimplemented target support
   is not a useful precedent.
3. **Do not copy approximate blend mappings:** PS2's approximate MOD/MUL is not
   a correctness precedent.
4. **Do not route through GLdc:** Native PVR backend must use public KOS/PVR
   APIs only.

---

## 7. Implementation blueprint

### 7.1 Renderer callbacks to add

1. **`SetRenderTarget`**: Finalize the old active scene and record the new
   selected destination without beginning a new scene.
2. **`QueueGeometry` validation**: Reject sampling the currently selected
   target before allocating queued vertices.

```c
static int DC_PVR_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture);
```

### 7.2 Source files to modify

1. **`src/render/dreamcast/SDL_render_dreamcast_pvr.c`**:
   - Add `DC_PVR_SetRenderTarget` callback.
   - Add common scene-finalization and target-aware lazy-begin helpers.
   - Finish an active RTT scene from `RenderPresent` without presenting it.
   - Install callback in `DC_PVR_CreateRenderer`.
   - Advertise `SDL_RENDERER_TARGETTEXTURE` only in the guarded prototype
     configuration until persistence passes.

2. **`src/render/dreamcast/SDL_render_dreamcast_pvr_data.h`**:
   - Add `rtt_render_w`, `rtt_render_h`, `rtt_stride_px`, and `rtt_completed`
     to `DC_PVR_TextureData`.
   - Add `current_target` and `scene_target` to `DC_PVR_RenderData`.

3. **`src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c`**:
   - Update `DC_PVR_CreateTexture` to accept `SDL_TEXTUREACCESS_TARGET` (RGB565 only).
   - Skip CPU staging for target textures and set their sampleable state
     consistently.
   - Update `DC_PVR_DestroyTexture` to handle target textures.

4. **`src/render/dreamcast/SDL_render_dreamcast_pvr_geometry.c`**:
   - Reject direct active-target feedback in `DC_PVR_QueueGeometry`.

### 7.3 Scene-manager state transitions

Implement three helpers rather than duplicating transitions:

1. `DC_PVR_FinishActiveScene(renderer)`: finish active list/scene, mark an RTT
   allocation completed, clear scene state, and invalidate the header cache.
2. `DC_PVR_BeginSceneForCurrentTarget(renderer)`: if no scene is active, call
   `pvr_scene_begin()` for screen or `pvr_scene_begin_rtt()` for the selected
   target, then record `scene_target`. Do not open the polygon list until a
   drawable command needs it.
3. `DC_PVR_SetRenderTarget(renderer, texture)`: prevalidate `texture`, finish
   the active scene, and assign `current_target`. It does not begin a scene.

The current `DC_PVR_BeginFrame` should become target-aware and retain lazy list
opening. `RunCommandQueue` continues to own actual primitive submission.

### 7.4 Texture-data fields

**DC_PVR_TextureData additions:**

```c
int rtt_render_w;
int rtt_render_h;
int rtt_stride_px;
SDL_bool rtt_completed;
```

**DC_PVR_RenderData additions:**

```c
SDL_Texture *current_target;
SDL_Texture *scene_target;
```

### 7.5 Validation/error checks

**DC_PVR_CreateTexture:**

```c
if (texture->access == SDL_TEXTUREACCESS_TARGET) {
    if (texture->format != SDL_PIXELFORMAT_RGB565) {
        return SDL_SetError("Dreamcast PVR target textures require RGB565 format");
    }
    texturedata->rtt_render_w = texture->w;
    texturedata->rtt_render_h = texture->h;
    texturedata->rtt_stride_px = texturedata->backing_w;
    texturedata->rtt_completed = SDL_FALSE;
}
```

**DC_PVR_SetRenderTarget:**

- Prevalidate backend data, RGB565 format, dimensions, stride, and VRAM before
  finishing the active scene.
- Finish the active scene and record the selection; do not begin a new one.

**DC_PVR_QueueGeometry:**

- Reject `texture && texture == renderer->target` before allocating vertices.

**DC_PVR_RenderPresent:**

- Finish whichever scene is active.
- If screen is selected, preserve the existing screen-present behavior.
- If a target is selected, commit that RTT scene without opening a screen
  scene or changing the selected target.
- Perform the existing final synchronization and deferred-resource release.

### 7.6 Capability advertisement timing

**Guarded prototype:**

Use one private compile-time guard named
`DC_PVR_RENDER_TARGET_PROTOTYPE`. Under that guard only:

1. install `SetRenderTarget`;
2. accept RGB565 target textures; and
3. add `SDL_RENDERER_TARGETTEXTURE` to the renderer flags.

Build the prototype SDL archive in a separate build directory with that macro
in its compiler flags. Do not reuse or overwrite the ordinary validated
`build-scripts/dcbuild` archive for the hardware probe. With the guard absent,
the ordinary build must continue rejecting target access and must not install
or advertise the callback.

**Phase 5 validation:**

1. Physical-hardware tests pass (Section 8).
2. No regression in existing tests (Phase 0-4).

**Phase 5 completion:**

1. All persistence probes pass, or an exact preservation design is implemented
   and physically validated.
2. Existing native-PVR tests remain passing on hardware.
3. Only then advertise `SDL_RENDERER_TARGETTEXTURE` in the ordinary build and
   document the RGB565-only target contract.

### 7.7 Smallest safe commit boundaries

**Coding pass 1: Guarded prototype, no commit**

- Add minimal fields, target allocation, lazy target-aware scene ownership,
  feedback rejection, and the focused persistence test.
- Keep the work uncommitted until physical screenshots establish behavior.

**Commit 1: Proven basic RTT path**

- Only after basic target render/sample, orientation, state, and lifecycle pages
  pass on hardware.
- Keep ordinary capability advertisement disabled if persistence still fails.

**Commit 2: Exact persistence strategy, if required**

- Implement separately because ping-pong accumulation or continuation changes
  resource ownership and performance materially.
- Measure VRAM and scene costs and rerun the persistence pages.

**Commit 3: Capability advertisement**

- Enable the flag in ordinary builds only after the complete focused hardware
  gate passes.

---

## 8. Focused physical-hardware validation design

### 8.1 Test directory

```
test/dreamcast/test/pvr_render_target/
├── Makefile.dc
├── pvr_render_target.c
└── README.md
```

### 8.2 Test coverage

**API behavior:**

- `SDL_CreateTexture(access=TARGET)` succeeds for RGB565.
- `SDL_CreateTexture(access=TARGET)` fails for ARGB1555/ARGB4444.
- `SDL_SetRenderTarget(renderer, target)` succeeds.
- `SDL_SetRenderTarget(renderer, NULL)` succeeds (reset to screen).
- `SDL_SetRenderTarget(renderer, NULL)` restores viewport/clip/scale.
- `SDL_UpdateTexture` on a prototype target fails cleanly with the documented
  backend error and does not damage later target rendering.
- `SDL_LockTexture` on target access is rejected by SDL core.

**Rendering into target:**

- Render solid rectangles into target.
- Render textured quads into target.
- Render with NONE/BLEND/ADD/MUL into target.

**Sampling completed target:**

- Sample completed target on screen.
- Sample target after returning to screen.
- Sample target with nearest/linear filtering.
- Sample target with color/alpha modulation.

**Target orientation:**

- Verify all four corners render correctly.
- Verify no vertical UV flip is needed.

**Clear/viewport/clip with target dimensions:**

- Clear target to solid color.
- Set viewport to target dimensions.
- Set clip rectangle to target dimensions.

**Screen → target → screen ordering:**

- Draw unique S1 markers on screen, switch to a target and render, switch back,
  then draw nonoverlapping S2 markers.
- The final capture must contain both S1 and S2. Missing S1 proves that a new
  screen scene does not preserve the earlier destination.

**Target A → target B → screen:**

- Create two targets (A and B).
- Render to A, switch to B, render, switch to screen, render.
- Verify both targets contain correct content.

**Repeated target recreation:**

- Create target, render, destroy, recreate, render again.
- Verify no VRAM leaks.

**Nearest and linear sampling:**

- Sample completed target with nearest filtering.
- Sample completed target with linear filtering.
- Verify filtering behavior.

**Color/alpha modulation:**

- Sample completed target with color modulation.
- Sample completed target with alpha modulation.
- Verify modulation behavior.

**NONE, BLEND, ADD, MUL on RGB565 targets:**

- Draw with NONE into target.
- Draw with BLEND into target.
- Draw with ADD into target.
- Draw with MUL into target.
- Verify blend behavior.

**Rejected self-sampling feedback:**

- While target A is selected, attempt to copy or geometrically sample A.
- Require failure with the backend's stable feedback error, require A to remain
  selected, then perform a valid draw to prove recovery.

**Target persistence across reselection:**

- Draw unique T1 markers into A, switch away, reselect A, and add nonoverlapping
  T2 markers.
- Sample A on screen and require both T1 and T2.

**Target persistence across present:**

- Draw T1 into A and call `SDL_RenderPresent` while A remains selected.
- Draw T2 without changing the selected target, switch to screen, and sample A.
- Require both T1 and T2 and verify that presenting did not silently reset the
  target.

**Selected-target destruction/lifecycle:**

- Select target, destroy it.
- Verify SDL resets target to screen before destruction.
- Verify no crashes or leaks.

**Recovery frames after scene transitions:**

- Render to target, switch to screen, render.
- Verify screen render is not corrupted by RTT scene.

**Guard geometry:**

- Draw bright contrasting geometry into target.
- Sample target on screen.
- Verify no padding leakage, stale tiles, or vertical inversion.

### 8.3 Test program structure

**Prototype pages:**

1. **API precheck page:** Verify API behavior (create/reject, set/get target).
2. **Target rendering page:** Render into target, sample on screen.
3. **Screen persistence page:** S1 → target → S2, with disjoint markers.
4. **Target A → B → screen page:** Two targets, switch between them.
5. **Blend mode page:** NONE/BLEND/ADD/MUL into target.
6. **Filtering/modulation page:** Nearest/linear, color/alpha modulation.
7. **Target reselection persistence page:** T1 → away → A again → T2.
8. **Target-present persistence page:** T1 → present while selected → T2.
9. **Feedback/lifecycle page:** Explicit self-feedback rejection, recovery,
   repeated create/destroy, and selected-target destruction.
10. **Guard geometry page:** Bright contrasting geometry, verify no corruption.

**Each page:**

- Present independently.
- Wait at least three VBLs before capture.
- Write unique `/pc` PPM path.
- Hold page long enough for physical observation (8 seconds).

### 8.4 Capture requirements

- One raw hardware framebuffer image per visually distinct page.
- Unique `/pc` PPM paths (e.g., `/pc/pvr_render_target-page1.ppm`).
- Retain complete serial output.
- Rebuild the ordinary non-prototype SDL archive and ELF afterward. They must
  retain the pre-Phase-5 target capability behavior and contain no screenshot
  symbols or `/pc` paths.

### 8.5 Hardware test plan

**Platform:** Real Dreamcast at 640x480 VGA.

**Upload:** `kos-tool -t 192.168.0.128 -x pvr_render_target.elf -m .`

**Capture:** `vid_screen_shot("/pc/pvr_render_target-pageN.ppm")`

**Validation:**

- Direct inspection of each PPM against documented expectations.
- Verify painter order, blend modes, filtering, modulation, orientation.
- Verify no corruption, blanking, or stale state.
- Verify program returns zero and prints PASS.

---

## 9. Verdict

### 9.1 Feasibility verdict

**Basic RGB565 RTT is feasible; complete SDL target semantics are not yet
established.** KOS provides the required render-to-texture primitive, and the
native renderer can prototype target allocation, rendering, and later sampling.
However, destination persistence across multiple PVR scenes is a correctness
gate. Capability advertisement is not ready for the ordinary build.

### 9.2 Exact first-prototype contract

- **Format:** RGB565 only.
- **Access:** `SDL_TEXTUREACCESS_TARGET`.
- **Backing:** POT (power-of-two) dimensions.
- **Logical size:** NPOT logical dimensions within POT backing (UV scaling).
- **Filtering:** Nearest and linear (inherited from texture subsystem).
- **Blend modes:** NONE, BLEND, ADD, MUL (inherited from blend-mode subsystem).
- **MOD:** Rejected (same as screen path).
- **ArGB1555/ARGB4444 targets:** Not supported in Phase 5.
- **Direct self-feedback:** Rejected by the backend.
- **Capability status:** Test-only/guarded until persistence passes.

### 9.3 Unresolved hardware questions

1. **Screen persistence:** Does S1 survive a target excursion before S2 is
   submitted in a new screen scene?
2. **Target persistence:** Do T1 contents survive target reselection and a
   present while the target stays selected?
3. **RTT orientation:** KOS examples and implementation suggest no vertical UV
   flip is needed. Physical-hardware validation will confirm.
4. **RTT format:** KOS documentation states "16-bit only". RGB565, ARGB1555,
   ARGB4444 are all 16-bit. Phase 5 locks to RGB565 for ADD/BLEND coherence.
5. **RTT synchronization:** KOS render serialization should make an explicit
   target-switch wait unnecessary. Physical hardware must confirm that the
   following screen scene samples completed RTT pixels without corruption.

### 9.4 Implementation risks

**HIGH:**

1. **Destination persistence:** A new scene may replace prior screen or target
   contents. This blocks full SDL semantics until proven or solved exactly.
2. **Scene boundary correctness:** Missing a scene finish/begin transition can
   corrupt the PVR state. Mitigation: validate each transition with physical
   hardware.
3. **Callback failure atomicity:** SDL does not restore `renderer->target` when
   the backend callback fails. Mitigation: prevalidate before finalization and
   fail the renderer closed after a PVR transition error.
4. **Partial target updates:** SDL permits `SDL_UpdateTexture` on target access,
   but a staging-free target cannot preserve GPU-written texels during a later
   partial CPU update. The prototype rejects this safely; full capability must
   either retain that documented limitation or add an exact update design.

**MEDIUM:**

1. **RTT stride validation:** Failing to validate `stride_px >= render_w` and
   `stride_px % 4 == 0` can cause `pvr_scene_begin_rtt` to fail. Mitigation:
   validate in `SetRenderTarget` callback.
2. **Target texture allocation:** Failing to allocate VRAM for target can cause
   `SDL_OutOfMemory`. Mitigation: validate allocation with physical hardware
   under VRAM pressure.
3. **Present while target selected:** The RTT scene must commit without opening
   or presenting a screen scene, and subsequent draws must retain the target
   selection.

**LOW:**

1. **Header cache invalidation:** Failing to invalidate header cache across
   target switches can cause stale state. Mitigation: explicitly invalidate it
   whenever a scene ends or the destination changes.
2. **Deferred texture release:** Failing to release deferred textures after
   RTT scene finish can cause VRAM leaks. Mitigation: reuse existing deferred
   texture release path.

### 9.5 Whether code implementation is ready to begin

**A first guarded coding/prototype pass is ready. Full implementation is not.**
The prototype must gather the hardware persistence evidence in Section 8. Do
not commit ordinary capability advertisement merely because the basic RTT page
renders.

### 9.6 Explicit reasons if more hardware research is required first

No additional pre-code research is required for the bounded prototype. Physical
hardware research is required immediately after that first pass to answer
screen and target persistence. If persistence fails, summarize the evidence
before designing ping-pong accumulation, continuation, or another exact path.

---

## 10. Conclusion

KOS RGB565 render-to-texture is feasible, but a complete SDL target backend is
not yet proven. The safe next action is a guarded first coding pass with lazy
scene creation and hardware persistence probes:

1. Add `SetRenderTarget` callback for scene transitions.
2. Add new fields to texture/render data structures.
3. Accept `SDL_TEXTUREACCESS_TARGET` in `CreateTexture` (RGB565 only).
4. Reject active-target feedback in `QueueGeometry`.
5. Advertise `SDL_RENDERER_TARGETTEXTURE` only in the prototype configuration.

The implementation must:

- Validate KOS RTT constraints (`stride_px >= render_w`, `stride_px % 4 == 0`).
- Continue consuming SDL's queued viewport/clip restoration commands.
- Finish active scenes during target transitions and finish active RTT work in
  `RenderPresent` without switching the selected target.
- Preserve or explicitly diagnose prior destination contents across every new
  PVR scene.
- Preserve existing blend-mode mappings (NONE/BLEND/ADD/MUL) for RGB565
  destinations.

Physical-hardware captures determine whether the simple scene-boundary design
is sufficient. Missing S1/T1 markers are an expected diagnostic outcome, not a
reason to lower the test requirements or silently accept weaker semantics.
Non-VGA modes, ARGB targets, and performance optimization remain later phases.

**Recommendation:** Keep ordinary target capability disabled. The guarded
prototype has now established the next exact design requirement: preserve the
existing destination across every screen and RTT scene boundary, including a
present while a target remains selected. Do not promote or commit the
prototype as Phase 5 functionality until that requirement and the raw-edge
sampling defect described below are solved and physically revalidated.

---

## 11. Guarded-prototype physical-hardware result (2026-07-29)

### 11.1 Reviewed prototype corrections before launch

Independent source review found and corrected several false-positive and
safety defects in the first implementation handoff before hardware execution:

- `SetRenderTarget` now validates the complete new target before finishing the
  active scene, because SDL has already changed `renderer->target` and does not
  roll it back after a backend callback failure.
- a completed RTT allocation is sampleable without satisfying the CPU-upload
  flag used by ordinary textures;
- RTT clear and geometry bounds use the target's logical dimensions instead of
  the 640x480 screen dimensions;
- the common scene-finalization helper returns PVR errors and later drawing
  fails closed after a scene failure;
- target destruction no longer traverses an unrelated deferred-texture chain;
  and
- the focused test was replaced because the handoff version captured no
  pages, held only after erasing the last page, contained placeholders and
  unconditional success assertions, did not perform self-feedback, and sampled
  an uninitialized source texture.

The corrected test presents ten independent pages, waits three VBLs, writes one
raw 640x480 PPM per page, and prints concrete visual expectations. Both the
prototype and ordinary SDL archives built successfully. The private callback
is present only in the prototype archive; the ordinary archive remains without
`DC_PVR_SetRenderTarget` or target capability advertisement.

### 11.2 Physical run record

- Platform: physical Dreamcast, 640x480 VGA, BBA at `192.168.0.128`.
- Serial log:
  `test/dreamcast/test/pvr_render_target/hardware-20260729-114256.log`.
- Captures:
  `pvr_render_target-hardware-page1.ppm` through
  `pvr_render_target-hardware-page10.ppm` in the same directory.
- API result: 10 checks, 0 failures.
- Process result: program returned 0.
- No emulator was used.

The API result is not the feature verdict. Direct inspection of the physical
captures gives the following results:

1. API page passed: the large status panel is green.
2. Basic RTT render/sample passed orientation and center clipping. All four
   colored corners and the clipped magenta center are present. Small gaps are
   visible along the sampled target's extreme bottom/right edges.
3. **Screen persistence failed.** The upper-left red S1 block is completely
   absent; only the later lower-right green S2 block remains.
4. Two independent targets rendered and sampled correctly in one final screen
   scene: orange/red A at left and cyan/green B at right.
5. NONE, BLEND, ADD, and MUL produced four distinct ordered target columns.
   Small gaps again appear at the extreme sampled bottom/right edge.
6. Completed-target nearest and linear/modulated sampling rendered as expected:
   sharp blue/white checker at left and smoother red-modulated translucent
   checker at right.
7. **Target reselection persistence failed.** Red T1 is completely absent;
   only the later green T2 marker remains.
8. **Target persistence across `SDL_RenderPresent` failed.** Blue T1 is
   completely absent; only later yellow T2 remains.
9. Feedback rejection and API recovery passed, but the previously copied green
   recovery target disappeared after subsequent RTT lifecycle transitions;
   only the final green status bar remains. This is another manifestation of
   failed screen persistence.
10. The NPOT target has correct orientation and its colored edges/center are
    recognizable, but periodic holes expose the screen background along the
    sampled extreme bottom edge and at the lower-right edge. The raw guard page
    therefore fails the no-missing-edge/no-padding-leakage requirement.

There was no freeze, reset, timeout, crash, or self-feedback recovery failure.
The failures are deterministic missing prior destination contents and
incomplete extreme-edge presentation in the raw physical framebuffer images.

### 11.3 Evidence-based verdict and next design task

The simple one-allocation scene-boundary design is insufficient for SDL render
targets on the PVR. A new scene does not load and blend against the prior
destination; it replaces it. This affects both the visible screen destination
and RTT VRAM, including consecutive target scenes separated only by
`SDL_RenderPresent`.

The next task is research/design, not another count adjustment or an immediate
renderer patch. Determine the smallest exact preservation architecture for:

- screen S1 -> RTT -> screen S2;
- target T1 -> away -> same target -> T2; and
- target T1 -> present while selected -> T2.

The design must quantify VRAM and TA costs and preserve SDL painter order.
Candidate approaches may include destination ping-pong with an initial full
copy, but must account for the lack of destination-as-source feedback and for
screen ownership. Separately identify whether the bottom/right holes come from
the copy geometry's exact UV endpoint, PVR texture wrapping, RTT stride/padding,
or raster edge ownership. Do not conflate that defect with persistence.

Until both issues are solved and rerun on physical hardware, keep
`DC_PVR_RENDER_TARGET_PROTOTYPE` private, keep ordinary
`SDL_RENDERER_TARGETTEXTURE` disabled, and do not commit the prototype as a
completed renderer feature.
