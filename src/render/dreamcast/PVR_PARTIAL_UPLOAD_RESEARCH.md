# Dreamcast Native PVR Renderer: Partial Texture Upload Research

**Status:** Research complete; its original defer recommendation was superseded
by the guarded Phase 6.5 implementation and physical-hardware validation on
2026-07-30. The historical analysis below remains the design record.
**Date:** 2026-07-28
**Scope:** KOS transfer-API audit, VRAM layout analysis, partial-upload
strategy comparison, alignment constraints, synchronization, dirty-region
design, performance analysis, DMA evaluation, test plan, benchmark plan,
validation plan, and implementation recommendation.

This document researches whether partial texture uploads are practical, safe,
and worthwhile with the current KallistiOS/PVR APIs and the native PVR
renderer architecture. It audits the current full-backing upload path, inspects
the actual KOS transfer implementations, evaluates partial-upload strategies,
and produces an implementation-ready recommendation.

**Related research:** `PVR_TEXTURE_RESEARCH.md` records the Phase 3 texture
baseline design and the implemented behavior that this document extends from.

> **Implementation addendum (2026-07-30).**
> `SDL_DC_PVR_PARTIAL_UPLOAD` is an OFF-by-default option dependent on the
> native Dreamcast PVR renderer. When enabled, `UpdateTexture` and
> `UnlockTexture` use a synchronous store-queue partial copy only for
> unreferenced nontwiddled POT allocations and strictly smaller
> 32-byte-expanded contiguous physical ranges. The range is copied unchanged
> from CPU staging to an aligned temporary buffer. Strided, preformatted,
> target, scene-referenced, and complete-backing updates retain the existing
> full-upload path; complete backings still use the blocking DMA/SQ chooser.
> `test/dreamcast/test/pvr_partial_upload` passed its three captured pages on
> real 640x480 VGA hardware. The implementation status, exact commands, and
> capture hashes are maintained in `AGENTS.md`.

## 1. Current upload-path audit

### 1.1 Texture creation path

`DC_PVR_CreateTexture` allocates two buffers of identical size:

```
allocation_size = backing_w * backing_h * 2    // bytes
```

- **CPU staging:** `data->pixels = SDL_calloc(1, allocation_size)`. Zero-
  initialized. Row pitch `cpu_pitch = backing_w * 2`.
- **VRAM:** `data->vram = pvr_mem_malloc(allocation_size)`. 32-byte aligned
  per the KOS `pvr_mem_malloc` assertion `assert_msg((rv32 & 0x1f) == 0, ...)`.
  Row pitch `vram_pitch = cpu_pitch`.

`texture->pitch` is set to `data->cpu_pitch`. `data->uploaded = SDL_FALSE`.
`data->dirty = SDL_TRUE`.

### 1.2 `SDL_UpdateTexture` path

```c
int DC_PVR_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                         const SDL_Rect *rect, const void *pixels, int pitch)
{
    // ... validation ...
    row_bytes = (size_t)rect->w * 2;
    destination = (Uint8 *)data->pixels +
                  (size_t)rect->y * data->cpu_pitch + (size_t)rect->x * 2;
    for (row = 0; row < rect->h; ++row) {
        SDL_memcpy(destination, source, row_bytes);
        destination += data->cpu_pitch;
        source += pitch;
    }
    data->dirty = SDL_TRUE;
    pvr_txr_load(data->pixels, data->vram, data->allocation_size);
    data->uploaded = SDL_TRUE;
    data->dirty = SDL_FALSE;
    return 0;
}
```

The loop copies `rect->h` rows of `rect->w * 2` bytes each, advancing by
`cpu_pitch` in the destination and `pitch` in the source. Untouched POT padding
retains previous contents.

**Then the full POT backing is uploaded** through `pvr_txr_load`, regardless
of which rectangle changed. A 1×1 update still transfers
`backing_w * backing_h * 2` bytes.

### 1.3 `SDL_LockTexture` / `SDL_UnlockTexture` path

`LockTexture` returns a pointer into the CPU staging at
`(rect->y * cpu_pitch + rect->x * 2)`. `*pitch` is set to `cpu_pitch`.

`UnlockTexture` calls the same `pvr_txr_load(data->pixels, data->vram,
data->allocation_size)` as `UpdateTexture`. Full backing upload on every unlock.

### 1.4 When and why the full backing is uploaded

The full backing upload is **not required for correctness**. A partial upload
would be correct if the backend could write to arbitrary VRAM offsets with
proper alignment. The current implementation uploads the full backing because:

1. `pvr_txr_load` accepts only a single contiguous block. It has no offset or
   pitch parameter.
2. The simpler implementation avoids tracking which VRAM region corresponds to
   which CPU region.
3. No partial-upload optimization is required for Phase 3 baseline behavior.

This is the simplest implementation. It is **not** the only correct one.

### 1.5 Texture destruction while uploads or draws are pending

- **Destruction during active scene:** `data->next_deferred` is prepended to
  `renderdata->deferred_textures`. Cleanup is deferred until
  `pvr_wait_render_done()` in `DC_PVR_RenderPresent`.
- **Destruction after queue flush:** SDL calls `FlushRenderCommandsIfTextureNeeded`
  before the backend `DestroyTexture`. Queued geometry referencing the texture
  is executed before destruction.
- **Destruction while locked:** `data->locked` is cleared. No upload required.
- **Header cache:** `renderdata->header_valid` is cleared and
  `renderdata->header_texture` is set to NULL before any cleanup, preventing
  stale references.

## 2. KOS/PVR transfer-API audit

### 2.1 `pvr_txr_load`

**Header declaration:**
```c
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count);
```

**Actual implementation** (from `pvr_texture.c`):
```c
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count) {
    count = __align_up(count, 4);
    pvr_sq_load((uint32_t *)dst, (const uint32_t *)src, count, PVR_DMA_VRAM64);
}
```

**Downstream:** `pvr_sq_load` calls `sq_cpy(dma_area_ptr, src, n)`, where
`dma_area_ptr` is the PVR-mapped destination address computed by
`pvr_dest_addr(dest, type)`.

**Constraints (verified in `sq.c` implementation):**

| Constraint | Value |
|-----------|-------|
| Source alignment | 8 bytes (fast path); 1 byte (slow path) |
| Destination alignment | 32 bytes |
| Transfer size | Must be multiple of 32 bytes (n >>= 5) |
| Minimum transfer unit | 32 bytes |
| Source pitch support | None. Single contiguous block only. |
| Destination pitch support | None. Single contiguous block only. |
| Destination offset support | None. Must start at `dst`. |
| Row-by-row copies | Not supported by this API. |
| Synchronous | Yes. Blocks until copy completes. |
| Completion wait | Not required. |
| Active PVR scene | Not required. Safe to call anytime. |
| Cache coherency | Store queues bypass data cache. No flush needed. |
| Texture layout | None. Raw bytes. |
| Behavior for small transfers | 32-byte minimum. |
| Behavior for non-32-byte row widths | Requires padding. |
| Internal padding | `__align_up(count, 4)` rounds up to 4; `sq_cpy` requires
  full 32-byte multiples. |
| DMA setup cost | N/A. Uses store queues, not DMA. |

**Key limitation:** `pvr_txr_load` copies a single contiguous block from a
single source address to a single destination address. It cannot:
- Upload individual rows to different destination offsets.
- Handle different source and destination pitches.
- Upload a sub-rectangle within a texture.

### 2.2 `pvr_txr_load_ex`

**Header declaration:**
```c
void pvr_txr_load_ex(const void *src, pvr_ptr_t dst,
                     uint32_t w, uint32_t h, uint32_t flags);
```

**Actual implementation:** Performs twiddling in software. Requires `w` and
`h` to be powers of two. Supports `PVR_TXRLOAD_INVERT_Y`. The comment says
"Pretty much the only supported flag, other than the format ones is the
PVR_TXRLOAD_INVERT_Y one."

**Constraints:**
- Source alignment: same as `pvr_txr_load` (8 bytes fast path).
- Destination alignment: 32 bytes (PVR memory).
- Transfer size: `w * h * bpp / 8` bytes.
- Synchronous.
- **Twiddles the data in software.** Not suitable for our nontwiddled format.
- Does not support partial uploads.

**Verdict:** Not useful for partial uploads of nontwiddled textures.

### 2.3 `pvr_txr_load_dma`

**Header declaration:**
```c
int pvr_txr_load_dma(const void *src, pvr_ptr_t dest, size_t count, bool block,
                     pvr_dma_callback_t callback, void *cbdata);
```

**Actual implementation** (from `pvr_dma.c`):
```c
int pvr_txr_load_dma(const void *src, pvr_ptr_t dest, size_t count, bool block,
                    pvr_dma_callback_t callback, void *cbdata) {
    return pvr_dma_transfer(src, (uintptr_t)dest, count, PVR_DMA_VRAM64, block,
                            callback, cbdata);
}
```

**Downstream:** `pvr_dma_transfer` configures DMA channel 2 with
`DMA_UNITSIZE_32BYTE`, `DMA_TRANSMITMODE_BURST`, and writes to the PVR DMA
registers.

**Constraints (verified in `pvr_dma.c` implementation):**

| Constraint | Value |
|-----------|-------|
| Source alignment | 32 bytes (checked: `if(src_addr & 0x1F) { ... errno = EFAULT; return -1; }`) |
| Destination alignment | 32 bytes (checked via `pvr_dest_addr`, `EFAULT` on misalignment) |
| Transfer size | Must be multiple of 32 bytes |
| Minimum transfer unit | 32 bytes |
| Source pitch support | None. Single contiguous block only. |
| Destination pitch support | None. Single contiguous block only. |
| Destination offset support | None. Must start at `dest`. |
| Row-by-row copies | Not supported by this API. |
| Synchronous | Configurable: `block` parameter. |
| Asynchronous | Supported via callback + non-blocking mode. |
| Completion wait | Required for blocking mode (`sem_wait(&dma_done)`). |
| Active PVR scene | Not required. Safe to call anytime. |
| Cache coherency | DMA bypasses cache. No flush needed. |
| Texture layout | None. Raw bytes. |
| Behavior for small transfers | 32-byte minimum. |
| Behavior for non-32-byte row widths | Requires padding. |
| DMA setup cost | Register writes, DMA channel configuration, possible interrupt
  overhead. Significant for many small transfers. |
| Prerequisite | `pvr_dma_init()` must be called first. **NOT called in the
  current renderer** (DMA disabled via `pvr_init_params` zero DMA bins). |
| Concurrency | `pvr_dma[PVR_DST] != 0` check rejects concurrent transfers
  (`errno = EINPROGRESS`). One DMA at a time. |

**Key limitation:** Same as `pvr_txr_load`. Single contiguous block only. Plus
it requires `pvr_dma_init()`, which is not enabled in the current renderer.

### 2.4 `pvr_dma_transfer`

Same constraints as `pvr_txr_load_dma`. Same single-contiguous-block
limitation. Same 32-byte alignment requirements.

### 2.5 `pvr_sq_load`

**Header declaration:**
```c
void *pvr_sq_load(void *dest, const void *src, size_t n, pvr_dma_type_t type);
```

**Actual implementation:**
```c
void *pvr_sq_load(void *dest, const void *src, size_t n, pvr_dma_type_t type) {
    void *dma_area_ptr;
    dma_area_ptr = (void *)pvr_dest_addr((uintptr_t)dest, type);
    sq_cpy(dma_area_ptr, src, n);
    return dest;
}
```

**Constraints:** Same as `pvr_txr_load` (wraps `sq_cpy`). Same single-
contiguous-block limitation.

### 2.6 `sq_cpy`

**Header declaration:**
```c
void *sq_cpy(void *dest, const void *src, size_t n);
```

**Actual implementation** (from `sq.c`):
```c
__noinline void *sq_cpy(void *dest, const void *src, size_t n) {
    const uint32_t *s = src;
    void *curr_dest = dest;
    uint32_t *d;
    size_t nb;

    n >>= 5;  // iterations of 32 bytes each

    while(n > 0) {
        nb = n > 0x8000 ? 0x8000 : n;  // max 1 MiB per lock

        d = sq_lock(curr_dest);
        curr_dest += nb * 32;
        n -= nb;

        if(!__is_aligned(src, 8)) {
            // slow path: individual 32-bit stores
            while(nb--) { ... }
        } else {
            // fast path: sq_fast_cpy
            sq_fast_cpy(d, s, nb);
            s += nb * 32;
        }
        sq_unlock();
    }
    return dest;
}
```

**Constraints:**

| Constraint | Value |
|-----------|-------|
| Source alignment | 8 bytes (fast path); any (slow path) |
| Destination alignment | 32 bytes (enforced by `sq_lock` / QACR registers) |
| Transfer size | Must be multiple of 32 bytes (n >>= 5) |
| Minimum transfer unit | 32 bytes |
| Thread safety | Mutex (`sq_mutex`). Recursive. |
| Synchronous | Yes. |
| Max per lock | 1 MiB (MMU SQ area limit). Split into chunks. |

**Verdict:** No pitch support. No offset support. Single contiguous block only.

### 2.7 Summary of KOS transfer API limitations

**None of the KOS transfer APIs support:**

1. Row-by-row transfers with different destination addresses.
2. Different source and destination pitches.
3. Partial uploads with arbitrary VRAM destination offsets.
4. Non-32-aligned destination offsets.
5. Non-32-byte-multiple transfer sizes.

**All of them require:**

1. 32-byte-aligned destination in PVR memory.
2. Transfer size multiple of 32 bytes.
3. Single contiguous source block.
4. Single contiguous destination block.

This is the fundamental constraint that makes partial uploads non-trivial.

## 3. VRAM memory layout

### 3.1 Linear nontwiddled layout

For all three current formats (RGB565, ARGB1555, ARGB4444), the VRAM layout is
linear row-major, 16 bits per pixel, nontwiddled:

```
Row 0:  [pixel(0,0)] [pixel(1,0)] [pixel(2,0)] ... [pixel(backing_w-1,0)] [padding]
Row 1:  [pixel(0,1)] [pixel(1,1)] [pixel(2,1)] ... [pixel(backing_w-1,1)] [padding]
...
Row y:  [pixel(0,y)] [pixel(1,y)] [pixel(2,y)] ... [pixel(backing_w-1,y)] [padding]
```

Each row occupies `backing_w * 2` bytes. Right-side padding bytes retain
previous contents. Bottom padding rows retain previous contents.

### 3.2 Row pitch

`backing_pitch = backing_w * 2` bytes per row.

For POT backing dimensions:

| Backing width | Row pitch (bytes) |
|--------------|-------------------|
| 8 | 16 |
| 16 | 32 |
| 32 | 64 |
| 64 | 128 |
| 128 | 256 |
| 256 | 512 |
| 512 | 1024 |
| 1024 | 2048 |

### 3.3 Destination offset for a logical `(x, y)` texel

```
offset = y * backing_pitch + x * bytes_per_pixel
       = y * backing_w * 2 + x * 2
       = 2 * (y * backing_w + x)
```

The `vram` base pointer is 32-byte aligned (from `pvr_mem_malloc`).

### 3.4 32-byte alignment constraint for destination

For a transfer starting at `vram + offset` to be 32-byte aligned:

```
offset ≡ 0 (mod 32)
2 * (y * backing_w + x) ≡ 0 (mod 32)
y * backing_w + x ≡ 0 (mod 16)
```

**For each POT backing width:**

| Backing width | Alignment condition | Practical implication |
|--------------|--------------------|----------------------|
| 8 | `8y + x ≡ 0 (mod 16)` | Depends on y parity. For even y: x must be 0 mod 16. For odd y: x must be 8 mod 16. |
| 16 | `16y + x ≡ 0 (mod 16)` → `x ≡ 0 (mod 16)` | Only x=0 and x=16 within a 16-width texture. |
| 32 | `32y + x ≡ 0 (mod 16)` → `x ≡ 0 (mod 16)` | x must be multiple of 16. |
| 64+ | `backing_w*y + x ≡ 0 (mod 16)` → `x ≡ 0 (mod 16)` | Same: x must be multiple of 16. |

**For any backing_w ≥ 16:** `x` must be a multiple of 16 for the destination
to be 32-byte aligned. For a 16-width texture, only `x=0` works (since `x=16`
is at the right edge).

**This is a severe constraint.** Most practical partial updates have arbitrary
`x` values that are not multiples of 16. Examples:

- Update rect `{5, 3, 4, 3}` on a 17×9 logical texture backed 32×16:
  `x=5`. Not 32-aligned. Cannot use `pvr_txr_load` directly.
- Update rect `{100, 50, 32, 32}` on a 320×240 logical texture backed
  512×256: `x=100`. Not 32-aligned (100 mod 16 = 4). Cannot use `pvr_txr_load`
  directly.
- Update rect `{0, 0, 1, 1}`: `x=0`. Always 32-aligned. Works.

### 3.5 Row-transfer alignment

For each row of the dirty rectangle, the destination is:
```
row_dest = vram + y * backing_pitch + x * 2
```

Even if `x` is a multiple of 16 (making `row_dest` 32-aligned), the row data
width `w * 2` must be a multiple of 32 for `pvr_txr_load` to accept it.

For a 13-pixel-wide row: `13 * 2 = 26` bytes. Not a multiple of 32. Would need
padding to 32 bytes.

For a 1-pixel-wide row: `1 * 2 = 2` bytes. Needs padding to 32 bytes. Wastes
30 bytes.

### 3.6 Validity for all current formats

The offset formula and alignment constraints apply identically to all three
current formats (RGB565, ARGB1555, ARGB4444). All are 16-bit, linear,
nontwiddled, with `bytes_per_pixel = 2`.

**Future formats:** Twiddled, VQ-compressed, mipmapped, and strided textures
use different VRAM layouts. They are explicitly out of scope for this research.

## 4. Partial-upload strategies

### 4.1 Strategy A: Row-by-row CPU/store-queue upload

For each dirty row `(x, y, w)`:
1. Calculate source staging offset: `source + y * src_pitch`.
2. Calculate destination VRAM offset: `vram + y * backing_pitch + x * 2`.
3. Copy `w * 2` bytes from source to destination.

**Correctness:** Requires custom code to handle misaligned destinations and
non-32-byte row widths. No KOS API supports this pattern directly.

**Alignment handling:** For each row:
- If `row_dest` is 32-aligned AND `w * 2` is multiple of 32: use `pvr_txr_load`
  directly. Works for `x ≡ 0 (mod 16)` and `w ≡ 0 (mod 16)`.
- If `row_dest` is 32-aligned but `w * 2` is not multiple of 32: pad the row
  to 32 bytes. Wastes bytes but uses standard API.
- If `row_dest` is NOT 32-aligned: cannot use `pvr_txr_load`. Requires custom
  32-bit store loop to VRAM.

**Custom 32-bit store loop:** The SH-4 supports unaligned 32-bit stores. A
custom loop can write individual `volatile uint32_t` stores to the
misaligned destination:

```c
static void DC_PVR_UploadRowUnaligned(const void *row_data, size_t row_bytes,
                                      pvr_ptr_t dest) {
    const uint8_t *src = (const uint8_t *)row_data;
    volatile uint32_t *dst = (volatile uint32_t *)dest;
    uintptr_t dest_addr = (uintptr_t)dest;
    size_t remaining = row_bytes;

    // Handle destination misalignment (low byte(s))
    if (dest_addr & 1) {
        *(volatile uint8_t *)dest_addr = *src++;
        dest_addr++;
        remaining--;
    }
    if (dest_addr & 2 && remaining > 0) {
        *(volatile uint8_t *)(dest_addr - 1) = ((*(volatile uint8_t *)(dest_addr - 1))
            & 0xFFFFFF) | (*src << 0);
        // ... careful byte manipulation ...
    }
    // Write aligned 32-bit words
    while (remaining >= 4) {
        *dst++ = *(const uint32_t *)src;
        src += 4;
        remaining -= 4;
    }
    // Handle trailing bytes
    // ...
}
```

This is **complex, error-prone, and slow** (no store-queue optimization, no
burst transfers).

**Handling of odd widths:** Row width `w * 2` may not be multiple of 4 (e.g.,
`w=1` → 2 bytes). Requires partial-word writes at start and/or end.

**Handling of one-pixel updates:** 2 bytes. Destination may be misaligned.
Requires careful byte-level manipulation. Very slow per-transfer.

**Store-queue requirements:** `sq_cpy` requires 32-byte-aligned destination.
Cannot be used for misaligned rows without padding and overwrite strategy.

**Overhead per row:** One transfer call per row. For a 240-row update, that is
240 API calls. Each call has function-call overhead, mutex acquire/release
(for `sq_cpy`), and potential pipeline stalls.

**Verdict for Strategy A:** Correctness achievable with significant custom code.
Performance uncertain. High implementation complexity.

### 4.2 Strategy B: Row-by-row DMA upload

Same as Strategy A, but using `pvr_dma_transfer` or `pvr_txr_load_dma` instead
of `pvr_txr_load`.

**Additional constraints:**
- Source must be 32-byte aligned (not just 8-byte). A tightly-packed
  temporary buffer must be 32-byte aligned.
- One DMA at a time. Concurrent rows not possible.
- Setup cost per transfer: register writes, DMA channel configuration,
  possible interrupt overhead.
- Asynchronous mode adds callback + synchronization complexity.

**DMA setup cost analysis:** Each `pvr_dma_transfer` call writes to multiple
PVR DMA registers, configures the DMA channel, and potentially waits for
completion. For small transfers (32-128 bytes), the setup cost dominates the
transfer time. Many small DMA requests are impractical.

**Verdict for Strategy B:** Worse than Strategy A for small rectangles due to
DMA setup overhead. Requires `pvr_dma_init()`, which is not enabled in the
current renderer. Not recommended.

### 4.3 Strategy C: Packed temporary-buffer upload

1. Allocate a tightly-packed temporary buffer (32-byte aligned):
   `temp = malloc(w * dirty_h * 2)`, padded to 32 bytes per row.
2. Copy dirty rows from source (with source pitch) into `temp` (row pitch =
   `w * 2`, no gaps).
3. For each row, upload from `temp` to VRAM.

**Advantages:**
- Source is tightly packed. No source-pitch handling during upload.
- Temp buffer is 32-byte aligned (malloc alignment).
- Reduces source-pitch complexity.

**Disadvantages:**
- Destination alignment problem remains. Still cannot use `pvr_txr_load` for
  misaligned rows.
- Per-row temporary buffer allocation cost: `w * 2` bytes per row, or one
  buffer of `w * dirty_h * 2` bytes for all rows.
- If one buffer for all rows: still need row-by-row uploads to different
  destination offsets.
- Multiple DMA descriptors or row transfers still required for different
  destination rows.

**Verdict for Strategy C:** Same destination-alignment problem as Strategy A.
Does not solve the core issue. Adds temporary buffer complexity without
eliminating custom alignment code.

### 4.4 Strategy D: Full-backing upload fallback

Continue uploading the full POT backing through `pvr_txr_load`. This is the
current implementation.

**When it remains preferable:**
- Large dirty area (most of the texture changed).
- Highly fragmented dirty regions (multiple small updates).
- Unsupported alignment (x not multiple of 16, w not multiple of 16).
- Very small texture (overhead of partial-upload code not justified).
- DMA unavailable (not initialized in renderer).
- Transfer occurring during unsafe PVR state.
- Test-only simplicity.
- First implementation (correctness before optimization).

**Verdict for Strategy D:** Simple, correct, uses standard KOS API. Wasteful
for small updates on large textures. Acceptable for Phase 3 baseline.

### 4.5 Recommended primary strategy and fallback

**Primary strategy for first implementation:** Keep full-backing upload
(Strategy D). It is correct, simple, and uses standard APIs.

**Fallback for later optimization:** If benchmarks show full upload is a
bottleneck, implement partial uploads with:
1. Custom row-by-row upload with alignment handling (Strategy A variant).
2. Tightly-packed temporary buffer for source (Strategy C addition).
3. Only use standard `pvr_txr_load` for rows where alignment permits; custom
   code for the rest.

**Do not implement partial uploads in the first increment.** Prove the full
upload is actually a problem before adding the complexity.

## 5. Alignment and edge cases

### 5.1 `x == 0`

Destination offset: `y * backing_pitch`. For `backing_w >= 16`:
`backing_pitch` is multiple of 32. So `y * backing_pitch` is always
32-aligned. Works with `pvr_txr_load` directly.

For `backing_w == 8`: `backing_pitch = 16`. `y * 16` is 32-aligned only when
`y` is even. Odd rows require custom handling.

### 5.2 `y == 0`

Destination offset: `x * 2`. For 32-byte alignment: `x * 2 ≡ 0 (mod 32)`,
so `x ≡ 0 (mod 16)`. Same constraint as general case.

### 5.3 Update touching the logical right edge

`rect->x + rect->w == logical_w`. The destination offset is still
`vram + y * backing_pitch + rect->x * 2`. The row data is `rect->w * 2` bytes.
Alignment depends on `rect->x`. Right-edge touching does not change alignment
constraints.

POT padding to the right of the logical texture retains previous contents.
The upload does not touch it.

### 5.4 Update touching the logical bottom edge

`rect->y + rect->h == logical_h`. Same alignment analysis as above. Bottom
padding rows retain previous contents.

### 5.5 One-pixel-wide updates

`rect->w == 1`. Row data: 2 bytes. Not multiple of 32. Requires padding to 32
bytes or custom store loop.

Destination: `vram + y * backing_pitch + x * 2`. For `x=0`: 32-aligned. For
`x!=0`: may not be 32-aligned.

### 5.6 One-pixel-high updates

`rect->h == 1`. Single row. Same alignment analysis as above.

### 5.7 Odd source pitch

Source pitch may be odd (e.g., 641 bytes for a 320-pixel-wide 16-bit image).
The current `UpdateTexture` loop handles this correctly by advancing source by
`pitch` and destination by `cpu_pitch`. The staging buffer copy is correct.

For partial-upload strategies, the source-pitch handling is the same: copy from
source at `pitch` stride into temp buffer at `w * 2` stride.

### 5.8 Source pitch larger than rectangle width

Already handled by the current implementation. The loop advances source by
`pitch` and destination by `cpu_pitch`. Extra bytes are skipped. Correct.

### 5.9 NPOT logical width with larger POT backing pitch

Logical width `w < backing_w`. The staging buffer uses `backing_w * 2` pitch.
The source may have a different pitch. The copy loop handles this correctly.

For partial uploads to VRAM: destination offset is based on `backing_w * 2`
pitch. Alignment constraint applies to `backing_w`, not `logical_w`.

### 5.10 Unaligned source pointer

SDL validates `pixels != NULL`. The source is typically aligned (application
buffers are usually aligned). For unaligned sources, `sq_cpy` uses the slow
path (individual stores). Acceptable but slower.

### 5.11 Destination offset not aligned to 32 bytes

Cannot use `pvr_txr_load`, `sq_cpy`, `pvr_dma_transfer`, or any KOS API that
requires 32-byte-aligned destination. Requires custom store loop.

### 5.12 Row byte count not divisible by 32

Cannot use `pvr_txr_load` or `sq_cpy` directly. Requires padding to 32 bytes
(or custom code). Padding writes garbage to adjacent VRAM bytes. If adjacent
texels exist, this corrupts them. If adjacent bytes are padding, no corruption.

**Padding safety:** For a row at the right edge of the logical texture, the
padding bytes to the right are outside the logical texture and may be safely
overwritten (they are never sampled). For a row in the middle, padding bytes
to the right are part of the next logical column and must not be corrupted.

**Conclusion:** Padding is safe only for rows at the right edge. For interior
rows, padding corrupts adjacent texels. Custom alignment code is required.

### 5.13 Zero-area update

`rect->w <= 0` or `rect->h <= 0`. SDL intersects and rejects empty rectangles
before reaching the backend. The backend never receives zero-area updates.

### 5.14 Invalid update rectangle

SDL intersects against texture bounds. The backend receives a valid rect
within `[0, logical_w) × [0, logical_h)`. No out-of-bounds writes.

### 5.15 Overflow in row and offset calculations

`rect->y * cpu_pitch + rect->x * 2` for max 1024×1024:
`1023 * 2048 + 1023 * 2 = ~2 MB`. Fits in `size_t`. No overflow.

`backing_w * backing_h * 2` for max 1024×1024: `2 MB`. Fits in `size_t`. No
overflow.

### 5.16 Formats with two bytes per pixel

All current formats are 16-bit (2 bytes per pixel). The analysis above applies
directly. No format-specific alignment differences.

### 5.17 Lock rectangles whose returned pointer is inside the CPU backing surface

The returned pointer is `(Uint8 *)data->pixels + rect->y * cpu_pitch + rect->x * 2`.
This is inside the CPU staging buffer. The application writes to this pointer.
On unlock, the full backing is uploaded. Correct.

### 5.18 Cases SDL core already rejects

- Empty rectangles (zero area).
- Out-of-bounds rectangles (outside logical texture).
- Non-streaming lock requests.
- NULL rect (SDL uses full texture rect).

The backend should still validate defensively but can rely on SDL's
intersection as the primary validation.

### 5.19 Cases the backend must defend against

- `pitch < rect->w * 2` (too-small pitch).
- `data == NULL` (destroyed texture).
- `data->pixels == NULL` or `data->vram == NULL` (invalid state).
- `data->locked` (nested lock).

## 6. Synchronization and renderer safety

### 6.1 When uploads may safely happen

| Context | Safe to upload? | Notes |
|--------|----------------|-------|
| Queued draw commands | Yes | Upload completes before `RunCommandQueue` executes. |
| Active PVR scene | Yes | `pvr_txr_load` does not require active scene. |
| `pvr_scene_begin` | Yes | Upload before begin is fine. Upload during scene is fine. |
| List submission | Yes | `pvr_prim` submission is separate from texture upload. |
| `pvr_scene_finish` | Yes | Upload before finish is fine. |
| `pvr_wait_ready` | Yes | Upload before wait is fine. Wait ensures completion. |
| `SDL_RenderFlush` | Yes | SDL flushes before texture updates. |
| `SDL_RenderPresent` | Yes | Upload before present is fine. |
| Texture destruction | Yes | No upload needed. |
| Renderer destruction | N/A | Renderer shutting down. |

### 6.2 Can VRAM be overwritten while a previously queued draw references the texture?

**No.** SDL calls `FlushRenderCommandsIfTextureNeeded(texture)` before
`SDL_UpdateTexture`, `SDL_LockTexture`, and `SDL_UnlockTexture`. Queued geometry
referencing the texture is executed before the update.

**Source confirmation:** `src/render/SDL_render.c` calls
`SDL_FlushRenderer(renderer)` (or equivalent) before texture updates.

### 6.3 Does SDL flush the renderer before `SDL_UpdateTexture` or lock/unlock callbacks?

**Yes.** SDL's `SDL_UpdateTexture` implementation calls
`SDL_FlushRendererIfTextureNeeded` before invoking the backend's
`UpdateTexture` callback. Same for `LockTexture`/`UnlockTexture`.

### 6.4 Must the backend call or require `SDL_RenderFlush` before changing texture VRAM?

**No.** SDL handles flushing. The backend can assume queued commands referencing
the texture have been executed before the update callback runs.

### 6.5 Can partial uploads occur immediately in `UpdateTexture` and `UnlockTexture`?

**Yes.** If partial uploads are implemented, they can occur immediately in
`UpdateTexture` and `UnlockTexture`, just like the current full-backing upload.
SDL's flush ensures ordering.

### 6.6 Should uploads be deferred to `RunCommandQueue` or `RenderPresent`?

**No.** Deferring uploads would require dirty-rectangle tracking and staging
lifetime changes. Immediate upload is simpler and correct. SDL's flush already
ensures ordering.

### 6.7 Would deferring uploads require dirty-rectangle tracking?

**Yes.** If uploads are deferred, the backend must:
- Track which rectangles are dirty.
- Accumulate dirty regions.
- Upload at `RunCommandQueue` or `RenderPresent` time.
- Handle texture destruction while dirty.

This adds complexity without clear benefit. The current immediate-upload
approach is simpler and correct.

### 6.8 Recommended safest architecture

**Immediate upload in `UpdateTexture` and `UnlockTexture`.** Full backing (for
now). No deferral. No dirty-rectangle accumulation. SDL's flush ensures
ordering. This preserves the current architecture.

## 7. Dirty-region design

### 7.1 Options

| Option | Description | Complexity |
|-------|------------|-----------|
| Single update rect | One rectangle per API call. Upload immediately. | Minimal |
| Accumulate rects | Track multiple dirty rects. Upload union. | Moderate |
| Union rects | Merge overlapping rects before upload. | Moderate-High |
| Row-range tracking | Track min/max y rows. Upload row strip. | Low-Moderate |
| Tile/stripe tracking | Track dirty tiles or stripes. | High |
| Immediate upload | No persistent dirty state. Upload in API callback. | Minimal |

### 7.2 Recommendation

**Immediate upload with no persistent dirty state.** The smallest correct
design.

**Rationale:**
- SDL flushes before texture updates. Multiple dirty rectangles from separate
  API calls are already ordered.
- Accumulation provides no benefit if each call uploads immediately.
- Union tracking adds complexity without clear payoff for the current
  workload.
- Row-range tracking is a partial-upload optimization that requires partial
  upload support first.

### 7.3 When accumulation might be useful

If the renderer defers uploads to `RunCommandQueue`, accumulation could reduce
the number of upload operations. But deferral is not recommended (Section 6).

### 7.4 Dirty-state fields

The current `data->dirty` flag is sufficient for the immediate-upload design.
It tracks whether VRAM is out of sync with staging. No additional dirty-region
state is needed.

## 8. Performance analysis

### 8.1 Transfer volume for representative cases

All textures are 16-bit (2 bytes per pixel).

#### 13×9 logical texture backed by 16×16

| Update | Full backing bytes | Update bytes | Reduction |
|-------|-------------------|-------------|-----------|
| Full backing | 512 | 512 | 0% |
| 1×1 update | 512 | 2 | 99.6% |
| 1-row update | 512 | 26 | 94.9% |
| 32×32 update | 512 | — (larger than backing) | N/A |
| 25% area (4×3) | 512 | 24 | 95.3% |
| Full logical (13×9) | 512 | 234 | 54.3% |

#### 320×240 logical texture backed by 512×256

| Update | Full backing bytes | Update bytes | Reduction |
|-------|-------------------|-------------|-----------|
| Full backing | 262,144 | 262,144 | 0% |
| 1×1 update | 262,144 | 2 | 99.999% |
| 1-row update | 262,144 | 640 | 99.7% |
| 32×32 update | 262,144 | 2,048 | 99.2% |
| 25% area (160×120) | 262,144 | 38,400 | 85.4% |
| Full logical (320×240) | 262,144 | 153,600 | 41.4% |

#### 256×256 texture (POT, no backing overhead)

| Update | Full backing bytes | Update bytes | Reduction |
|-------|-------------------|-------------|-----------|
| Full backing | 131,072 | 131,072 | 0% |
| 1×1 update | 131,072 | 2 | 99.998% |
| 1-row update | 131,072 | 512 | 99.6% |
| 32×32 update | 131,072 | 2,048 | 98.4% |
| 25% area (128×128) | 131,072 | 32,768 | 75.0% |
| Full logical (256×256) | 131,072 | 131,072 | 0% |

#### 512×512 texture (POT, no backing overhead)

| Update | Full backing bytes | Update bytes | Reduction |
|-------|-------------------|-------------|-----------|
| Full backing | 524,288 | 524,288 | 0% |
| 1×1 update | 524,288 | 2 | 99.9996% |
| 1-row update | 524,288 | 1,024 | 99.8% |
| 32×32 update | 524,288 | 2,048 | 99.6% |
| 25% area (256×256) | 524,288 | 131,072 | 75.0% |
| Full logical (512×512) | 524,288 | 524,288 | 0% |

### 8.2 Transfer-call overhead

The byte reduction is dramatic for small updates. But each transfer call has
overhead:

- **`pvr_txr_load` call overhead:** Function call, `__align_up`, cast,
  `pvr_sq_load` call, `sq_cpy` call, `sq_lock` mutex acquire, transfer loop,
  `sq_unlock` mutex release. Estimated ~1-5 μs per call on SH-4 at 200 MHz.
- **Per-row overhead for row-by-row upload:** Same as above per row. For 240
  rows, that is 240 × ~2 μs = ~480 μs just in call overhead, before actual
  transfer time.

**Transfer time estimate:** Store queues on SH-4 can sustain ~100 MB/s for
aligned writes. For 262 KB full upload: ~2.6 ms. For 2 bytes (1×1 update):
~0.00002 ms transfer + ~2 μs overhead = dominated by overhead.

**For 240-row partial upload of a 320×240 texture:**
- Partial bytes: 240 × 640 = 153,600 bytes. Transfer time: ~1.5 ms.
- Call overhead: 240 × ~2 μs = ~0.5 ms.
- Total: ~2 ms.
- Full upload: ~2.6 ms.

**Net savings: ~0.6 ms (23% reduction).** But this assumes alignment permits
direct `pvr_txr_load` per row, which requires `x ≡ 0 (mod 16)` and
`w ≡ 0 (mod 16)`. For arbitrary rectangles, custom code is slower.

### 8.3 Practical threshold heuristic

Given the alignment constraints and overhead, a practical heuristic is:

```
use partial rows when:
  - dirty_bytes < 8192  (small update)
  - AND row_count < 64  (not too many rows)
  - AND x ≡ 0 (mod 16)  (alignment permits)
  - AND w ≡ 0 (mod 16)  (row width is transfer-aligned)
  - AND backing_w >= 32 (backing pitch is 32-aligned)
otherwise: upload full backing
```

**These thresholds are illustrative, not calibrated.** Hardware benchmarking
is required to determine actual break-even points. The heuristic should be
tuned after measurement.

### 8.4 Uncertainty

Performance improvement depends on:
- Actual SH-4 store-queue throughput.
- DMA setup cost (if DMA is used).
- Custom code efficiency (if alignment handling is needed).
- Competition with other PVR work (vertex submission, TA traffic).
- Frame-time budget (how much upload time is acceptable).

**Do not assume partial uploads are faster without hardware measurement.**

## 9. CPU copy versus DMA recommendation

### 9.1 Current renderer state

The renderer initializes PVR with zero DMA bins:

```c
static const pvr_init_params_t pvr_params = {
    { PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
    512 * 1024,
    0,  /* No DMA bins. */
    ...
};
```

`pvr_dma_init()` is never called. DMA is unavailable.

### 9.2 SH-4 store queues

Store queues provide synchronous, aligned writes to PVR memory. They bypass
the data cache. Transfer rate is ~100 MB/s for aligned writes. No setup cost
beyond function-call overhead.

### 9.3 DMA characteristics

- Requires `pvr_dma_init()`.
- 32-byte source alignment (stricter than SQ's 8-byte).
- 32-byte destination alignment.
- 32-byte transfer-size minimum.
- Setup cost: register writes, channel configuration.
- Asynchronous mode possible (callback + non-blocking).
- One DMA at a time (mutex via `pvr_dma[PVR_DST]`).
- Transfer rate similar to SQ for large blocks.

### 9.4 Recommendation for first implementation

**CPU/store-queue only. Defer DMA until after correctness is proven.**

**Rationale:**
1. DMA is not initialized in the current renderer. Enabling it requires
   changes to `pvr_init_params` and `DC_PVR_CreateRenderer`.
2. DMA has higher setup cost per transfer. For small rectangles, SQ is faster.
3. DMA adds asynchronous complexity (callbacks, synchronization).
4. SQ is synchronous and simple. Matches the current upload approach.
5. Correctness must be proven before optimization.
6. If partial uploads are pursued later, DMA can be added in a separate
   increment after benchmarks justify it.

### 9.5 When DMA might be justified later

- Large contiguous uploads (full backing or near-full).
- Asynchronous upload to hide latency behind other work.
- Benchmarks show SQ is a bottleneck for the target workload.

**Not in this phase.**

## 10. Focused test design

### 10.1 Test directory

```
test/dreamcast/test/pvr_partial_upload/
├── Makefile.dc
├── README.md
└── pvr_partial_upload.c
```

### 10.2 Test coverage

#### Full update baseline
- Full update on each format. Verify drawn result matches known pattern.

#### One-pixel update
- Update `{0, 0, 1, 1}` on each format. Verify only that pixel changed.
- Update `{logical_w-1, logical_h-1, 1, 1}` (bottom-right corner).
- Update `{logical_w/2, logical_h/2, 1, 1}` (center).

#### One-row update
- Update `{0, 0, logical_w, 1}` (top row).
- Update `{0, logical_h-1, logical_w, 1}` (bottom row).
- Update `{0, y, logical_w, 1}` for interior y.

#### One-column update
- Update `{0, 0, 1, logical_h}` (left column).
- Update `{logical_w-1, 0, 1, logical_h}` (right column).

#### Interior rectangle update
- Update `{w/4, h/4, w/2, h/2}` (center quarter).
- Update `{1, 1, w-2, h-2}` (inset by 1).

#### Right-edge update
- Update `{logical_w - rw, 0, rw, h}` for various `rw`.

#### Bottom-edge update
- Update `{0, logical_h - rh, w, rh}` for various `rh`.

#### Bottom-right corner update
- Update `{logical_w - rw, logical_h - rh, rw, rh}`.

#### NPOT logical texture with POT backing
- 13×9 logical backed 16×16. Multiple partial updates at various offsets.
- 17×9 logical backed 32×16. Multiple partial updates at various offsets.
- 320×240 logical backed 512×256. Multiple partial updates.

#### Formats
- RGB565, ARGB1555, ARGB4444. Same update patterns on each.

#### Access modes
- Static textures: `UpdateTexture` only.
- Streaming textures: `LockTexture` (full and partial), `UnlockTexture`.

#### Source pitch larger than update width
- Provide source buffer with `pitch > rect->w * 2`. Verify correct copy.

#### Repeated updates to different regions
- Update region A, verify. Update region B (disjoint from A), verify A still
  correct and B is updated.

#### Updates followed immediately by drawing
- Update texture, then `RenderCopy` the same texture. Verify result.

#### Update after previous drawing of the same texture
- Draw texture, update it, draw it again. Verify second draw shows updated
  content.

#### Nearest and bilinear filtering at logical edges
- Update a pattern that tests filtering at the logical edge. Verify no
  POT-padding leakage.

#### Proof that untouched texels remain unchanged
- Fill texture with known pattern. Update a small rectangle. Draw full
  texture. Verify untouched regions retain original pattern.

#### Proof that POT padding is not exposed
- Update only the logical region. Draw the full texture. Verify POT padding
  does not leak into the visible area (UV scaling should clamp to logical
  region).

### 10.3 Programmatic checks

- Return codes from `SDL_UpdateTexture`, `SDL_LockTexture`,
  `SDL_UnlockTexture`.
- Test function return value (0 = pass).
- Counters for updates performed, draws performed.

### 10.4 Visual checks

- Untouched texels remain unchanged (color probes at known positions).
- Updated texels have correct values.
- No POT-padding leakage.
- Filtering behavior at edges.
- Ordered updates produce cumulative correct result.

### 10.5 Test structure

One test file with multiple pages/scenes. Each page covers a subset of the
above cases. A final page summarizes counts and returns pass/fail.

## 11. Benchmark design

### 11.1 What to measure

- Full-backing upload time.
- Partial-upload time (if implemented later).
- Per-update time for various rectangle sizes.
- Per-row upload overhead (if row-by-row).

### 11.2 Timing API

`timer_get_ticks()` or `get_timer()` from KOS. High-resolution hardware
timer. Convert to microseconds.

### 11.3 Warm-up

Run 10 warm-up iterations before measurement. Discard warm-up results.

### 11.4 Iteration count

100 iterations per test case. Report median, minimum, maximum.

### 11.5 Synchronization points

- Start timer before upload call.
- End timer after upload returns.
- For full upload: `pvr_txr_load` return.
- For partial upload: last row upload return.

### 11.6 Rendering inclusion

Upload-only. Do not include rendering in measurements. Rendering adds
uncontrollable variance.

### 11.7 Reporting

Print to serial:
```
benchmark: texture=256x256 update=32x32 x=100 y=50
  full_upload: median=XXus min=YYus max=ZZus
  partial_upload: median=XXus min=YYus max=ZZus
```

### 11.8 Avoiding emulator timings

Flycast timings are not hardware timings. Run benchmarks on physical
Dreamcast hardware. Use BBA upload for testing.

### 11.9 Avoiding serial logging dominance

Do not print every iteration. Print only summary statistics. Use a compact
format. Limit total log output.

### 11.10 When to run benchmarks

Only after partial uploads are implemented and correctness is validated.
Benchmarks determine whether the optimization is worthwhile.

## 12. Flycast and hardware validation

### 12.1 Validation sequence

1. **Strict-warning unified build.** Build SDL with `SDL_VIDEO_RENDER_DREAMCAST_PVR=ON`.
   No warnings from changed files.
2. **Focused test build.** Build `pvr_partial_upload` test. Strict warnings.
   Linker map resolves local SDL archive.
3. **Existing native-PVR regression builds.** All existing tests still build.
4. **Flycast API and visual validation.** Run test in Flycast. Check log for
   errors. Inspect rendered frames.
5. **Physical Dreamcast run through BBA.** Upload via
   ```sh
   kos-tool -t 192.168.0.128 \
     -x test/dreamcast/test/pvr_partial_upload/pvr_partial_upload.elf \
     -m .
   ```
   Test must return normally to `dc-load-ip`.
6. **Hardware benchmark.** Run benchmark on physical hardware. Record results.
7. **Optional final hardware screenshot.** Capture PPM for visual reference.
8. **Ordinary rebuild.** Prove no capture instrumentation remains.
9. **PVR opt-out build.** Build with `SDL_VIDEO_RENDER_DREAMCAST_PVR=OFF`.
   No native-PVR objects or symbols.

### 12.2 Hardware command

```sh
kos-tool -t 192.168.0.128 \
  -x test/dreamcast/test/pvr_partial_upload/pvr_partial_upload.elf \
  -m .
```

### 12.3 Acceptance criteria

- Test returns zero.
- Test prints PASS line.
- No SDL, PVR, KOS, or dc-load errors.
- Rendered frames match expected patterns.
- Untouched texels unchanged.
- Updated texels correct.
- No POT-padding leakage.

## 13. Scope boundaries

### 13.1 Explicitly deferred

- Twiddled textures.
- VQ compression.
- Mipmaps.
- Render targets.
- Strided textures.
- Texture format expansion.
- Texture atlases.
- Dirty-region trees.
- Multi-rectangle batching.
- Broad renderer command rearchitecture.
- Unrelated GLdc/FBO work.
- DMA uploads.
- Asynchronous uploads.
- Custom blend modes for uploads.

### 13.2 In scope for later phase (if pursued)

- Partial VRAM uploads via row-by-row transfers.
- Alignment handling for misaligned destinations.
- Temporary upload buffer management.
- Benchmark-driven threshold calibration.
- DMA uploads (separate increment).

## 14. Documentation

### 14.1 Files to update or create

- **`src/render/dreamcast/PVR_TEXTURE_RESEARCH.md`** — Add a section
  recording this research and the recommendation. Cross-reference this
  document.
- **`src/render/dreamcast/PVR_PARTIAL_UPLOAD_RESEARCH.md`** — This document.
  The authoritative reference for partial-upload design decisions.

### 14.2 Do not update

- `AGENTS.md` — This is research only. No completion claims.
- Source files — No implementation changes.
- Test files — No test changes.

## 15. Recommendation

### 15.1 Verdict

**Defer partial uploads to a later optimization phase.**

The first implementation should keep the current full-backing upload through
`pvr_txr_load`. Partial uploads are not practical with the current KOS API
constraints without significant custom alignment code. DMA is not initialized
in the renderer. The performance benefit is uncertain without hardware
benchmarks.

### 15.2 Primary strategy

**Keep full-backing upload.** It is correct, simple, and uses standard KOS
APIs. No alignment issues. No custom code.

### 15.3 Fallback strategy (for later phase)

If benchmarks show full upload is a bottleneck:
1. Implement row-by-row partial uploads with alignment handling.
2. Use a tightly-packed temporary buffer for the source where useful.
3. Calibrate thresholds via hardware benchmarks.
4. Evaluate DMA separately only if hardware profiling shows a need for it;
   DMA does not remove the alignment, transfer-unit, or contiguous-block
   restrictions.

### 15.4 Heuristic (for later phase, uncalibrated)

```
use partial rows when:
  - dirty_bytes < 8192
  - AND row_count < 64
  - AND x ≡ 0 (mod 16)
  - AND w ≡ 0 (mod 16)
  - AND backing_w >= 32
otherwise: upload full backing
```

**Do not implement this heuristic now.** It requires partial-upload support
first, which requires alignment handling, which is the complexity we are
deferring.

### 15.5 DMA placement

**Defer DMA to a later phase.** Not in this increment. DMA requires
`pvr_dma_init()`, which is not enabled. DMA has higher setup cost for small
transfers. SQ is sufficient for current needs.

### 15.6 Dirty-region design

**Immediate upload with no persistent dirty state.** The current `data->dirty`
flag is sufficient. No accumulation. No union tracking. SDL flush ensures
ordering.

### 15.7 Focused test

`test/dreamcast/test/pvr_partial_upload/` covers all cases in Section 10.
Should be implemented when partial uploads are implemented, not now.

### 15.8 Hardware benchmark

Design in Section 11. Run only after partial uploads are implemented and
correctness is validated.

### 15.9 Validation plan

Section 12. Same sequence as other renderer increments.

### 15.10 Files to modify

None now. When partial uploads are pursued later:
- `src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c` — Partial upload
  logic.
- `src/render/dreamcast/SDL_render_dreamcast_pvr_data.h` — Dirty-region state
  (if needed).
- `test/dreamcast/test/pvr_partial_upload/` — Test files.

### 15.11 Implementation steps (for later phase)

1. Add partial-upload helper functions to the texture file.
2. Modify `UpdateTexture` to attempt partial upload with fallback to full.
3. Modify `UnlockTexture` similarly.
4. Build and test.
5. Benchmark on hardware.
6. Calibrate thresholds.
7. Tune the implementation based on measurements.
8. Evaluate DMA in a separate increment only if hardware profiling justifies
   it.

### 15.12 Acceptance criteria (for later phase)

- Partial uploads produce correct results for all test cases.
- Full-backing fallback works for all alignment cases.
- Benchmarks show improvement for targeted update patterns.
- No regression in existing tests.
- Physical hardware validation passes.

### 15.13 Risks and open questions

1. **Alignment complexity:** Custom alignment code is error-prone. Byte-level
   manipulation for misaligned destinations can corrupt adjacent texels.
2. **Performance uncertainty:** Partial uploads may not be faster due to
   per-row overhead. Benchmarks are required.
3. **DMA enablement cost:** Enabling DMA changes PVR initialization. May
   affect other PVR behavior.
4. **Test coverage:** Partial uploads add many edge cases. Test matrix grows.
5. **Maintainability:** Custom alignment code is harder to maintain than
   standard API calls.
6. **Open question:** Are there KOS patches or upstream changes that add
   partial-upload support? (Not researched.)
7. **Open question:** Does Flycast's PVR emulator handle misaligned writes
   differently than hardware? (Requires testing.)
8. **Open question:** What is the actual SH-4 store-queue throughput for
   small transfers? (Requires measurement.)

### 15.14 Documentation created

- `src/render/dreamcast/PVR_PARTIAL_UPLOAD_RESEARCH.md` — This document.
- `src/render/dreamcast/PVR_TEXTURE_RESEARCH.md` — Will be updated with a
  cross-reference to this document.

### 15.15 Next practical work

The next practical texture work is the wider texture-dimension and
logical-edge validation matrix already identified in `PVR_TEXTURE_RESEARCH.md`.
Partial uploads, their focused test, threshold calibration, and DMA remain
deferred optimization work.
