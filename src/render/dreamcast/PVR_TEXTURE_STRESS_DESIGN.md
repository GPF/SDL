# Phase 3.3: Texture Lifecycle Stress, VRAM Exhaustion, and Failure Rollback

**Status:** Implemented and validated in Flycast and on physical Dreamcast.
**Date:** 2026-07-28
**Scope:** Ownership audit, SDL contract confirmation, KOS allocator behavior,
failure-injection design, VRAM-exhaustion strategy, focused test design,
Flycast and hardware workflow, acceptance criteria.

---

## 1. Current ownership model

### 1.1 Renderer ownership

`DC_PVR_CreateRenderer` allocates `DC_PVR_RenderData` via `SDL_calloc`.
`pvr_init()` runs with direct submission and DMA disabled. If `pvr_init()`
succeeds, `data->pvr_owned` is set to `SDL_TRUE`. If it fails, the allocated
data is freed and the renderer is rejected.

`DC_PVR_DestroyRenderer` shuts PVR down only if `data->pvr_owned` is true:
- finishes active list/scene with synchronization;
- calls `DC_PVR_ReleaseDeferredTextures(data, SDL_TRUE)` to free all
  deferred textures (those destroyed while a scene was active);
- waits one vblank;
- calls `pvr_shutdown()`;
- frees `data` and clears `renderer->driverdata`.

If `pvr_owned` is false (PVR owned by another path), deferred textures are
released with `free_vram = SDL_FALSE` (CPU-only cleanup) and PVR is not shut
down. This preserves the renderer-owns-PVR invariant.

### 1.2 Texture ownership

`DC_PVR_CreateTexture` allocates `DC_PVR_TextureData` via `SDL_calloc`, then
CPU staging via `SDL_calloc`, then VRAM via `pvr_mem_malloc`. On failure at
any step, previously allocated resources are freed and the error is returned.
On success, `texture->driverdata` is set and `texture->pitch` is set to
`data->cpu_pitch`.

`DC_PVR_DestroyTexture` clears the lock state, invalidates the header cache
if the destroyed texture matches `renderdata->header_texture`, and either
frees immediately (when no scene is active) or appends to the
`deferred_textures` linked list (when a scene is active). `RenderPresent`
flushes deferred textures after `pvr_wait_render_done()`.

`DC_PVR_FreeTextureData` frees VRAM (only if `free_vram` is true), then
`SDL_free(data->pixels)`, then `SDL_free(data)`.

### 1.3 State tracked per texture

```c
typedef struct DC_PVR_TextureData
{
    struct DC_PVR_TextureData *next_deferred;
    void *pixels;                   // CPU staging (SDL_calloc)
    pvr_ptr_t vram;                 // PVR VRAM (pvr_mem_malloc)
    size_t allocation_size;         // backing_w * backing_h * 2
    Uint32 sdl_format;              // SDL_PIXELFORMAT_*
    int pvr_format;                 // PVR_TXRFMT_*
    int logical_w, logical_h;       // user-requested dimensions
    int backing_w, backing_h;       // next power of two (min 8)
    int cpu_pitch;                  // backing_w * 2
    int vram_pitch;                 // same as cpu_pitch
    SDL_TextureAccess access;       // STATIC or STREAMING
    pvr_filter_mode_t filter;       // NEAREST or BILINEAR
    SDL_bool uploaded;              // pvr_txr_load has been called
    SDL_bool dirty;                 // pending upload
    SDL_bool locked;                // LockTexture in progress
    SDL_Rect locked_rect;           // locked sub-rectangle
} DC_PVR_TextureData;
```

### 1.4 State tracked per renderer

```c
typedef struct DC_PVR_RenderData
{
    SDL_bool pvr_owned;             // renderer initialized PVR
    SDL_bool scene_active;          // pvr_scene_begin called
    SDL_bool list_active;           // pvr_list_begin called
    SDL_bool header_valid;          // current polygon header cached
    SDL_bool vsync;                 // vsync enabled
    SDL_bool pvr_failed;            // fatal PVR error
    int output_w, output_h;         // framebuffer dimensions
    int view_x, view_y, view_w, view_h;
    SDL_bool viewport_set;
    SDL_bool has_viewport;
    SDL_Rect clip_rect;
    SDL_bool has_cliprect;
    SDL_Texture *header_texture;    // cached texture pointer
    SDL_BlendMode header_blend;
    pvr_txr_shading_mode_t header_texture_env;
    pvr_filter_mode_t header_filter;
    DC_PVR_TextureData *deferred_textures;
} DC_PVR_RenderData;
```

### 1.5 Confirmed defects

**None found.** The current implementation correctly:
- rolls back on each intermediate failure in `CreateTexture`;
- never double-frees (each allocation freed exactly once through one free path);
- clears lock state on destroy;
- invalidates header cache on destroy;
- defers VRAM cleanup during active scenes and flushes at `RenderPresent`;
- resets all state on renderer recreate/destroy;
- handles `pvr_failed` recovery by calling `pvr_shutdown()` and then releasing
  deferred CPU allocations without individually freeing invalidated VRAM.

### 1.6 Defensive hardening opportunities

These are not bugs but opportunities to detect future regressions:

- `DC_PVR_DestroyTexture` does not check `renderdata->scene_active` before
  reading `header_texture` (relied on via `renderdata->header_valid` short-
  circuit). Adding an explicit guard would make the invariant more visible.
- `DC_PVR_UnlockTexture` does not validate `texture->driverdata` is still
  attached to a live texture (SDL does not check magic on unlock). This is
  acceptable because SDL calls unlock only on valid textures, but a diagnostic
  guard could catch misuse in tests.
- `DC_PVR_DestroyRenderer` does not assert `data->deferred_textures == NULL`
  after release (it always is, by design). Adding the assertion would catch
  future bugs.

---

## 2. SDL contract findings

### 2.1 `SDL_CreateTexture` failure path

SDL core (`SDL_render.c` line 1354+):

1. Validates format, access, dimensions.
2. Allocates `SDL_Texture` via `SDL_calloc`.
3. Links it into `renderer->textures` list.
4. Calls `renderer->CreateTexture(renderer, texture)`.
5. If that fails, calls `SDL_DestroyTexture(texture)` and returns NULL.

**Implication:** If the backend fails after partially initializing
`driverdata`, SDL calls `DestroyTexture`, which calls the backend callback
with a partial `driverdata`. The backend must handle this safely.

**Current behavior:** `DC_PVR_CreateTexture` sets `texture->driverdata` only
after all three allocations succeed. On failure at any step, previously
allocated memory is freed before returning. `DC_PVR_DestroyTexture` returns
immediately if `data` is NULL. **Contract is satisfied.**

### 2.2 `SDL_DestroyTexture` behavior

SDL core (`SDL_render.c` line 4387+):

1. Calls `FlushRenderCommandsIfTextureNeeded(texture)` to execute queued
   geometry referencing this texture.
2. Sets `texture->magic = NULL`.
3. Unlinks from `renderer->textures`.
4. If `texture->native` exists, calls `SDL_DestroyTexture(texture->native)`.
5. Frees `texture->pixels` (conversion buffer).
6. Calls `renderer->DestroyTexture(renderer, texture)`.
7. Frees `texture->locked_surface`.
8. Frees `texture` struct itself.

**Implication:** The backend callback runs after the texture is unlinked and
the magic is cleared. SDL does NOT unlock a locked texture before calling
`DestroyTexture`. The backend must handle locked textures on destroy.

**Current behavior:** `DC_PVR_DestroyTexture` clears `data->locked` and
zeros `data->locked_rect`, then frees allocations. **Contract is satisfied.**

### 2.3 `SDL_LockTexture` behavior

SDL core (`SDL_render.c` line 2156+):

1. Checks `texture->magic`.
2. Validates `texture->access == SDL_TEXTUREACCESS_STREAMING`.
3. If `rect` is NULL, uses full texture rect.
4. If `texture->native` exists, calls `SDL_LockTextureNative`.
5. Otherwise, calls `renderer->LockTexture(renderer, texture, rect, pixels, pitch)`.

**Implication:** SDL does NOT check for nested locks. The backend must
reject nested locks. SDL does NOT validate rect bounds before calling the
backend; the backend must validate.

**Current behavior:** `DC_PVR_LockTexture` rejects non-streaming, checks
`data->locked`, validates rect, returns pointer into staging. **Contract is
satisfied.**

### 2.4 `SDL_UnlockTexture` behavior

SDL core (`SDL_render.c` line 2272+):

1. Checks `texture->magic`.
2. Returns immediately if `texture->access != SDL_TEXTUREACCESS_STREAMING`.
3. If `texture->native` exists, calls `SDL_UnlockTextureNative`.
4. Otherwise, calls `renderer->UnlockTexture(renderer, texture)`.
5. Frees `texture->locked_surface`.

**Implication:** SDL does NOT check if the texture is currently locked.
The backend must handle unlock without a prior lock gracefully.

**Current behavior:** `DC_PVR_UnlockTexture` returns immediately if
`data->locked` is false. **Contract is satisfied.**

### 2.5 `SDL_UpdateTexture` behavior

SDL core (`SDL_render.c` line 1865+):

1. If `texture->native` exists, calls `SDL_UpdateTextureNative` (which locks
   native, copies converted pixels, unlocks native).
2. Otherwise, calls `renderer->UpdateTexture(renderer, texture, rect, pixels, pitch)`.

**Implication:** The backend receives validated rect and pitch. SDL does NOT
validate that the texture is not locked before calling the backend. The
backend must reject updates to locked textures.

**Current behavior:** `DC_PVR_UpdateTexture` rejects if `data->locked` is true.
**Contract is satisfied.**

### 2.6 Public API prechecks (cannot reach backend)

The following are validated by SDL core before the backend callback runs:

- `SDL_CreateTexture`: format must be supported (or convertible), access must
  be valid, dimensions must be positive and within `max_texture_width/height`.
- `SDL_LockTexture`: access must be `STREAMING`, rect must be within texture
  bounds (if provided).
- `SDL_UpdateTexture`: rect must be within texture bounds (if provided),
  pixels must be non-NULL.
- `SDL_DestroyTexture`: texture must be non-NULL and magic-valid.

**Implication for testing:** These prechecks are SDL behavior, not backend
behavior. The backend test should not verify them unless explicitly
exercising failure paths that reach the backend (e.g., format rejection when
the format IS in the advertised list).

---

## 3. KOS/PVR allocator findings

### 3.1 `pvr_mem_malloc`

- Returns 32-byte-aligned pointer on success.
- Returns NULL on exhaustion (when `pvr_int_sbrk` returns -1).
- Uses dlmalloc-derived allocator (`pvr_mem_core.c`).
- Requires `pvr_mem_sbrk != NULL` (checked via `CHECK_MEM_SBRK`).

### 3.2 `pvr_mem_free`

- Calls `pvr_int_free((void *)chunk)`.
- Optionally removes from debug block list (if `PVR_KM_DBG` is defined).
- Does NOT validate alignment or size.
- Does NOT require synchronization.

### 3.3 `pvr_mem_available`

- Returns `mi.arena - mi.uordblks + (PVR_RAM_INT_TOP - (size_t)pvr_mem_sbrk)`.
- Requires `pvr_mem_sbrk != NULL`.
- Returns 0 if PVR is not initialized.

### 3.4 `pvr_mem_reset`

- Resets allocator to base: `pvr_mem_sbrk = pvr_mem_base; pvr_int_mem_reset();`.
- Used on mode/configuration change.
- Not called by `pvr_shutdown()` or `pvr_init()` directly.

### 3.5 Fragmentation behavior

dlmalloc is a general-purpose allocator. It does not guarantee
contiguous-free recovery after fragmentation. However, for small, uniform
allocations (all textures are same size), fragmentation is minimal.

### 3.6 Allocator alignment

- 32-byte alignment for all allocations.
- `pvr_txr_load` requires source pointer aligned to 8 bytes (SH-4 cache line).
- `SDL_calloc` returns aligned memory.

### 3.7 Restrictions during active scene/list

No documented restriction. `pvr_mem_free` does not check scene/list state.
However, freeing a texture while queued geometry references it could cause
PVR to sample freed memory. SDL flushes before destruction, so this is
safe in practice.

### 3.8 Renderer/PVR shutdown

`pvr_shutdown()` does NOT call `pvr_mem_reset()`. The allocator state
persists across renderer create/destroy cycles if PVR is re-initialized.

### 3.9 Destruction after queued rendering is flushed

SDL flushes commands referencing the texture before calling
`DestroyTexture`. After `pvr_wait_render_done()` in `RenderPresent`, all
queued geometry has been processed. The focused test draws a texture, lets SDL
flush that queued rendering before destruction, and verifies that cleanup is
safe after synchronization; it does not destroy a texture while its draw
remains queued.

---

## 4. Implemented stress and failure strategy

The focused test uses 256×256 RGB565 exhaustion textures. Logical and backing
dimensions are identical, so each allocation represents 256 * 256 * 2 =
131,072 bytes of VRAM. A cap of 64 prevents an infinite allocation loop. Three
known-good format-specific textures are kept outside the exhaustion array and
drawn before and after the first failure and after reallocation.

Failure injection is compiled only when
`DC_PVR_TEXTURE_FAILURE_INJECTION` is defined. Under that guard, the texture
translation unit reads the private hints `SDL_DC_PVR_TEST_ALLOC_STAGE` and
`SDL_DC_PVR_TEST_ALLOC_COUNTDOWN`. A private stage enum selects driver data,
CPU staging, or VRAM, and a resettable countdown rejects the Nth matching
allocation immediately before the corresponding native allocation. Changing
either hint resets the countdown. No global allocator is intercepted and no
public SDL API is added. Ordinary builds contain neither the state and
branches nor the private hint and injected-error strings.

The test runs 50 static cycles, 20 streaming cycles, 12 simultaneous mixed
textures, three injected failures with immediate recovery, VRAM exhaustion,
half-free/reallocation, post-exhaustion texture use, two renderer recreations,
and a final three-format visual page. Streaming coverage includes full,
partial, right-edge, and bottom-edge locks; nested-lock rejection;
unlock-without-lock no-op behavior; lock reset; and four destroy-while-locked
cases. Lifecycle coverage also draws a texture before SDL flushes its queued
rendering for destruction, synchronizes before cleanup, and tears down a
renderer with five associated live textures.

`pvr_mem_available()` was not used because the standard KOS build does not
enable `PVR_KM_DBG`. Recovery is therefore inferred from successful uniform
reallocations; exact allocator-byte restoration is not claimed.

## 5. Validation results (2026-07-28)

Flycast and physical Dreamcast produced identical exhaustion accounting:
45 initial allocations (5,898,240 represented bytes), 23 freed, and 23
successfully reallocated before the next clean SDL failure. Both failures
reported `Could not allocate Dreamcast PVR texture VRAM`. All three injected
allocation failures were identified by stage, and each was followed
immediately by successful create/update/draw/destroy use.

Final accounting in both environments was: 180 successful texture creations,
5 expected creation failures, 180 destroys, 192 updates, 115 draws, 84 locks,
80 unlocks, 4 locked destroys, 20 unlock-without-lock calls, 45 initial
exhaustion allocations, 23 freed, 23 recovered, 3 renderer creates, and 2
renderer destroys while one renderer remained active for final cleanup.

KOS emits its generic `pvr_mem: ERROR -- out of PVR memory; texture heap
probably corrupted` diagnostic at each intentional exhaustion point. The SDL
error, complete recovery, identical emulator/hardware counts, and continued
use of existing and new textures confirm deliberate exhaustion rather than
heap corruption. No other SDL, KOS, PVR, or allocator warning was observed.

The physical command was:

```sh
kos-tool -t 192.168.0.128 \
  -x test/dreamcast/test/pvr_texture_stress/pvr_texture_stress.elf \
  -m .
```

The capture build waited three vblanks after final presentation for the
three-buffer display path, then wrote
`/pc/pvr_texture_stress-hardware.ppm`. The preserved 640×480 PPM is
`test/dreamcast/test/pvr_texture_stress/pvr_texture_stress-hardware.ppm`, with
SHA-256 `ea26ac35a8664399f01fb8e64bd505de16b013b783a3449717986eca26996c7a`.
It matches the Flycast final frame: three intact format-specific gradients
with clean red, green, and blue borders and no stale tiles, corruption,
blanking, or seams. Hardware printed PASS, returned zero, and returned
normally to `dc-load-ip`.

Fresh unified, failure-enabled, and PVR-disabled SDL builds succeeded. The
changed renderer translation unit and stress test compile with
`-Wall -Wextra -Werror`; all existing native-PVR tests rebuild against the
explicit ordinary local archive. The opt-out archive contains no native-PVR
objects or symbols. The ordinary archive and final ordinary ELF contain no
failure hints or injected-error strings; the ELF also contains no screenshot
symbol, `/pc` capture path, or screenshot define.

---

## 9. Scope boundaries

### 9.1 In scope

- Texture creation ownership and cleanup.
- Partial-allocation rollback (CPU staging, VRAM).
- Repeated static and streaming texture lifecycles.
- Lock/unlock state management.
- Renderer teardown with five associated live textures.
- VRAM exhaustion and allocator recovery.
- Deterministic failure injection (test-only).
- Flycast and physical-hardware validation.

### 9.2 Out of scope (deferred)

- Partial VRAM uploads.
- DMA uploads.
- Twiddled/VQ/strided textures.
- Render targets.
- New pixel formats.
- New blend modes.
- Broad performance optimization.
- Unrelated FBO/GLdc work.
- 320×240 or PAL/NTSC mode coverage.
- TA/OPB capacity measurement.

---

## 10. Completion report

### 10.1 Current ownership model

- Renderer owns PVR: initialized on create, shut down on destroy (if owned).
- Texture owns CPU staging and VRAM: allocated on create, freed on destroy
  (immediate or deferred during active scene).
- Header cache tracks current texture: invalidated on destroy.
- Deferred textures list: accumulates textures destroyed during active scene,
  flushed at `RenderPresent` after sync.

### 10.2 Confirmed defects

None. The current implementation is architecturally sound and behaviorally
correct for its documented scope.

### 10.3 SDL contract findings

- SDL validates format, access, dimensions before backend callback.
- SDL flushes queued geometry before `DestroyTexture`.
- SDL does NOT check nested locks or unlock-before-destroy.
- Backend must handle locked textures on destroy, nested locks, and unlock
  without prior lock.

### 10.4 KOS allocator findings

- `pvr_mem_malloc` returns 32-byte-aligned pointer or NULL on exhaustion.
- `pvr_mem_free` does not require synchronization.
- `pvr_mem_available` returns available bytes (requires `PVR_KM_DBG`).
- dlmalloc fragmentation is minimal for uniform allocations.
- Allocator state persists across renderer create/destroy cycles.

### 10.5 Failure-injection implementation

One compile-time guard enables private stage/countdown hints and state. The
Nth matching driver-data, staging, or VRAM allocation fails deterministically;
ordinary builds compile out the mechanism completely.

### 10.6 VRAM exhaustion strategy

Allocate 256×256 RGB565 textures (131,072 VRAM bytes each) until failure.
Preserve three known-good textures, free every other allocation, reallocate
uniform textures, destroy all, and verify both normal allocation and renderer
recreation.

### 10.7 Completed test phases and iteration counts

- Phase A: 50 static textures (create/update/draw/destroy).
- Phase B: 20 streaming textures (full/partial lock/unlock, nested rejection).
- Phase C: 12 simultaneous (mixed lifetimes, renderer destroy/recreate).
- Phase D: 3 injection attempts (driverdata/staging/VRAM).
- Phase E: 45 initial allocations, 23 frees, and 23 recovered allocations.

### 10.8 Flycast and hardware validation

- Flycast and hardware both reached PASS with identical stress counts.
- Hardware returned zero to `dc-load-ip` and produced the preserved 640×480
  PPM documented above.

### 10.9 Acceptance criteria

- Build: clean (strict warnings), opt-in/opt-out correct.
- Flycast: PASS, no errors, all draws correct.
- Hardware: PASS, only the two expected exhaustion diagnostics, all draws
  correct, exits normally.
- Code review: no leaks, no double-frees, no dangling pointers.
- Documentation: README.md and PVR_TEXTURE_STRESS_DESIGN.md complete.

### 10.10 Exact files to modify

1. `src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c`:
   - Add failure-injection guards (optional, test-only).
2. `test/dreamcast/test/pvr_texture_stress/pvr_texture_stress.c`:
   - Implement test phases A-E.
3. `test/dreamcast/test/pvr_texture_stress/Makefile.dc`:
   - Configure build with explicit local SDL archive.
4. `test/dreamcast/test/pvr_texture_stress/README.md`:
   - Document test phases, iteration counts, pass/fail criteria.
5. `src/render/dreamcast/PVR_TEXTURE_STRESS_DESIGN.md`:
   - This design document (already created).

### 10.11 Remaining limitations

1. **Exact allocator-byte restoration:** Not measured without `PVR_KM_DBG`;
   uniform successful reallocation is the recovery evidence.

2. **Remaining hardening:** Other video modes, partial VRAM uploads, DMA,
   twiddled/VQ layouts, and TA/OPB capacity remain later work.

3. **Failure injection necessity:** The current implementation already handles
   partial-allocation failure correctly. Injection is for regression testing,
   not for validating current behavior. Consider omitting if not needed.

4. **Screenshot necessity:** Start with no screenshot. Add only if visual
   artifacts need confirmation during validation.

5. **Stress duration:** ~350 texture operations should complete in < 30
   seconds on hardware. Monitor during Flycast validation and adjust if
   needed.
