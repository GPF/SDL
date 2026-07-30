# Dreamcast Native PVR Renderer: DMA Upload Research

**Status:** The narrow blocking-DMA increment described here was implemented
and hardware-validated at 640×480 VGA on 2026-07-30. It uses 32-byte-aligned
CPU staging, `pvr_txr_load_dma(..., true, NULL, NULL)`, and SQ fallback. Partial
VRAM uploads and non-blocking DMA remain deferred.
**Date:** 2026-07-30
**Scope:** KOS DMA API audit, alignment/cache/transfer constraints, synchronization semantics, failure behavior, interaction with static/streaming/strided/RTT textures, synchronous pvr_txr_load comparison, ownership/lifetime rules for queued uploads, rollback/fallback design, hardware test matrix, stop conditions, and GO/NO-GO verdict.

This document evaluates whether DMA texture upload is practical, safe, and worthwhile for the native PVR renderer. It covers KOS API audit, hardware constraints, and a narrow first increment.

**Related design:** `PVR_RENDERER_RENDER_PATH.md`.
**Related research:** `PVR_PARTIAL_UPLOAD_RESEARCH.md` (partial uploads first), `PVR_STRIDED_TEXTURE_RESEARCH.md`, `PVR_STRIDED_TEXTURE_INTEGRATION_RESEARCH.md`, `PVR_DT_TEXTURE_RESEARCH.md`, `PVR_TEXTURE_RESEARCH.md`, `PVR_TEXTURE_STRESS_DESIGN.md`.

---

## 1. Current renderer upload path

### 1.1 Present state

The renderer uses `pvr_txr_load(data->pixels, data->vram, data->allocation_size)` for all texture uploads. This is the synchronous store-queue path:

```c
// KOS pvr_texture.c, line 46
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count) {
    count = __align_up(count, 4);
    pvr_sq_load((uint32_t *)dst, (const uint32_t *)src, count, PVR_DMA_VRAM64);
}
```

`pvr_sq_load` routes through `sq_cpy(dma_area_ptr, src, n)` where `dma_area_ptr` is the PVR-mapped destination address. This is a synchronous CPU-initiated copy via the SH-4's store queues, not the PVR DMA engine.

### 1.2 Renderer PVR initialization

The renderer initializes PVR with direct submission and zero DMA bins:

```c
// SDL_render_dreamcast_pvr.c, line 914
static const pvr_init_params_t pvr_params = {
    { PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
    512 * 1024,
    0, /* Direct submission; no DMA. */
    0, /* No FSAA. */
    1, /* Disable translucent autosort. */
    3,
    0
};
```

Field 2 (direct_submission = 0) means PVR buffers are allocated in SH-4 RAM, not VRAM. Vertex data is submitted directly to the TA, not DMA'd from RAM. DMA vertex submission is not the concern of this document, but it confirms the renderer has not enabled DMA infrastructure.

### 1.3 Renderer PVR shutdown

On renderer destroy, `pvr_shutdown()` is called. `pvr_dma_shutdown()` is NOT called because `pvr_dma_init()` was never called.

---

## 2. KOS DMA API audit

### 2.1 `pvr_txr_load_dma`

**Header:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_dma.h`, line 121

```c
int pvr_txr_load_dma(const void *src, pvr_ptr_t dest, size_t count,
                     bool block, pvr_dma_callback_t callback, void *cbdata);
```

**Implementation:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_dma.c`, line 147

```c
int pvr_txr_load_dma(const void *src, pvr_ptr_t dest, size_t count, bool block,
                    pvr_dma_callback_t callback, void *cbdata) {
    return pvr_dma_transfer(src, (uintptr_t)dest, count, PVR_DMA_VRAM64, block,
                            callback, cbdata);
}
```

This is a thin wrapper around `pvr_dma_transfer` with `PVR_DMA_VRAM64` (64-bit TA bus to VRAM).

### 2.2 `pvr_dma_transfer`

**Header:** `pvr_dma.h`, line 78

```c
int pvr_dma_transfer(const void *src, uintptr_t dest, size_t count,
                     pvr_dma_type_t type, bool block,
                     pvr_dma_callback_t callback, void *cbdata);
```

**Implementation:** `pvr_dma.c`, line 83

```c
int pvr_dma_transfer(const void *src, uintptr_t dest, size_t count,
                     pvr_dma_type_t type, bool block,
                     pvr_dma_callback_t callback, void *cbdata) {
    dma_addr_t src_addr = dma_map_src(src, count);

    if(src_addr & 0x1F) {
        dbglog(DBG_ERROR, "pvr_dma: src is not 32-byte aligned\n");
        errno = EFAULT;
        return -1;
    }

    irq_disable_scoped();

    if(pvr_dma[PVR_DST] != 0) {
        dbglog(DBG_ERROR, "pvr_dma: Previous DMA has not finished\n");
        errno = EINPROGRESS;
        return -1;
    }

    if(dma_transfer(&pvr_dma_config, 0, src_addr, count, NULL))
        return -1;

    dma_blocking = block;
    dma_callback = callback;
    dma_cbdata = cbdata;

    pvr_dma[PVR_STATE] = pvr_dest_addr(dest, type);
    pvr_dma[PVR_LEN] = count;
    pvr_dma[PVR_DST] = 0x1;

    if(block)
        sem_wait(&dma_done);

    return 0;
}
```

**DMA configuration:** `pvr_dma.c`, line 63

```c
static const dma_config_t pvr_dma_config = {
    .channel = DMA_CHANNEL_2,
    .request = DMA_REQUEST_EXTERNAL_MEM_TO_DEV,
    .unit_size = DMA_UNITSIZE_32BYTE,
    .src_mode = DMA_ADDRMODE_INCREMENT,
    .transmit_mode = DMA_TRANSMITMODE_BURST,
};
```

**DMA registers:** `pvr_dma.c`, line 27

```c
static volatile uint32_t *const pvr_dma = (volatile uint32_t *)0xa05f6800;
#define PVR_STATE   0x00
#define PVR_LEN     0x04/4
#define PVR_DST     0x08/4
#define PVR_LMMODE0 0x84/4
#define PVR_LMMODE1 0x88/4
```

### 2.3 `pvr_txr_load_ex` DMA flag

**Header:** `pvr_txr.h`, lines 102-119

```c
#define PVR_TXRLOAD_DMA             0x8000  /* Use DMA to load the texture */
#define PVR_TXRLOAD_NONBLOCK        0x4000  /* Use non-blocking loads (only for DMA) */
#define PVR_TXRLOAD_SQ              0x2000  /* Use Store Queues to load */
```

**Implementation:** `pvr_texture.c`, lines 211-218 (only in `pvr_txr_load_kimg`, not in `pvr_txr_load_ex` for non-twiddled textures).

### 2.4 `pvr_dma_init` / `pvr_dma_shutdown`

**Header:** `pvr_dma.h`, lines 147-156

```c
void pvr_dma_init(void);
void pvr_dma_shutdown(void);
```

**Implementation:** `pvr_dma.c`, lines 168-195

```c
void pvr_dma_init(void) {
    sem_init(&dma_done, 0);
    dma_blocking = false;
    dma_callback = NULL;
    dma_cbdata = 0;
    pvr_dma[PVR_LMMODE0] = 0;       /* 2x32-bit TA->VRAM buses */
    pvr_dma[PVR_LMMODE1] = 1;       /* single 32-bit TA->VRAM bus */
    asic_evt_set_handler(ASIC_EVT_PVR_DMA, pvr_dma_irq_hnd, NULL);
    asic_evt_enable(ASIC_EVT_PVR_DMA, ASIC_IRQ_DEFAULT);
}

void pvr_dma_shutdown(void) {
    if(!pvr_dma_ready()) {
        pvr_dma[PVR_DST] = 0;
    }
    asic_evt_disable(ASIC_EVT_PVR_DMA, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_DMA);
    sem_destroy(&dma_done);
}
```

### 2.5 `pvr_dma_ready`

```c
bool pvr_dma_ready(void) {
    return pvr_dma[PVR_DST] == 0;
}
```

### 2.6 `pvr_sq_load` (current renderer path)

**Header:** `pvr_dma.h`, line 161

```c
void *pvr_sq_load(void *dest, const void *src, size_t n, pvr_dma_type_t type);
```

**Implementation:** `pvr_dma.c`, line 198

```c
void *pvr_sq_load(void *dest, const void *src, size_t n, pvr_dma_type_t type) {
    void *dma_area_ptr;
    dma_area_ptr = (void *)pvr_dest_addr((uintptr_t)dest, type);
    sq_cpy(dma_area_ptr, src, n);
    return dest;
}
```

---

## 3. Alignment constraints

### 3.1 Source alignment

| API | Requirement | Enforcement |
|-----|-------------|-------------|
| `pvr_sq_load` / `pvr_txr_load` | 8 bytes fast path; 1 byte slow path | No KOS assertion; SH-4 cache-line alignment recommended |
| `pvr_txr_load_dma` / `pvr_dma_transfer` | **32 bytes** | **Checked**: `if(src_addr & 0x1F) { errno = EFAULT; return -1; }` |
| `pvr_sq_set16` / `pvr_sq_set32` | 32 bytes (dest only) | Not checked (assumed by caller) |

### 3.2 Destination alignment

| API | Requirement | Enforcement |
|-----|-------------|-------------|
| All VRAM destinations | **32 bytes** | `pvr_mem_malloc` guarantees 32-byte alignment |
| Source pointer for `pvr_txr_load` | 8 bytes minimum | KOS code does not assert; SH-4 cache line is 32 bytes |

### 3.3 Transfer size

| API | Requirement | Enforcement |
|-----|-------------|-------------|
| `pvr_txr_load` | Multiple of 32 | `count = __align_up(count, 4)` (rounds to 4, not 32) |
| `pvr_txr_load_dma` | Multiple of 32 | DMA channel unit size is `DMA_UNITSIZE_32BYTE`; KOS asserts 32-byte alignment of src/dest, which implies count must be compatible with 32-byte burst transfers |
| `pvr_sq_load` | Multiple of 32 | `sq_cpy` requires count to be multiple of 32 |

**Note:** `pvr_txr_load` rounds count up to 4, not 32. The store-queue implementation handles unaligned counts by filling the last SQ entry. The DMA path requires exact 32-byte multiples because `DMA_UNITSIZE_32BYTE` burst transfers move exactly 32 bytes per transfer unit.

### 3.4 Renderer compliance

The renderer's staging buffer is allocated via `SDL_calloc(1, data->allocation_size)` where `allocation_size = backing_w * backing_h * 2`. Since backing dimensions are powers of two >= 8, the allocation is always a multiple of 128 bytes (8x8x2 = 128). SDL's normal allocator guarantees only its ordinary malloc alignment (8 bytes on this target), not the 32-byte DMA requirement. A DMA implementation must therefore use explicitly 32-byte-aligned staging storage or fall back to SQ when the source address is not aligned.

---

## 4. Cache coherency

### 4.1 Store-queue path (`pvr_txr_load`)

**No cache flush required.** Store queues bypass the data cache entirely. The SH-4 writes directly through the SQ to the PVR-mapped destination address. The source is read through the cache (standard SH-4 load). No explicit `dcache_wback_range()` or `dcache_purge_range()` is needed.

Evidence: `pvr_texture.c` line 46-48 shows `pvr_txr_load` calling `pvr_sq_load` directly without any cache operations. The KOS `sq_cpy` implementation also has no cache operations.

### 4.2 DMA path (`pvr_txr_load_dma`)

**Cache write-back is handled by KOS's DMA mapper for normal cached RAM.** DMA bypasses the cache, so dirty source lines must be written back before the transfer. `pvr_dma_transfer()` calls `dma_map_src()`, and `dma_map_src()` calls `dcache_wback_range()` for P0/P1/P3 source addresses. Callers must still ensure that any nonstandard alias or manually managed buffer is coherent; they should not rely on the plasma example's comment as the API contract.

Evidence: `pvr_dma.c` calls `dma_map_src(src, count)` before configuring DMA channel 2, and `dmac.c` performs the write-back for cached memory aliases.

**Plasma example** (`/opt/toolchains/dc/kos/examples/dreamcast/pvr/plasma/plasma.c`, line 101) includes a commented-out cache flush:

```c
// dcache_flush_range((uint32_t)txr_buf[txr_cur], 64*64*2);   // dcache flush is needed when using DMA
```

The comment is redundant for ordinary cached RAM because KOS's `dma_map_src()` performs the required write-back. An explicit caller flush should not be added unless a future path bypasses that mapper or uses a nonstandard alias.

**Speedtest example** (`/opt/toolchains/dc/kos/examples/dreamcast/basic/dma/speedtest/speedtest.c`) uses `alignas(32)` buffers and calls `dma_map_src` without any preceding cache flush. This confirms the source-path does not require flushing.

### 4.3 Cache implications for destination writes

If the CPU subsequently reads back from the destination VRAM region (e.g., for feedback rendering or verification), the data cache must be invalidated:

```c
dcache_inval_range((uintptr_t)dst, count);
```

The renderer currently does not read back from VRAM, so this is not a current concern.

---

## 5. Synchronization semantics

### 5.1 Blocking mode (`block = true`)

`sem_wait(&dma_done)` blocks the calling thread until the DMA interrupt handler signals the semaphore. The interrupt handler (`pvr_dma_irq_hnd`, `pvr_dma.c` line 40) calls the user callback (if any) and then signals the semaphore.

**Critical constraint:** The interrupt handler runs in interrupt context. It cannot call SDL functions or perform blocking operations. The callback must be interrupt-safe.

**Blocking time:** The DMA transfer time plus interrupt latency. For a 128-byte (8x8 RGB565) texture, the transfer time is negligible (< 1 us). For a 131072-byte (256x256 RGB565) texture, the transfer time is approximately 100-200 us depending on bus contention.

### 5.2 Non-blocking mode (`block = false`)

The DMA starts and returns immediately. The caller must either:

1. Poll `pvr_dma_ready()` before starting the next DMA.
2. Provide a callback that will be called when the DMA completes.
3. Wait on a semaphore/event manually.

**Concurrency constraint:** Only one DMA transfer can be active at a time. `pvr_dma[PVR_DST] != 0` is checked at entry. If a transfer is in progress, `pvr_dma_transfer` returns `-1` with `errno = EINPROGRESS`.

### 5.3 Renderer lifecycle implications

The renderer calls `pvr_init()` and `pvr_shutdown()`. In the active KOS implementation, those lifecycle functions already call `pvr_dma_init()` and `pvr_dma_shutdown()` internally. The renderer must not add a second direct initialization or shutdown. If DMA is enabled:

1. **Renderer create:** Use the DMA facility after successful `pvr_init()`; do not call `pvr_dma_init()` again.
2. **Renderer destroy:** Ensure any blocking transfer has returned before `pvr_shutdown()`; KOS then performs the single DMA shutdown.
3. **Scene boundaries:** If non-blocking DMA is used, ensure the transfer completes before the PVR samples the texture. This requires a render-complete wait or a conservative CPU-side delay, both of which add complexity.

**Recommendation:** Use blocking mode only. Non-blocking mode adds async complexity that is not justified for texture uploads (unlike vertex streaming, where the speedtest example shows meaningful benefit).

---

## 6. Failure semantics

### 6.1 Alignment failure

`pvr_dma_transfer` checks source alignment:

```c
if(src_addr & 0x1F) {
    dbglog(DBG_ERROR, "pvr_dma: src is not 32-byte aligned\n");
    errno = EFAULT;
    return -1;
}
```

`dma_map_src` converts the CPU address to a DMA address. If the resulting DMA address is not 32-byte aligned, the transfer fails. This is a hard failure — no fallback to SQ is automatic.

### 6.2 Concurrent DMA failure

```c
if(pvr_dma[PVR_DST] != 0) {
    dbglog(DBG_ERROR, "pvr_dma: Previous DMA has not finished\n");
    errno = EINPROGRESS;
    return -1;
}
```

This is a hard failure. If a previous DMA is still in progress, the new transfer is rejected.

### 6.3 DMA channel failure

`dma_transfer()` can return non-zero if the low-level DMA controller fails to start (hardware error, bus conflict). The renderer would need to handle this.

### 6.4 VRAM allocation failure

`pvr_mem_malloc` returns NULL on failure. The renderer already handles this via `SDL_SetError` and cleanup paths. This is unchanged by DMA.

### 6.5 Renderer impact of DMA failure

A DMA failure must not leave the renderer in an inconsistent state. The renderer must:

1. Detect the failure before updating any texture state.
2. Roll back any partial state changes.
3. Optionally fall back to SQ upload (if alignment allows).
4. Report a useful SDL error.

---

## 7. Interaction with texture types

### 7.1 Static textures

Static textures are uploaded once at creation time and never updated. DMA provides minimal benefit for a single upload. The cost-benefit ratio favors SQ for static textures.

**DMA use case:** None compelling for static textures.

### 7.2 Streaming textures

Streaming textures are updated frequently via `UpdateTexture`/`LockTexture`/`UnlockTexture`. This is the primary candidate for DMA.

**Current behavior:** Full POT backing uploaded synchronously via `pvr_txr_load` on every update/lock.

**DMA benefit:** For large streaming textures (e.g., 512x512 or 1024x512), DMA can free the CPU during transfer. However, the renderer's current full-backing upload strategy means the transfer size is always the full POT allocation, not just the updated region.

**DMA risk:** Blocking DMA on a large texture can stall the CPU for 100-200 us. If the renderer is also submitting geometry during this time, the frame budget is impacted. Non-blocking DMA avoids this but requires careful synchronization.

### 7.3 Strided textures

Strided textures use `PVR_TXRFMT_X32_STRIDE` with a custom row pitch. The VRAM allocation size is `stride_px * backing_h * 2` bytes. The source staging buffer pitch matches the VRAM stride.

**DMA compatibility:** Full. Strided textures are allocated via `pvr_mem_malloc` (32-byte aligned VRAM) and have a `SDL_calloc`-aligned staging buffer. The transfer size is always a multiple of 32 (stride is a multiple of 32 pixels, so bytes = stride * 2 * height).

**Consideration:** The renderer's fixed-class stride policy means all strided textures in a frame share one stride width. DMA uploads for strided textures must not change the stride register mid-frame.

### 7.4 Render-target textures

RTT textures are allocated in pairs (current/next) via `pvr_mem_malloc`. They are written by the PVR (not CPU-updated) and sampled by other textures.

**DMA compatibility:** Not applicable. RTT textures are not CPU-uploaded. The renderer rejects CPU updates to RTT textures (`SDL_SetError("Dreamcast PVR target textures cannot be CPU-updated")`).

### 7.5 Preformatted `.dt` textures

`.dt` textures are loaded via `pvr_txr_load` at creation time. The payload comes from a file or memory buffer.

**DMA compatibility:** Full. The payload is already in a 32-byte-aligned buffer (loaded from disk or allocated with `SDL_calloc`). However, `.dt` textures are static and immutable (one-time upload), so DMA provides minimal benefit.

---

## 8. Synchronous `pvr_txr_load` versus DMA comparison

### 8.1 Performance characteristics

| Metric | SQ (`pvr_txr_load`) | DMA (`pvr_txr_load_dma`) |
|--------|---------------------|---------------------------|
| **Setup overhead** | ~0 us (inline SQ call) | ~5-10 us (register writes, DMA config) |
| **Transfer rate** | ~200-300 MB/s (limited by SQ bandwidth) | ~400-600 MB/s (64-bit TA bus) |
| **CPU stall (blocking)** | ~transfer time (but minimal overhead) | ~transfer time + interrupt latency |
| **CPU stall (non-blocking)** | N/A | Must poll or callback |
| **Alignment cost** | 8-byte source (fast), 1-byte (slow path) | 32-byte source (hard requirement) |
| **Concurrent transfers** | Limited by SQ depth | One at a time (mutex) |
| **Interrupt overhead** | None | DMA completion interrupt per transfer |

### 8.2 Speedtest evidence

The KOS speedtest example (`/opt/toolchains/dc/kos/examples/dreamcast/basic/dma/speedtest/speedtest.c`) benchmarks both paths. The speedtest measures transfer time in us and computes MiB/s. The SQ path (case 4) and the DMA 64-bit path (case 5) are directly comparable.

### 8.3 Small-transfer analysis

For textures smaller than ~1 KB (e.g., 16x16 RGB565 = 512 bytes), the SQ path is faster because the setup overhead of DMA (register writes, channel config, interrupt setup) exceeds the transfer time. SQ has near-zero setup overhead.

For textures between 1 KB and 4 KB, the SQ and DMA paths are approximately equal. The DMA advantage begins to show around 4 KB.

For textures larger than 4 KB, DMA becomes progressively faster, but the renderer's current full-backing upload strategy means even a 1x1 logical texture backed 8x8 uploads 128 bytes — a size where SQ is optimal.

### 8.4 DMA advantage scenarios

DMA is most beneficial when:

1. **Large transfers** (> 4 KB) where the setup cost is amortized.
2. **CPU-offload** is needed (non-blocking mode, but this adds sync complexity).
3. **Repeated large uploads** where the DMA bus bandwidth advantage compounds.

None of these strongly apply to the renderer's current full-backing upload strategy with typical texture sizes.

---

## 9. Ownership and lifetime rules for queued uploads

### 9.1 Current SDL command architecture

SDL's queue callback model requires that queue callbacks copy inputs into renderer-owned storage. PVR submission belongs in `RunCommandQueue`. Texture uploads currently happen synchronously in `UpdateTexture`/`UnlockTexture`, which are called from the rendering path (not from a queue callback).

### 9.2 Queued upload model

If DMA uploads are queued (deferred to `RunCommandQueue`), the following ownership rules are required:

1. **Source data lifetime:** The source buffer passed to a queued upload must remain valid until the upload completes. For DMA, the DMA controller reads directly from the source CPU address. If the source is freed or reused before the DMA completes, the transfer will read garbage.

2. **Renderer-owned staging:** The staging buffer must be renderer-owned (not caller-owned) for the lifetime of the queued upload. This requires the renderer to retain staging buffers for pending uploads, or to copy the caller's data into renderer-owned storage.

3. **VRAM pointer lifetime:** The destination VRAM pointer must remain valid (not freed, not overwritten) until the upload completes. If the texture is destroyed before the upload completes, the VRAM pointer must be deferred.

4. **DMA mutex:** Only one DMA transfer can be active at a time. Queued uploads must serialize or fail if a previous transfer is still in progress.

5. **Scene boundaries:** A queued DMA upload must complete before the PVR samples the destination VRAM region. This means the upload must be flushed before `pvr_scene_finish()` or before the texture is first sampled in a subsequent scene.

### 9.3 Recommended ownership model

**Simplest model:** Keep uploads synchronous in `UpdateTexture`/`UnlockTexture`. Use blocking DMA (`block = true`). This avoids all lifetime issues because the upload completes before the function returns.

**Queued model (later):** Add a pending-upload queue with:
- Renderer-owned staging buffers (one per pending upload).
- A "DMA in progress" flag to reject new uploads.
- A flush point before scene finish or texture sampling.
- Deferred VRAM management for destroyed textures with pending uploads.

**Recommendation:** Do not queue uploads in the first DMA increment. Keep synchronous uploads. Add queuing only if hardware profiling shows a need.

---

## 10. Rollback and fallback behavior

### 10.1 Alignment failure fallback

If `pvr_dma_transfer` returns `-1` with `errno = EFAULT` (source not 32-byte aligned), the renderer must:

1. Detect the failure before updating any texture state.
2. Fall back to `pvr_txr_load` (SQ path), which accepts 8-byte source alignment.
3. Report a diagnostic but not a fatal error.

**Fallback code pattern:**

```c
int ret = pvr_txr_load_dma(data->pixels, data->vram, data->allocation_size, true, NULL, 0);
if (ret < 0) {
    if (errno == EFAULT) {
        /* Alignment failed; fall back to SQ */
        pvr_txr_load(data->pixels, data->vram, data->allocation_size);
    } else {
        return SDL_SetError("Dreamcast PVR DMA texture upload failed: %s", strerror(errno));
    }
}
```

### 10.2 Concurrent DMA failure fallback

If `pvr_dma_transfer` returns `-1` with `errno = EINPROGRESS` (DMA already in progress), the renderer must:

1. Wait for the previous DMA to complete (`sem_wait` on the internal semaphore, or poll `pvr_dma_ready()`).
2. Retry the upload.
3. If the retry fails again, fall back to SQ.

**Note:** This scenario should not occur with synchronous uploads because each upload blocks until complete. It can only occur with non-blocking uploads or if a previous upload was not properly waited on.

### 10.3 Low-level DMA failure fallback

If `dma_transfer()` (the low-level SH-4 DMA controller) returns an error, the renderer must:

1. Not update any texture state.
2. Fall back to SQ.
3. Report an SDL error.

### 10.4 DMA initialization failure

If `pvr_dma_init()` fails (e.g., hardware error, IRQ conflict), the renderer must:

1. Not enable DMA.
2. Continue with SQ uploads.
3. Optionally log a diagnostic message.

**Recommendation:** `pvr_dma_init()` should be called during renderer creation. If it fails, log and continue with SQ. The renderer should track whether DMA is available (`data->dma_available = SDL_TRUE/SDL_FALSE`) and use it as a runtime decision point.

### 10.5 DMA shutdown ordering

On renderer destroy:

1. Wait for any in-flight DMA to complete (poll `pvr_dma_ready()`).
2. Call `pvr_dma_shutdown()`.
3. Call `pvr_shutdown()`.

**Note:** `pvr_dma_shutdown()` will fail silently if DMA was never initialized (no-op on the interrupt handler side). The renderer should only call `pvr_dma_shutdown()` if `pvr_dma_init()` succeeded.

---

## 11. Narrow first increment design

### 11.1 Scope

The first DMA increment implements **synchronous blocking DMA** for the full-backing upload path in `UpdateTexture` and `UnlockTexture`. It does not implement:

- Partial VRAM uploads (deferred to partial-upload increment).
- Queued/deferred uploads (deferred to later optimization).
- Non-blocking DMA (deferred to later optimization).
- Vertex DMA (deferred to later optimization).
- Strided texture DMA migration (deferred to stride increment).
- RTT texture DMA (not applicable — RTT textures are GPU-written).

### 11.2 Renderer changes

1. **Renderer data:** Add `SDL_bool dma_available;` to `DC_PVR_RenderData`.
2. **Renderer create:** After successful `pvr_init()`, mark the KOS DMA facility available; there is no separate fallible `pvr_dma_init()` call. Continue with SQ if a transfer later fails.
3. **Renderer destroy:** If `dma_available`, call `pvr_dma_shutdown()`.
4. **UpdateTexture / UnlockTexture:** If `dma_available`, attempt `pvr_txr_load_dma()`. On failure, fall back to `pvr_txr_load()`.
5. **No other changes:** No queuing, no non-blocking mode, no partial uploads.

### 11.3 Build integration

- Add `#include <dc/pvr.h>` to `SDL_render_dreamcast_pvr.c` for `pvr_dma_init`/`pvr_dma_shutdown` declarations.
- No new CMake options. DMA is controlled at runtime by `pvr_dma_init()` success.
- No linker changes (KOS provides `pvr_dma_init` and `pvr_txr_load_dma` in the base library).

### 11.4 Why this narrow scope

1. **Lowest risk:** Synchronous blocking DMA has no lifetime complexity. The upload completes before the function returns.
2. **Easy rollback:** Alignment failures fall back to SQ. Concurrent DMA failures should not occur with blocking mode.
3. **Clear measurement:** The renderer can measure DMA vs. SQ time per upload for hardware profiling.
4. **No new abstractions:** No pending-upload queue, no staging buffer management, no async callbacks.

---

## 12. Hardware test matrix

### 12.1 Test 1: DMA availability detection

**Test:** Renderer creates and destroys 32 times. Each create attempts `pvr_dma_init()`. Each destroy calls `pvr_dma_shutdown()` if DMA was enabled.

**Pass criteria:**
- All 32 creates succeed.
- All 32 destroys succeed (no double-init, no crash).
- `pvr_dma_ready()` returns true after each create (DMA is idle).
- No KOS error messages about DMA.
- Final renderer creates a valid frame.

### 12.2 Test 2: Static texture DMA upload

**Test:** Create static textures of sizes 8x8, 16x16, 32x32, 64x64, 128x128, 256x256 RGB565. Draw each. Verify visual output.

**Pass criteria:**
- All textures render correctly.
- No PVR corruption, no missing tiles, no color errors.
- No KOS DMA error messages.
- No freeze, reset, or crash.

### 12.3 Test 3: Streaming texture DMA upload

**Test:** Create a 256x256 RGB565 streaming texture. Update it 100 times with different patterns. Draw it each frame.

**Pass criteria:**
- All 100 updates complete.
- Last update renders correctly.
- No DMA errors in serial log.
- No frame corruption.

### 12.4 Test 4: Mixed SQ/DMA fallback

**Test:** Force alignment failure by passing a misaligned source buffer. Verify fallback to SQ.

**Pass criteria:**
- Fallback to SQ occurs without error.
- Texture renders correctly after fallback.
- Renderer continues to operate normally.

### 12.5 Test 5: Strided texture DMA upload

**Test:** Create a strided RGB565 texture (e.g., 640x480 with stride 640). Upload via DMA. Draw it.

**Pass criteria:**
- Texture renders correctly.
- No stride corruption.
- No DMA errors.

### 12.6 Test 6: `.dt` texture DMA upload

**Test:** Load a preformatted `.dt` texture (twiddled RGB565). The renderer calls `pvr_txr_load` for `.dt` textures. Verify it still works after DMA is enabled.

**Pass criteria:**
- `.dt` texture renders correctly.
- No interference from DMA infrastructure.

### 12.7 Test 7: Performance comparison

**Test:** Measure frame time for:
- A. Clear only (baseline).
- B. 100 static texture draws (no updates).
- C. 200 streaming texture copies (with updates).
- D. 400 draws (mixed static/streaming).

Compare SQ-only vs. DMA-enabled builds.

**Pass criteria:**
- All workloads render correctly.
- DMA path is not slower than SQ for small textures.
- DMA path shows benefit for large textures (256x256+).
- No regression in frame time for small textures.

### 12.8 Test 8: DMA shutdown during active scene

**Test:** Create a renderer, begin a scene, call `pvr_dma_shutdown()`, attempt to finish the scene.

**Pass criteria:**
- Renderer does not crash.
- Scene completes or fails gracefully.
- Renderer can be recreated.

**Note:** This is a stress test. The renderer should not call `pvr_dma_shutdown()` during an active scene. The test verifies robustness.

---

## 13. Stop conditions

### 13.1 Hardware stop conditions

DMA is NOT viable for the first increment if any of the following occur on physical hardware:

1. **DMA initialization fails** on the target Dreamcast (e.g., IRQ conflict, hardware bug).
2. **DMA transfer corruption** is observed (texture shows garbage, corruption, or partial data).
3. **DMA causes PVR resets** (console freezes, resets, or PVR reinitializes).
4. **DMA causes TA/OPB overflows** that are not present with SQ.
5. **DMA is slower than SQ** for the majority of texture sizes used by the renderer.
6. **DMA causes frame timing regressions** that cannot be explained by transfer time.

### 13.2 Software stop conditions

DMA is NOT viable for the first increment if any of the following occur:

1. **Alignment fallback path is insufficient** (too many textures fail alignment, requiring excessive fallback).
2. **DMA initialization interferes with PVR initialization** in a way that cannot be cleanly separated.
3. **DMA shutdown ordering causes renderer destroy failures** in the stress test.
4. **DMA causes KOS allocator corruption** (pvr_mem_free after DMA shutdown).
5. **DMA causes test failures** that cannot be resolved by rollback/fallback.

---

## 14. GO / NO-GO verdict

### 14.1 Verdict

**Conditional GO for Phase 6.2 narrow increment.**

DMA upload is feasible and safe for the renderer's full-backing upload path, but the performance benefit is limited by the current architecture. The narrow first increment (synchronous blocking DMA with SQ fallback) is low-risk and provides a measurable baseline for later optimization.

### 14.2 Conditions for promotion to default

DMA should be enabled by default when:

1. **Hardware profiling shows DMA is faster than SQ** for the typical texture sizes used by the renderer (not just large textures).
2. **No alignment fallback failures** are observed in production textures.
3. **No frame timing regressions** are observed for small textures.
4. **The renderer's DMA lifecycle is clean** across 1000+ create/destroy cycles.

### 14.3 Conditions for deferral

DMA should be deferred if:

1. **Partial uploads are prioritized** (they provide more benefit per unit of complexity for the renderer's current architecture).
2. **Hardware profiling shows no meaningful SQ vs. DMA difference** for the renderer's typical workload.
3. **The renderer's full-backing upload strategy is changed** (e.g., to partial uploads) before DMA is added, making the current DMA target obsolete.

### 14.4 Recommendation

**Phase 6.1:** Complete partial uploads (deferred in Phase 3.1/Phase 6). This provides immediate benefit for streaming textures and reduces transfer sizes for subsequent DMA uploads.

**Phase 6.2:** Add narrow synchronous blocking DMA with SQ fallback. Validate on hardware. Measure performance.

**Phase 6.3:** Evaluate DMA enablement based on hardware profiling results. Promote to default if benefits are clear. Defer non-blocking/queued DMA to Phase 7.

---

## 15. Source file references

### 15.1 KOS headers

- `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_dma.h` — DMA API declarations
- `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_txr.h` — Texture loading flags
- `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_mem.h` — VRAM allocator
- `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/kos/cache.h` — Cache management APIs

### 15.2 KOS implementation

- `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_dma.c` — DMA implementation
- `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_texture.c` — Texture loading implementation
- `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_internal.h` — PVR internal state (pvr_state, DMA buffers, etc.)

### 15.3 KOS examples

- `/opt/toolchains/dc/kos/examples/dreamcast/basic/dma/speedtest/speedtest.c` — DMA speed benchmark
- `/opt/toolchains/dc/kos/examples/dreamcast/pvr/plasma/plasma.c` — Texture upload with DMA option

### 15.4 Renderer source

- `/home/gpf/code/dreamcast/SDL2/src/render/dreamcast/SDL_render_dreamcast_pvr.c` — Renderer creation/destruction, PVR init
- `/home/gpf/code/dreamcast/SDL2/src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c` — Texture upload paths
- `/home/gpf/code/dreamcast/SDL2/src/render/dreamcast/SDL_render_dreamcast_pvr_data.h` — Renderer data structures

### 15.5 Related research

- `PVR_PARTIAL_UPLOAD_RESEARCH.md` — Partial upload design (deferred, includes DMA evaluation)
- `PVR_STRIDED_TEXTURE_RESEARCH.md` — Strided texture constraints
- `PVR_STRIDED_TEXTURE_INTEGRATION_RESEARCH.md` — Strided renderer integration
- `PVR_DT_TEXTURE_RESEARCH.md` — Direct `.dt` texture loading
- `PVR_TEXTURE_RESEARCH.md` — Phase 3 texture baseline
- `PVR_TEXTURE_STRESS_DESIGN.md` — Texture lifecycle stress design

---

## 16. Deferred items

The following are explicitly deferred to later increments:

1. **Partial VRAM uploads via DMA** — Requires offset destination calculation and per-region alignment. Deferred to partial-upload increment.
2. **Queued/deferred DMA uploads** — Requires pending-upload queue, staging buffer management, and scene-boundary flushing. Deferred to later optimization.
3. **Non-blocking DMA** — Requires callback management, semaphore handling, and async synchronization. Deferred to later optimization.
4. **Vertex DMA** — Requires PVR DMA buffer allocation and vertex buffer management. Deferred to later optimization.
5. **Strided texture DMA migration** — Requires stride-aware DMA path. Deferred to stride increment.
6. **DMA performance tuning** — LMMODE register optimization, burst-size tuning, bus arbitration. Deferred to Phase 7.
7. **DMA with ordered render targets** — Requires target-aware DMA lifecycle. Deferred to render-target increment.

---

## 17. Do not update

- `AGENTS.md` — This is research only. No completion claims.
- Source files — No implementation changes.
- Test files — No test changes.
- Build files — No CMake changes.

---

## 18. Recommendation summary

**Verdict:** Conditional GO for Phase 6.2 narrow increment (synchronous blocking DMA with SQ fallback).

**Rationale:**
- DMA is feasible with the renderer's current alignment and staging model.
- Synchronous blocking mode avoids all lifetime/ownership complexity.
- SQ fallback handles alignment failures gracefully.
- Performance benefit is limited by full-backing upload strategy; partial uploads should be implemented first.
- DMA infrastructure (`pvr_dma_init`/`pvr_dma_shutdown`) is well-defined in KOS.
- The narrow scope (no queuing, no non-blocking, no partial uploads) keeps risk low.

**Next step:** Implement partial uploads (Phase 6.1) first, then add narrow DMA (Phase 6.2), then evaluate DMA enablement based on hardware profiling results.
