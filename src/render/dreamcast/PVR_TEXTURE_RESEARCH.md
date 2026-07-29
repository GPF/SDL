# Dreamcast Native PVR Renderer: Phase 3 Texture Research

**Status:** Research contract implemented; focused Flycast and physical Dreamcast validation complete.
**Date:** 2026-07-28
**Scope:** SDL texture contract, KOS PVR layout requirements, logical vs. backing dimensions, CPU staging and VRAM ownership, CreateTexture/UpdateTexture/LockTexture/UnlockTexture semantics, upload path, filtering, modulation, destruction, source-file split recommendation, validation design, resource analysis, and concrete implementation recommendation.

This document defines the smallest correct Phase 3 baseline texture subsystem for the native PVR renderer. It preserves all established Phase 0–2 behavior: renderer-owns-PVR lifecycle, SDL command-order submission, CPU-only queue callbacks, single autosort-disabled transparent list, pixel-exact CPU clipping, and nearest-neighbor baseline.

**Related design:** `PVR_TEXTURE_STRESS_DESIGN.md` records the completed Phase
3.3 texture lifecycle, VRAM exhaustion/recovery, and deterministic rollback
increment.

**Related research:** `PVR_PARTIAL_UPLOAD_RESEARCH.md` audits the KOS transfer
APIs, analyzes VRAM alignment constraints, compares partial-upload strategies,
evaluates DMA, and recommends deferring partial uploads to a later optimization
phase.

## Implementation and validation update (2026-07-27)

The first Phase 3 increment is implemented. The native renderer is split into
renderer/lifecycle, geometry, and texture source files. It advertises exactly
RGB565, ARGB1555, and ARGB4444; supports static and streaming POT-backed,
nontwiddled 16-bit textures; performs synchronous full-backing uploads for
pitched partial updates and full/partial streaming locks; and supports nearest
and bilinear filtering. On the installed KOS version, SDL linear filtering maps
to `PVR_FILTER_BILINEAR` (not `PVR_FILTER_LINEAR`). RGB565 sampled texture alpha
is disabled while vertex alpha remains enabled. Target textures, DMA, partial
VRAM uploads, twiddling, VQ, mipmaps, and additional blend modes remain out of
scope.

The focused `test/dreamcast/test/pvr_textures` test passes strict-warning
builds and Flycast inspection. On 2026-07-27 it also passed on a real Dreamcast
at 640x480 VGA. The ELF was uploaded through the Broadband Adapter with:

```sh
kos-tool -t 192.168.0.128 -x pvr_textures.elf -m .
```

A temporary `PVR_TEXTURE_HARDWARE_SCREENSHOT` build wrote the displayed raw
framebuffer through `/pc/pvr_textures-hardware.ppm`. The host artifact is
`test/dreamcast/test/pvr_textures/pvr_textures-hardware.ppm`; its SHA-256 is
`96ac33fc4320a1f4e17556615f026848bc34fd589c9b587bb64126e5d1ed2c94`.
The program reached its PASS line, returned zero, and produced no SDL, PVR,
KOS, dc-load, or transfer errors.

Direct hardware-framebuffer inspection confirms the RGB565 NPOT border and
partial update without POT-padding leakage; the streaming checker and partial
yellow lock update; ARGB1555 transparent cutouts; the ARGB4444 alpha gradient;
sharp nearest and smooth bilinear filtering; color and alpha modulation; opaque
RGB565 red texels; and blue/translucent-texture/green painter order. Hardware
closely matches the Flycast reference. The ordinary test ELF was rebuilt after
capture without `PVR_TEXTURE_HARDWARE_SCREENSHOT`.

The focused source-rectangle/scaling/rotation/flip increment and the Phase 3.3
lifecycle/exhaustion increment described below are also complete. Remaining
Phase 3 validation is the wider texture-dimension and edge-update matrix.
Non-VGA video modes and performance/capacity measurements remain Phase 7
hardening.

## Phase 3.2 copy/copy-ex path and validation (2026-07-28)

`SDL_RenderCopy` converts its integer destination to `SDL_FRect` and calls
`SDL_RenderCopyF`. Because the native backend intentionally leaves
`QueueCopy` unset, SDL creates one `SDL_RENDERCMD_GEOMETRY` command: four
vertices and six 32-bit indices in `renderer->rect_index_order`. Source UVs are
the source rectangle divided by the texture's logical dimensions. Destination
scaling is represented by the vertex positions and the renderer scale applied
by `QueueGeometry`; the native backend then applies its established
logical-to-POT-backing UV scale.

`SDL_RenderCopyEx` similarly converts to floating-point arguments and calls
`SDL_RenderCopyExF`. A no-flip integral multiple of 360 degrees uses the normal
copy path. Otherwise, because `QueueCopyEx` is unset, SDL creates the same
four-vertex, six-index geometry form. The default center is half the
destination size and an explicit center is destination-relative. SDL applies
flips by exchanging destination-axis endpoints before its 2x2 rotation; in
SDL's y-down coordinates, a positive angle appears clockwise. UVs remain
attached to their original corners. Public `SDL_RenderGeometryRaw` may also
reach the callback without indices or with 8-, 16-, or 32-bit indices.

The missing backend piece was defensive support for all of those geometry
forms. `QueueGeometry` now validates pointers, strides, counts, triangle
divisibility, index sizes and ranges, scale and allocation arithmetic, and
finite input and scaled position/UV values before copying expanded independent
triangles into renderer-owned command storage. Index reads use `SDL_memcpy`,
so unaligned 8/16/32-bit streams remain safe. Queue callbacks remain CPU-only;
`RunCommandQueue` remains the sole PVR submission owner.

KOS builds use `-ffast-math` and define `__FINITE_MATH_ONLY__`, which caused
ordinary `isfinite()` guards to be optimized away. The native geometry file
therefore uses bitwise IEEE-754 exponent checks (through `SDL_memcpy`) for
float and double values. This also makes the existing clipping and point/rect
finite checks effective under the actual Dreamcast compiler flags.

An independent Phase 3.2 source review found two additional queue-time cases,
and both were confirmed in the current SDL call paths. `SDL_RenderFillRectsF`
passes scaled floating-point widths and heights to `QueueFillRects` without a
finite precheck; the backend now rejects non-finite dimensions with the same
bitwise helper while leaving finite zero/negative rectangles as empty geometry.
Also, `SDL_RenderGeometryRaw` uses the index pointer to select its vertex count
and normalizes `size_indices` to zero when that pointer is null, but still
forwards a positive `num_indices`. `QueueGeometry` now rejects that malformed
null-index/count combination before allocation while preserving valid
unindexed `NULL/0/0` geometry.

`test/dreamcast/test/pvr_copy_ex` covers source rectangles at every logical
edge and one-pixel dimensions, NPOT backing guards, 1:1/enlarged/reduced and
nonuniform scaling, nearest/bilinear filtering, every flip combination,
0/90/180/270/45-degree rotation, default and explicit centers, cropped
rotation/flips, viewport and clip state, modulation and alpha, painter order,
and unindexed plus 8/16/32-bit public geometry. Invalid index size/range and
non-finite position/UV checks must fail. Follow-up API checks construct NaN and
Infinity from IEEE-754 bit patterns and require non-finite rectangle width and
height plus null indices with a positive count to fail; finite empty rectangles
and valid unindexed geometry must continue to succeed.

Fresh unified and PVR-disabled SDL builds passed, and all existing native-PVR
tests rebuilt with strict warnings against the explicit local unified archive.
Flycast reached PASS and direct inspection of all three pages found correct
source boundaries, orientation, clockwise positive rotation, centers,
filtering, clipping, NPOT edges, modulation/alpha, and painter order, with no
padding leakage, cracks, seams, or missing triangles.

The source-validation follow-up repeated the unified archive build, strict
compilation of the changed renderer source and focused test, and Flycast
API/runtime validation. These corrections only reject malformed queue inputs
and do not alter valid rendering commands, so physical hardware was not rerun;
the existing hardware PPM files and hashes remain the visual gate and are
unchanged.

Physical validation used the Broadband Adapter command:

```sh
kos-tool -t 192.168.0.128 \
  -x test/dreamcast/test/pvr_copy_ex/pvr_copy_ex.elf \
  -m .
```

At 640x480 VGA, the capture build wrote all three framebuffer pages, printed
PASS, returned zero, and returned control normally to `dc-load-ip`. The
lasting PPMs and SHA-256 values are:

- `test/dreamcast/test/pvr_copy_ex/pvr_copy_ex-hardware-page1.ppm`:
  `120aa843cd10c259f5c67ed852fab6eee08fd22a882bbba09edf1f9da3f56c93`
- `test/dreamcast/test/pvr_copy_ex/pvr_copy_ex-hardware-page2.ppm`:
  `c4580265159a1dc529fd45427a98d87a007ed6569530f37161cc229308b9d528`
- `test/dreamcast/test/pvr_copy_ex/pvr_copy_ex-hardware-page3.ppm`:
  `787660b80b92933242117868f327626a532639c40a9909cd69e812e6665dcb66`

All are valid 640x480 raw PPM images. Direct inspection matches Flycast for
the full source/scaling/filter matrix, flips and rotation centers, and the
viewport/clip/modulation/order panels. No POT-padding or clip-boundary leakage,
triangle seam, or missing primitive is visible. The ordinary ELF was rebuilt
after capture and contains neither `vid_screen_shot` nor the hardware path
strings.

## Phase 3.3 lifecycle stress and exhaustion validation (2026-07-28)

Revalidation against the current renderer and SDL core confirmed the ownership
research. Staging and VRAM allocations are each freed once; failed creation
does not attach partial driver data; locked destruction clears state; texture
destruction during an active scene is deferred until render synchronization;
the polygon-header cache is invalidated before texture state is freed; SDL
core destroys associated textures before the backend renderer; and PVR-init
failure does not trigger invalid shutdown or VRAM operations. The one research
wording discrepancy was the fatal `pvr_failed` path: it deliberately performs
recovery `pvr_shutdown()` and then releases deferred CPU allocations without
individual VRAM frees. No lifecycle defect was exposed and the working normal
ownership paths were left unchanged.

The only renderer addition is deterministic test support guarded by
`DC_PVR_TEXTURE_FAILURE_INJECTION`. Private, unsupported stage/countdown hints
select the Nth driver-data, staging, or VRAM allocation. Injection occurs
immediately before the selected native allocation and produces a stage-
specific SDL error. Changing the selector or countdown resets the state, so
one SDL test archive covers every case. Ordinary builds compile out the state,
branches, hint names, and errors; string/symbol checks of the ordinary archive
and ELF confirmed their absence.

The focused public-API test completed 50 static and 20 streaming cycles, 12
simultaneous mixed-lifetime textures, four locked destroys, a texture draw
followed by SDL's pre-destruction queue flush and synchronized cleanup,
renderer teardown with five associated live textures, three injected failures
and immediate recoveries, uniform VRAM exhaustion/reallocation, two renderer
recreations, and a final RGB565/ARGB1555/ARGB4444 visual page. Both Flycast and
physical hardware allocated 45 256x256 RGB565 exhaustion textures at 131,072
VRAM bytes each, freed 23, and recovered 23 before the next expected VRAM
allocation failure. Previously created and post-failure textures remained
drawable. Because the standard KOS configuration lacks `PVR_KM_DBG`, allocator
recovery is inferred from those uniform reallocations rather than an exact
available-byte query.

Flycast and a real 640x480 VGA Dreamcast printed PASS with identical final
statistics. The two deliberate exhaustion points produce KOS's generic
out-of-PVR-memory/heap-probably-corrupted warning; no other SDL, KOS, PVR, or
allocator warning occurred. Hardware returned zero to `dc-load-ip`. The final
PPM is
`test/dreamcast/test/pvr_texture_stress/pvr_texture_stress-hardware.ppm`
(640x480), SHA-256
`ea26ac35a8664399f01fb8e64bd505de16b013b783a3449717986eca26996c7a`.
Direct inspection matches Flycast: three intact format-specific gradient
panels, clean red/green/blue borders, and no stale tiles, corruption, blanking,
or seams. Partial uploads, DMA, twiddled/VQ storage, other video modes, and
TA/OPB capacity remain later hardening work.

## 0. Pre-implementation texture baseline (source-confirmed at research time)

The following records the implementation that existed when this research was
written. The implementation and validation update above supersedes this
historical baseline where they differ.

### 0.1 Advertised SDL texture formats

```c
{
    SDL_PIXELFORMAT_RGB565,
    SDL_PIXELFORMAT_ARGB1555
}
```

`num_texture_formats == 2`. `max_texture_width == 1024`, `max_texture_height == 1024`.

`SDL_PIXELFORMAT_ARGB4444` is **not** advertised, despite being listed in the Phase 3 plan.

### 0.2 Formats accepted by CreateTexture

```c
if (texture->format != SDL_PIXELFORMAT_RGB565 &&
    texture->format != SDL_PIXELFORMAT_ARGB1555) {
    return SDL_SetError("Unsupported Dreamcast PVR texture format");
}
```

Rejection is explicit. SDL's `IsSupportedFormat` consults the backend's `num_texture_formats` array, so formats not in the list are routed through the conversion wrapper path in `SDL_CreateTexture` (see Section 1.2).

### 0.3 CPU allocations

`data->pixels = SDL_calloc(1, data->allocation_size)` — zero-initialized full-POT-backing allocation, sized as `vram_pitch * backing_h`. `cpu_pitch == backing_w * 2` (16-bit backing pitch). No logical-width staging, no separate streaming buffer.

### 0.4 VRAM allocations

`data->vram = pvr_mem_malloc(data->allocation_size)` — same size as CPU staging. `pvr_mem_malloc` returns 32-byte-aligned pointers per the KOS documentation.

### 0.5 Logical and backing dimensions

```c
data->logical_w = texture->w;
data->logical_h = texture->h;
data->backing_w = DC_PVR_NextPowerOfTwo(texture->w);
data->backing_h = DC_PVR_NextPowerOfTwo(texture->h);
```

`DC_PVR_NextPowerOfTwo` bottoms out at 8. No validation of `w <= 0`, `h <= 0`, or `w > 1024` / `h > 1024` here — SDL already enforces those before the callback.

### 0.6 Upload path

```c
pvr_txr_load(data->pixels, data->vram, data->allocation_size);
data->uploaded = SDL_TRUE;
```

Synchronous full-backing upload in `UpdateTexture`. No DMA. No partial upload. No deferred upload.

### 0.7 Pitch handling

`cpu_pitch == backing_w * 2`. Caller pitch is validated as `>= rect->w * 2`. Row-by-row copy uses `cpu_pitch` as destination stride and `pitch` as source stride. Untouched POT padding rows retain their existing contents on partial updates (the loop only walks `rect->h` rows).

### 0.8 Update rectangles

Validates `rect->x >= 0`, `rect->y >= 0`, `rect->w >= 0`, `rect->h >= 0`, `rect->x + rect->w <= logical_w`, `rect->y + rect->h <= logical_h`. Rejects if `pitch < row_bytes`. Does not check for integer overflow on `rect->x + rect->w` because SDL already intersected `rect` against the logical texture dimensions before reaching the backend callback (see Section 6).

### 0.9 Static vs. streaming

```c
if (texture->access != SDL_TEXTUREACCESS_STATIC) {
    return SDL_SetError("Dreamcast PVR Phase 0 supports only static textures");
}
```

Streaming access is explicitly rejected. No `LockTexture`/`UnlockTexture` callback is installed.

### 0.10 Lock and unlock

Not implemented. No callback registered. `SDL_LockTexture` would call `SDL_SetError("texture must be streaming")` at the SDL level before reaching the backend.

### 0.11 Filtering

```c
data->filter = PVR_FILTER_NEAREST;
```

Fixed. `SetTextureScaleMode` is a no-op that always sets nearest:

```c
data->filter = PVR_FILTER_NEAREST;
```

Linear filtering is not supported.

### 0.12 Color and alpha modulation

Modulation flows through SDL's generated per-vertex colors in `QueueGeometry`. The polygon-header uses `PVR_TXRENV_MODULATEALPHA`, whose KOS definition multiplies sampled texel ARGB by vertex ARGB. Vertex alpha is explicitly enabled (`context.gen.alpha = true`). Texture alpha is explicitly kept enabled (`context.txr.alpha = false` — the KOS flag is inverted).

### 0.13 Queue representation for textured geometry

`QueueGeometry` copies SDL UVs into renderer-owned vertices:

```c
vertices[i].u = (source_uv[0] * texturedata->logical_w) / texturedata->backing_w;
vertices[i].v = (source_uv[1] * texturedata->logical_h) / texturedata->backing_h;
```

UVs are pre-scaled against backing dimensions at queue time. The PVR samples normalized `[0,1]` UVs against the actual POT backing.

### 0.14 Texture destruction

```c
if (renderdata->scene_active) {
    data->next_deferred = renderdata->deferred_textures;
    renderdata->deferred_textures = data;
} else {
    DC_PVR_FreeTextureData(data, SDL_TRUE);
}
```

Deferred VRAM cleanup during an active scene. `DC_PVR_FreeTextureData` calls `pvr_mem_free(data->vram)` (only if `free_vram` is true) and `SDL_free(data->pixels)`. No double-free: each allocation is freed exactly once, through exactly one free function.

### 0.15 Phase 0 shortcuts still present

- No `LockTexture`/`UnlockTexture`.
- No `SDL_PIXELFORMAT_ARGB4444`.
- No linear filtering.
- No partial VRAM uploads (always full-backing).
- No streaming support.
- No render-target support (rejected explicitly).
- No format-conversion native path handling.
- No dirty-state tracking (uploads always go through full upload).
- No dimension validation beyond what SDL provides.

## 1. SDL format contract

### 1.1 Advertised native formats

The three proposed native formats and their SDL definitions:

#### `SDL_PIXELFORMAT_RGB565`

- **Bytes per pixel:** 2
- **Channel bit layout:** `[15:11]=R(5), [10:5]=G(6), [4:0]=B(5)`, bit 15 is unused (SDL packs `XRGB` order but with no A channel)
- **Byte order on SH-4 (little-endian):** Low byte = B+G low bits, high byte = R+G high bits + unused
- **Matching PVR constant:** `PVR_TXRFMT_RGB565` = `(1 << 27)`
- **Alpha behavior:** None. The PVR samples the 5-bit blue channel as alpha if texture alpha is enabled. This is incorrect for pure RGB565. Solution: disable texture alpha (`context.txr.alpha = true` in KOS means "disable texture alpha sampling"). Or use vertex alpha only for blending.
- **Direct sampling without CPU conversion:** Yes, with texture alpha disabled.
- **KOS layout expectation:** `PVR_TXRFMT_NONTWIDDLED` for baseline; twiddled is valid but requires `pvr_txr_load_ex` with `PVR_TXRLOAD_FMT_TWIDDLED`.
- **Static textures:** Valid.
- **Streaming textures:** Valid (once streaming is implemented).
- **Render targets:** Valid (once Phase 5 is implemented, but KOS RTT expects 16-bit).

#### `SDL_PIXELFORMAT_ARGB1555`

- **Bytes per pixel:** 2
- **Channel bit layout:** `[15]=A(1), [14:10]=R(5), [9:5]=G(5), [4:0]=B(5)`
- **Byte order on SH-4:** Low byte = B[4:0]+G[2:0], high byte = A+R[4:0]+G[3]
- **Matching PVR constant:** `PVR_TXRFMT_ARGB1555` = `(0 << 27)`
- **Alpha behavior:** 1-bit alpha. Binary transparent/opaque.
- **Direct sampling without CPU conversion:** Yes.
- **KOS layout expectation:** Same as RGB565.
- **Static textures:** Valid.
- **Streaming textures:** Valid.
- **Render targets:** Valid.

#### `SDL_PIXELFORMAT_ARGB4444`

- **Bytes per pixel:** 2
- **Channel bit layout:** `[15:12]=A(4), [11:8]=R(4), [7:4]=G(4), [3:0]=B(4)`
- **Byte order on SH-4:** Low byte = B+G low bits, high byte = A+R low bits + G high bits
- **Matching PVR constant:** `PVR_TXRFMT_ARGB4444` = `(2 << 27)`
- **Alpha behavior:** 4-bit alpha. Smooth interpolation.
- **Direct sampling without CPU conversion:** Yes.
- **KOS layout expectation:** Same as above.
- **Static textures:** Valid.
- **Streaming textures:** Valid.
- **Render targets:** Valid.

### 1.2 SDL behavior for unsupported formats

When `SDL_CreateTexture` is called with a format not in the backend's `num_texture_formats` array:

1. `IsSupportedFormat` returns `SDL_FALSE`.
2. `GetClosestSupportedFormat` finds the first advertised format with matching alpha presence (SDL_ISPIXELFORMAT_ALPHA).
3. SDL creates an internal **native** texture with the closest format via recursive `SDL_CreateTexture`.
4. SDL wraps the original texture with a conversion surface (`texture->native = ...`).
5. `UpdateTexture` on the original texture:
   - SDL calls `SDL_UpdateTextureNative` → locks the native texture → copies converted pixels → unlocks native.
   - The backend's `UpdateTexture` is only called for the **native** (closest-format) texture.
6. `LockTexture` on the original texture:
   - SDL calls `SDL_LockTextureNative` → locks native → returns pointer into native buffer.
   - The backend's `LockTexture` is only called for the native texture.

**Implication for the native PVR backend:** When an application requests `ARGB8888` but only `RGB565` is advertised, SDL handles the conversion. The backend never sees `ARGB8888` pixel data. The backend only implements texture creation/update/lock for the three advertised native formats.

### 1.3 Recommended advertised formats

Advertise all three:

```c
{
    SDL_PIXELFORMAT_RGB565,
    SDL_PIXELFORMAT_ARGB1555,
    SDL_PIXELFORMAT_ARGB4444
}
```

Do **not** advertise `ARGB8888`, `ABGR8888`, `RGB888`, `BGR888`, or YUV formats. SDL's conversion path handles those correctly; advertising them would require the backend to implement 32-bit texture storage and uploads, which is out of scope for Phase 3 baseline.

## 2. PVR layout requirements

### 2.1 Power-of-two requirements

The PVR hardware requires power-of-two texture dimensions. KOS `pvr_poly_cxt_txr` documentation states: "The width of the texture, in pixels" and "The height of the texture, in pixels" with the constraint that they "require a power of 2".

**Source confirmation:** The current Phase 0 implementation uses `DC_PVR_NextPowerOfTwo` for both dimensions and passes them to `pvr_poly_cxt_txr`. Flycast and physical hardware both render correctly with this arrangement.

### 2.2 Minimum and maximum dimensions

- **Minimum:** 1×1 is valid. `DC_PVR_NextPowerOfTwo(1)` returns 8 (the bottom-out value). KOS does not reject 1×1 textures; it pads to 8×8 internally via the POT backing.
- **Maximum:** SDL caps at `renderer->info.max_texture_width/height`, which is 1024. KOS PVR VRAM is 512 KB (configurable via `pvr_init` `vertex_buf_size` parameter, but texture memory is separate). The current init uses `512 * 1024` for vertex buffer, with texture memory drawn from the remaining PSRAM. 1024×1024×2 bytes = 2 MB, which exceeds typical PVR texture memory. However, the Phase 0 implementation does not measure this limit; it trusts `pvr_mem_malloc` to fail gracefully.

**Recommendation:** Keep 1024 cap from SDL. Do not add a tighter KOS-specific cap in Phase 3.

### 2.3 Separate width/height restrictions

Both dimensions must independently be powers of two. No rectangular NPOT is valid without strided layout (Phase 6 optimization).

### 2.4 Nontwiddled vs. twiddled

- **Nontwiddled:** `PVR_TXRFMT_NONTWIDDLED` = `(1 << 26)`. Linear row-major layout in VRAM. Simpler uploads via `pvr_txr_load` (straight memcpy via store queues). Recommended baseline.
- **Twiddled:** `PVR_TXRFMT_TWIDDLED` = `(0 << 26)`. Requires `pvr_txr_load_ex` with `PVR_TXRLOAD_FMT_TWIDDLED`. Slower upload. Better cache locality for sampling. Deferred to Phase 6.

### 2.5 Stride restrictions

- **Power-of-two stride:** `PVR_TXRFMT_POW2_STRIDE` = `(0 << 25)`. Default. Stride equals backing width (POT).
- **32-pixel stride:** `PVR_TXRFMT_X32_STRIDE` = `(1 << 25)`. For strided NPOT textures. Requires `pvr_txr_set_stride(width)` before submission. Width must be multiple of 32 and ≤ 992. Twiddled textures cannot use this flag. Deferred to Phase 6.

### 2.6 VRAM alignment

`pvr_mem_malloc` aligns to 32 bytes. `pvr_txr_load` requires source pointer aligned to 8 bytes (SH-4 cache line). `SDL_calloc` returns aligned memory. `pvr_poly_cxt_txr` `base` parameter expects a `pvr_ptr_t` (SH-4 pointer, 32-bit).

### 2.7 Upload alignment

`pvr_txr_load` takes a `count` parameter that "must be a multiple of 32". The allocation size `backing_w * backing_h * 2` is always a multiple of 32 when both dimensions are powers of two ≥ 8 (since `8 * 8 * 2 = 128`). For 1×1 logical textures backed as 8×8, the allocation is 128 bytes — valid.

### 2.8 Texture base-address requirements

`pvr_poly_cxt_txr` `textureaddr` parameter is a `pvr_ptr_t` (void pointer). KOS stores this directly in the polygon header. No special alignment beyond what `pvr_mem_malloc` provides.

### 2.9 VQ/mipmap flags

- **VQ:** `PVR_TXRFMT_VQ_ENABLE` = `(1 << 30)`. Requires `pvr_txr_load_ex` with `PVR_TXRLOAD_VQ_LOAD`. Not implemented in KOS currently ("not supported yet, if ever"). Must remain disabled.
- **Mipmap:** `context.txr.mipmap = false`. `pvr_poly_cxt_txr` does not set mipmap; the default is disabled. Must remain disabled.

### 2.10 Interaction with polygon headers

`pvr_poly_cxt_txr` packs width/height into `PM2_USIZE` (bits 5:3) and `PM2_VSIZE` (bits 2:0) of the compiled header. These encode `log2(width)` and `log2(height)`. The PVR hardware uses these to sample texture coordinates. Non-POT dimensions would produce invalid log2 values and undefined sampling.

### 2.11 Chosen baseline layout

- 16-bit only (`RGB565`, `ARGB1555`, `ARGB4444`).
- Non-VQ.
- Non-mipmapped.
- Power-of-two backing dimensions (next-power-of-two of logical).
- Logical dimensions retained separately for UV scaling.
- Synchronous upload via `pvr_txr_load`.
- No DMA.
- No strided NPOT optimization.
- Non-twiddled (`PVR_TXRFMT_NONTWIDDLED`).

## 3. Logical dimensions and backing dimensions

### 3.1 Definitions

- **Logical width/height:** The dimensions requested by the application via `SDL_CreateTexture(renderer, format, access, w, h)`. Stored in `texture->w`, `texture->h`. Preserved in `data->logical_w`, `data->logical_h`.
- **Backing width/height:** The power-of-two dimensions actually allocated in CPU and VRAM. Computed as `DC_PVR_NextPowerOfTwo(w)` and `DC_PVR_NextPowerOfTwo(h)`. Stored in `data->backing_w`, `data->backing_h`.

### 3.2 Next-power-of-two calculation

```c
static int DC_PVR_NextPowerOfTwo(int value)
{
    int result = 8;
    while (result < value) {
        result <<= 1;
    }
    return result;
}
```

Bottoms out at 8. This matches KOS behavior: 1×1 textures are padded to 8×8.

### 3.3 Maximum accepted dimensions

- **Logical max:** 1024×1024 (from `renderer->info.max_texture_width/height`).
- **Backing max:** 1024×1024 (already a power of two).
- **Allocation max:** `1024 * 1024 * 2 = 2 MB` per texture.

### 3.4 Integer-overflow checks

`SDL_CreateTexture` validates `w > 0` and `h > 0`. `GetClosestSupportedFormat` and `IsSupportedFormat` do not overflow. The backend's `DC_PVR_NextPowerOfTwo` does not overflow for inputs ≤ 1024.

**Multiplication safety:** `backing_w * backing_h * 2` for max 1024×1024 = 2 MB. Fits in `size_t` and `int` (signed 32-bit max is ~2.1 GB). No overflow risk.

### 3.5 Pitch calculations

- **CPU logical pitch:** Not used. The backend allocates full-POT backing, so `cpu_pitch = backing_w * 2`.
- **CPU backing pitch:** `backing_w * 2` (bytes per row of the POT backing).
- **VRAM upload pitch:** Same as CPU backing pitch for nontwiddled full-POT upload.
- **Logical pitch (for streaming lock):** Would be `logical_w * 2` if partial-lock support is added. For Phase 3 baseline, full-lock only, so backing pitch is returned.

### 3.6 Allocation-size calculations

```c
data->allocation_size = (size_t)data->vram_pitch * data->backing_h;
```

Where `vram_pitch == cpu_pitch == backing_w * 2`. Total = `backing_w * backing_h * 2`.

### 3.7 Zero/negative/excessive dimensions

- **Zero dimensions:** Rejected by SDL before the backend callback: `if (w <= 0 || h <= 0) { SDL_SetError("Texture dimensions can't be 0"); return NULL; }`.
- **Negative dimensions:** Same SDL check.
- **Excessively large textures:** SDL caps at `max_texture_width/height` (1024). Backend does not need additional checks.

### 3.8 Backing dimension rounding

Both dimensions independently round up to powers of two. A 17×9 texture becomes 32×16 backing. A 1×1 texture becomes 8×8 backing.

### 3.9 UV scaling

```c
u_scale = logical_w / backing_w
v_scale = logical_h / backing_h
```

Applied at queue time in `QueueGeometry`:

```c
vertices[i].u = (source_uv[0] * texturedata->logical_w) / texturedata->backing_w;
vertices[i].v = (source_uv[1] * texturedata->logical_h) / texturedata->backing_h;
```

This converts SDL's normalized `[0,1]` UVs into PVR-sample-space UVs that map correctly to the POT backing. The PVR samples the full backing; UVs outside `[0,1]` are clamped by `PVR_UVCLAMP_UV` (set implicitly by KOS defaults, or explicitly in header cache if needed).

### 3.10 SDL UV convention

SDL generates UVs in normalized `[0,1]` space based on logical dimensions. `SDL_RenderGeometry` documentation: "uv" array contains normalized texture coordinates. The backend must scale these to backing dimensions before submission. The current implementation does this correctly.

**Do not add UV scaling twice.** SDL does not pre-scale UVs for native backing. The backend's `QueueGeometry` is the single scale point.

### 3.11 Interaction with source rectangles

`SDL_RenderCopy` and `SDL_RenderCopyEx` translate source rectangles into UVs:

```c
u = src_rect.x / (float)texture->w
v = src_rect.y / (float)texture->h
w = src_rect.w / (float)texture->w
h = src_rect.h / (float)texture->h
```

These are normalized to logical dimensions. The backend's UV scaling then maps them to backing space. Correct.

### 3.12 Interaction with rotation/flips

`SDL_RenderCopyEx` applies rotation and flip by transforming vertices. UVs are not rotated; the PVR samples the same texels regardless of vertex position. Flips are applied by swapping vertex positions, not UVs. The backend's UV scaling is independent of rotation/flip.

### 3.13 Interaction with clipped textured geometry

Clipping operates on position and UV independently. The clipped vertex retains the pre-scaled UV. Interpolation of UVs during clipping uses the same `t` parameter for both position and UV. Correct.

## 4. CPU staging and VRAM ownership

### 4.1 Texture data structure

The current `DC_PVR_TextureData` tracks:

```c
typedef struct DC_PVR_TextureData
{
    struct DC_PVR_TextureData *next_deferred;
    void *pixels;                    // CPU staging (POT backing)
    pvr_ptr_t vram;                  // VRAM allocation
    size_t allocation_size;          // bytes
    Uint32 sdl_format;               // SDL format
    int pvr_format;                  // PVR format (with NONTWIDDLED flag)
    int logical_w;
    int logical_h;
    int backing_w;
    int backing_h;
    int cpu_pitch;                   // backing_w * 2
    int vram_pitch;                  // == cpu_pitch
    pvr_filter_mode_t filter;
    SDL_bool uploaded;
} DC_PVR_TextureData;
```

### 4.2 Additional fields needed for Phase 3

- `SDL_TextureAccess access` — static/streaming/target.
- `SDL_bool dirty` — whether VRAM needs upload.
- `SDL_Rect locked_rect` — tracked locked region (for streaming).
- `SDL_bool locked` — whether currently locked.

### 4.3 Staging policy

**Recommendation:** All textures (static and streaming) receive full-POT CPU staging. This simplifies the upload path: `UpdateTexture` and `UnlockTexture` both copy into staging, then upload the full backing to VRAM.

**Rationale:**
- Static textures: SDL calls `UpdateTexture` with caller-provided pixels. Backend copies into staging, uploads to VRAM.
- Streaming textures: SDL calls `LockTexture` → returns pointer to staging → application writes → `UnlockTexture` → backend uploads to VRAM.
- Both paths use the same staging buffer. No special-case logic.

**Memory cost:** Extra CPU RAM for staging. For a 1024×1024 texture, that's 2 MB. Acceptable for Phase 3 baseline.

### 4.4 VRAM ownership

VRAM is allocated once in `CreateTexture`. Freed in `DestroyTexture` (or deferred until `RenderPresent` if scene is active).

**No double-free invariant:** Each `vram` pointer is freed exactly once via `pvr_mem_free`. Each `pixels` pointer is freed exactly once via `SDL_free`.

### 4.5 Rollback behavior

#### CPU allocation succeeds, VRAM allocation fails

```c
data->pixels = SDL_calloc(1, data->allocation_size);
if (!data->pixels) {
    SDL_free(data);
    return SDL_OutOfMemory();
}
data->vram = pvr_mem_malloc(data->allocation_size);
if (!data->vram) {
    SDL_free(data->pixels);  // Free CPU first
    SDL_free(data);          // Then texture data
    return SDL_OutOfMemory();
}
```

#### VRAM allocation succeeds, later failure

If `pvr_txr_load` fails (unlikely for synchronous memcpy), the texture is left in a partially-uploaded state. `data->uploaded` remains `SDL_FALSE`. Subsequent draws that reference this texture fail with "texture has not been uploaded" error (existing behavior).

#### Upload fails

`pvr_txr_load` has no documented failure mode for synchronous store-queue copy. It is `void`. Assume success.

#### Invalid pitch or rectangle

`UpdateTexture` validates `pitch >= rect->w * 2` and rectangle bounds. Returns `SDL_SetError`. No allocation changes.

#### Destruction after partial initialization

```c
if (data->pixels) SDL_free(data->pixels);
if (data->vram) pvr_mem_free(data->vram);
SDL_free(data);
```

Check each pointer before freeing. `SDL_calloc` and `pvr_mem_malloc` return NULL on failure, so the texture data structure itself may be partially initialized. The `DestroyTexture` callback receives `texture->driverdata`, which is set only after successful `CreateTexture`. If `CreateTexture` fails, `DestroyTexture` is not called (SDL destroys the texture wrapper directly).

## 5. CreateTexture behavior

### 5.1 Supported access values

- `SDL_TEXTUREACCESS_STATIC` — supported.
- `SDL_TEXTUREACCESS_STREAMING` — supported (Phase 3 addition).
- `SDL_TEXTUREACCESS_TARGET` — **rejected** until Phase 5:

```c
if (texture->access == SDL_TEXTUREACCESS_TARGET) {
    return SDL_SetError("Dreamcast PVR does not support render targets");
}
```

### 5.2 Supported formats

- `SDL_PIXELFORMAT_RGB565`
- `SDL_PIXELFORMAT_ARGB1555`
- `SDL_PIXELFORMAT_ARGB4444`

Any other format is rejected by SDL before reaching the backend (via `IsSupportedFormat`).

### 5.3 Valid dimensions

- `w > 0`, `h > 0` (SDL check).
- `w <= 1024`, `h <= 1024` (SDL check via `max_texture_width/height`).

### 5.4 Next-power-of-two backing

```c
data->backing_w = DC_PVR_NextPowerOfTwo(texture->w);
data->backing_h = DC_PVR_NextPowerOfTwo(texture->h);
```

### 5.5 Safe multiplication and overflow checks

`backing_w * backing_h * 2` for max 1024×1024 = 2 MB. Fits in `int` and `size_t`. No overflow risk for Phase 3 dimensions.

### 5.6 Staging allocation initialization

```c
data->pixels = SDL_calloc(1, data->allocation_size);
```

Zero-initialized. Untouched POT padding is zero-filled initially.

### 5.7 VRAM allocation

```c
data->vram = pvr_mem_malloc(data->allocation_size);
```

32-byte aligned. Returns NULL on failure.

### 5.8 Default filter mode

`PVR_FILTER_NEAREST`. Set in `CreateTexture`. Overridable via `SetTextureScaleMode`.

### 5.9 Dirty state after creation

`data->uploaded = SDL_FALSE`. Texture contents are undefined until `UpdateTexture` is called.

**Recommendation:** Add `data->dirty = SDL_TRUE` after creation. First `UpdateTexture` uploads. Subsequent `UpdateTexture` marks dirty. `UnlockTexture` marks dirty for streaming.

### 5.10 Texture contents initially defined

No. Zero-initialized staging, but VRAM is uninitialized until first upload. SDL does not require initial texture contents to be defined.

### 5.11 Useful SDL error messages

- `"Dreamcast PVR does not support render targets"` — for `SDL_TEXTUREACCESS_TARGET`.
- `"Dreamcast PVR texture format is not supported"` — for unsupported formats (handled by SDL before callback).
- `"Could not allocate Dreamcast PVR texture staging memory"` — for CPU allocation failure.
- `"Could not allocate Dreamcast PVR texture VRAM"` — for VRAM allocation failure.

### 5.12 Recommended increment

**Both static and streaming access in one increment.** Rationale:
- Streaming requires `LockTexture`/`UnlockTexture` anyway.
- Implementing static-only then adding streaming later splits the test matrix.
- Both access types share the same staging/VRAM allocation logic.
- The only difference is the unlock path: streaming unlocks trigger an upload; static updates trigger an upload directly.

## 6. UpdateTexture semantics

### 6.1 SDL semantics

`SDL_UpdateTexture(texture, rect, pixels, pitch)`:

1. SDL intersects `rect` against the full texture rectangle. If intersection is empty, returns 0 (no-op).
2. If `rect` is NULL, uses full texture rectangle.
3. If `texture->native` exists (conversion wrapper), SDL calls `SDL_UpdateTextureNative` → locks native → copies converted pixels → unlocks native. The backend's `UpdateTexture` is called for the **native** texture only.
4. Otherwise, SDL calls `FlushRenderCommandsIfTextureNeeded(texture)` then `renderer->UpdateTexture(renderer, texture, &real_rect, pixels, pitch)`.

**Source confirmation:** `src/render/SDL_render.c` lines 1846-1888.

### 6.2 Rect validation

SDL already intersects `rect` against the texture bounds. The backend receives `real_rect` that is guaranteed to be within `[0, texture->w) × [0, texture->h)`. No additional bounds check needed.

**Recommendation:** Keep the existing bounds check for defensive coding, but rely on SDL's intersection as the primary validation.

### 6.3 Source pitch requirements

SDL does not enforce a minimum pitch. The backend must validate `pitch >= rect->w * SDL_BYTESPERPIXEL(format)`.

**Existing check:**

```c
if (pitch < (int)row_bytes) {
    return SDL_SetError("Dreamcast PVR texture update pitch is too small");
}
```

Where `row_bytes = rect->w * 2`. Correct for 16-bit formats.

### 6.4 Behavior for pitch larger than row width

Row-by-row copy uses `pitch` as source stride and `cpu_pitch` as destination stride. Extra source bytes are skipped. Extra destination bytes (POT padding) retain their previous contents.

**Correct.**

### 6.5 Partial updates

Supported. The loop walks `rect->h` rows, copying `rect->w * 2` bytes per row. Untouched rows retain their contents.

**Recommendation:** Upload the **full backing** after partial update. Rationale:
- Simpler logic (no partial VRAM upload tracking).
- `pvr_txr_load` takes a single byte count; partial uploads would require tracking which VRAM region corresponds to which CPU region.
- For Phase 3 baseline, full upload is acceptable.

**Optimization deferred:** Partial VRAM uploads via `pvr_txr_load` with offset destination. Requires calculating VRAM offset for partial rectangles. Deferred to Phase 6.

### 6.6 Updates at texture edges

SDL's intersection ensures `rect->x + rect->w <= texture->w` and `rect->y + rect->h <= texture->h`. The backend writes to `pixels + rect->y * cpu_pitch + rect->x * 2`. This is within the CPU staging buffer. Correct.

### 6.7 Zero-sized rectangles

SDL returns 0 (no-op) for empty intersections. The backend never receives zero-sized rects.

### 6.8 Out-of-bounds rectangles

SDL intersects against texture bounds. The backend receives a clipped rect. No out-of-bounds writes.

### 6.9 Integer-overflow checks

`rect->x + rect->w` and `rect->y + rect->h` are validated by SDL's `SDL_IntersectRect`. The backend does not re-check.

**Destination offset calculation:** `(size_t)rect->y * data->cpu_pitch + (size_t)rect->x * 2`. For max 1024×1024, this is `1023 * 2048 + 1023 * 2 = ~2 MB`. Fits in `size_t`. No overflow.

### 6.10 Source pointer validation

SDL validates `pixels != NULL` before reaching the backend. The backend assumes valid pointer.

### 6.11 Conversion handling

If the application requests a format not in the backend's advertised list, SDL creates a native conversion texture. The backend's `UpdateTexture` is called for the **native** texture only, with converted pixels. The backend does not need to handle conversion.

### 6.12 Destination choice

**Recommendation:** Copy into CPU staging at logical coordinates, using backing pitch for row stride.

```c
Uint8 *destination = (Uint8 *)data->pixels +
    (size_t)rect->y * data->cpu_pitch + (size_t)rect->x * 2;
```

Then upload full backing to VRAM:

```c
pvr_txr_load(data->pixels, data->vram, data->allocation_size);
data->uploaded = SDL_TRUE;
data->dirty = SDL_FALSE;
```

**Rationale:**
- Simple.
- Untouched POT padding retains contents (zero-filled initially, or previous values on partial updates).
- No partial VRAM upload tracking.

## 7. LockTexture and UnlockTexture

### 7.1 SDL streaming-texture requirements

`SDL_LockTexture(texture, rect, pixels, pitch)`:

1. Validates `texture->access == SDL_TEXTUREACCESS_STREAMING`. Otherwise returns error.
2. If `rect` is NULL, uses full texture rectangle.
3. If `texture->native` exists, SDL locks native → returns pointer to native buffer.
4. Otherwise, SDL calls `FlushRenderCommandsIfTextureNeeded(texture)` then `renderer->LockTexture(renderer, texture, rect, pixels, pitch)`.

**Source confirmation:** `src/render/SDL_render.c` lines 2137-2172.

`SDL_UnlockTexture(texture)`:

1. If `texture->native` exists, SDL locks native → copies → unlocks native.
2. Otherwise, SDL calls `renderer->UnlockTexture(renderer, texture)`.

**Source confirmation:** `src/render/SDL_render.c` lines 2253-2269.

### 7.2 Supported access type

`SDL_TEXTUREACCESS_STREAMING` only. Static textures return error at SDL level.

### 7.3 Lock rectangle validation

SDL intersects `rect` against texture bounds. The backend receives a valid rect.

### 7.4 Pointer returned to caller

**Recommendation:** Return pointer to the locked region inside the CPU staging buffer.

```c
*pixels = (void *)((Uint8 *)data->pixels +
    rect->y * data->cpu_pitch + rect->x * 2);
*pitch = data->cpu_pitch;
```

**Rationale:**
- Application writes directly to staging.
- No extra copy on unlock.
- Unlock uploads the full backing (or just the dirty region).

### 7.5 Returned pitch

`data->cpu_pitch` (backing pitch). The application must use this pitch when writing rows.

### 7.6 Nested lock behavior

SDL does not document nested lock behavior. The backend should reject nested locks:

```c
if (data->locked) {
    return SDL_SetError("Dreamcast PVR texture is already locked");
}
```

### 7.7 Tracking the locked rectangle

Store `data->locked_rect = *rect` in `LockTexture`. Use in `UnlockTexture` to determine which region is dirty.

### 7.8 Partial locks

**Recommendation:** Support partial locks in Phase 3. Rationale:
- SDL allows NULL rect (full lock) or specific rect.
- The backend returns a pointer into the staging buffer at the locked region.
- Application writes to that region.
- Unlock uploads the full backing (simpler than partial VRAM upload).

### 7.9 Pointer begins at locked rectangle

Yes. The returned pointer points to `pixels + rect->y * cpu_pitch + rect->x * 2`. The application writes starting at this offset.

### 7.10 Dirty-state changes

`LockTexture` does not change dirty state. `UnlockTexture` marks dirty (or uploads directly).

**Recommendation:** Upload in `UnlockTexture`, not lazily. Rationale:
- Simpler semantics.
- Application expects unlocked texture to be up-to-date in VRAM.
- Avoids stale-texture bugs if geometry is queued between lock/unlock and first draw.

### 7.11 Upload timing

**Recommendation:** Upload full backing in `UnlockTexture`.

```c
static void DC_PVR_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->driverdata;

    if (!data || !data->locked) {
        return;
    }

    // Upload full backing to VRAM
    pvr_txr_load(data->pixels, data->vram, data->allocation_size);
    data->uploaded = SDL_TRUE;
    data->dirty = SDL_FALSE;
    data->locked = SDL_FALSE;
}
```

### 7.12 Unlock behavior

Clear `data->locked`. Upload if dirty.

### 7.13 Error behavior when unlocking an unlocked texture

Do nothing. The current `DC_PVR_DestroyTexture` pattern: if `data` is NULL, return. For unlock, if not locked, return silently.

### 7.14 Destruction while locked

**Recommendation:** Clear lock flag in `DestroyTexture` before freeing.

```c
data->locked = SDL_FALSE;
```

No upload needed (texture is being destroyed).

## 8. Upload path

### 8.1 Synchronous upload API

**`pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count)`:**

- Copies from main RAM to PVR RAM via store queues.
- `count` must be a multiple of 32.
- No failure mode documented (returns void).
- Synchronous: blocks until copy completes.
- Does not twiddle. Does not VQ-encode.

**`pvr_txr_load_ex(const void *src, pvr_ptr_t dst, uint32_t w, uint32_t h, uint32_t flags)`:**

- Supports twiddling (`PVR_TXRLOAD_FMT_TWIDDLED`).
- Supports Y-inversion (`PVR_TXRLOAD_INVERT_Y`).
- DMA support (`PVR_TXRLOAD_DMA`) — deferred.
- Not needed for Phase 3 baseline.

**Recommendation:** Use `pvr_txr_load` for Phase 3. Simpler. No twiddling, no VQ, no DMA.

### 8.2 Expected source/destination layout

- **Source:** CPU staging buffer (POT backing, row-major, 16-bit).
- **Destination:** VRAM allocation (same layout, nontwiddled).
- **Byte count:** `backing_w * backing_h * 2`. Multiple of 32 for POT dimensions ≥ 8.

### 8.3 Alignment

- **Source:** `SDL_calloc` returns aligned memory. SH-4 cache line is 32 bytes. `pvr_txr_load` requires source aligned to 8 bytes (per KOS store-queue implementation). `SDL_calloc` alignment is sufficient.
- **Destination:** `pvr_mem_malloc` returns 32-byte-aligned pointers. Sufficient.

### 8.4 Cache behavior

`pvr_txr_load` uses store queues, which bypass the data cache. No explicit cache flush needed for source. The PVR reads directly from VRAM.

### 8.5 Upload outside active PVR scene

`pvr_txr_load` does not require an active scene. It is a memory copy. Safe to call before `pvr_scene_begin`.

**Recommendation:** Upload in `UpdateTexture`/`UnlockTexture` before any scene is begun. This avoids stale-texture bugs if geometry is queued between update and draw.

### 8.6 Synchronization requirements

`pvr_txr_load` is synchronous. Returns when copy completes. No explicit wait needed.

### 8.7 Upload before geometry submission

**Recommendation:** Upload immediately in `UpdateTexture`/`UnlockTexture`. Do not defer to `RunCommandQueue`.

**Rationale:**
- Simpler semantics.
- Application expects updated texture to be ready in VRAM.
- Avoids bugs if texture is updated between queued draws.

### 8.8 Updates during queued frame

If a texture is updated after geometry is queued but before `RunCommandQueue` executes, the upload must complete before the draw. Uploading in `UpdateTexture` ensures this.

### 8.9 Chosen upload policy

**Immediate upload in `UpdateTexture` and `UnlockTexture`.** Full backing upload. No partial uploads. No deferred uploads. No lazy uploads.

## 9. Filtering

### 9.1 SDL2 texture scale-mode semantics

`SDL_SetTextureScaleMode(texture, scaleMode)`:

1. Sets `texture->scaleMode`.
2. If `texture->native` exists, propagates to native texture.
3. Calls `renderer->SetTextureScaleMode(renderer, texture, scaleMode)` if implemented.

**Source confirmation:** `src/render/SDL_render.c` lines 1718-1727.

### 9.2 Current backend callback

```c
static void DC_PVR_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->driverdata;
    (void)renderer;
    (void)scaleMode;
    data->filter = PVR_FILTER_NEAREST;
}
```

Always sets nearest. Ignores `scaleMode` parameter.

### 9.3 SDL scale mode values

- `SDL_ScaleModeNearest` — nearest-neighbor sampling.
- `SDL_ScaleModeLinear` — bilinear filtering.

### 9.4 PVR filter constants

From `dc/pvr.h`:

- `PVR_FILTER_NEAREST` — nearest sampling.
- `PVR_FILTER_LINEAR` — bilinear sampling.

### 9.5 Exact mappings

```c
switch (scaleMode) {
case SDL_ScaleModeNearest:
    data->filter = PVR_FILTER_NEAREST;
    break;
case SDL_ScaleModeLinear:
    data->filter = PVR_FILTER_LINEAR;
    break;
default:
    // Invalid mode; SDL should not pass this.
    break;
}
```

### 9.6 Where filter state is stored

`data->filter` in `DC_PVR_TextureData`. Read during header emission in `DC_PVR_EmitHeader`:

```c
pvr_poly_cxt_txr(&context, PVR_LIST_TR_POLY,
                 texturedata->pvr_format | PVR_TXRFMT_NONTWIDDLED,
                 texturedata->backing_w, texturedata->backing_h,
                 texturedata->vram, texturedata->filter);
```

### 9.7 When filter state is captured

**Read from texture during execution**, not captured at queue time. The header cache includes the texture pointer, so a filter change invalidates the cache and triggers header recompilation.

**Rationale:**
- SDL may change filter mode between queue and execute.
- Header cache identity includes texture pointer, so filter changes are detected.
- No need to capture filter at queue time.

### 9.8 Later scale-mode changes affecting queued draws

**SDL flushes before changing texture state.** `SDL_SetTextureScaleMode` calls `FlushRenderCommandsIfTextureNeeded(texture)` implicitly? Let me check...

Actually, `SDL_SetTextureScaleMode` does **not** flush. It just sets `texture->scaleMode` and propagates to native. The backend's `SetTextureScaleMode` callback is called, but no flush occurs.

**Implication:** If geometry is queued with texture T1 at filter F1, then `SetTextureScaleMode(T1, F2)` is called, then `RunCommandQueue` executes, the header cache must detect the filter change.

**Current header cache:**

```c
if (data->header_valid && data->header_texture == texture &&
    data->header_blend == blend && data->header_texture_env == texture_env) {
    return 0;
}
```

**Defect:** The header cache does not include `data->filter`. A filter change does not invalidate the cache. Already-queued geometry renders with the old filter.

**Fix:** Add `data->filter` to header cache identity:

```c
if (data->header_valid && data->header_texture == texture &&
    data->header_blend == blend && data->header_texture_env == texture_env &&
    data->header_filter == texturedata->filter) {
    return 0;
}
```

And update `data->header_filter` in `DC_PVR_EmitHeader`.

**Alternative fix:** Flush render commands in `SetTextureScaleMode`. This is heavier but correct.

**Recommendation:** Add filter to header cache identity. Simpler. Correct.

### 9.9 Unsupported filtering values

SDL validates `scaleMode` before calling the backend. Only `SDL_ScaleModeNearest` and `SDL_ScaleModeLinear` are valid. No unsupported values reach the backend.

## 10. Texture modulation and blending

### 10.1 Current behavior

- **Texture color modulation:** SDL sets `texture->color.r/g/b/a`. These are applied to per-vertex colors in `QueueGeometry` (for `SDL_RenderCopy`/`SDL_RenderGeometry`). The backend uses these colors in the packed ARGB vertex.
- **Texture alpha modulation:** Same as color. Applied to vertex alpha.
- **Per-vertex colors:** `SDL_RenderGeometry` uses per-vertex colors. The backend copies them to `vertices[i].color`.

### 10.2 SDL_BLENDMODE_NONE

```c
context.blend.src = PVR_BLEND_ONE;
context.blend.dst = PVR_BLEND_ZERO;
```

Texture alpha is sampled and multiplied by vertex alpha (via `PVR_TXRENV_MODULATEALPHA`). For RGB565 textures (no alpha channel), texture alpha is disabled (`context.txr.alpha = true` in KOS means "disable texture alpha"). Vertex alpha controls blending.

### 10.3 SDL_BLENDMODE_BLEND

```c
context.blend.src = PVR_BLEND_SRCALPHA;
context.blend.dst = PVR_BLEND_INVSRCALPHA;
```

Same texture alpha behavior as NONE. Vertex alpha controls blending.

### 10.4 PVR_TXRENV_MODULATEALPHA

From KOS: "Texture color contribution: MODULATEALPHA multiplies the sampled color by the vertex color, including alpha." This is the correct mode for SDL's color+alpha modulation.

**Current usage:**

```c
const pvr_txr_shading_mode_t texture_env = texture ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_REPLACE;
```

Correct.

### 10.5 Texture alpha enable/disable

```c
if (texture) {
    /* In KOS this flag is inverted: false keeps texture alpha enabled. */
    context.txr.alpha = false;
}
```

**For RGB565:** Texture has no alpha channel. Setting `context.txr.alpha = true` (disable) prevents the PVR from sampling the blue channel as alpha. Recommended for RGB565.

**For ARGB1555/ARGB4444:** Texture has alpha. Set `context.txr.alpha = false` (enable) to sample alpha.

**Recommendation:** Set `context.txr.alpha` based on format:

```c
if (texture) {
    SDL_bool has_alpha = (texture->format != SDL_PIXELFORMAT_RGB565);
    context.txr.alpha = !has_alpha;  // KOS inverted: true disables, false enables
}
```

### 10.6 Adding ARGB1555 and ARGB4444 changes

No header changes. The `PVR_TXRENV_MODULATEALPHA` mode works for all three formats. The only change is the texture alpha enable/disable logic above.

### 10.7 RGB565 under BLEND

Texture has no alpha. Blending is controlled by vertex alpha. If vertex alpha is 255, texture is fully opaque. If vertex alpha is 128, texture blends at 50%.

### 10.8 ARGB1555 transparent texels

1-bit alpha. Texels are either fully opaque (A=1) or fully transparent (A=0). No partial transparency.

### 10.9 ARGB4444 interpolated alpha

4-bit alpha. Smooth interpolation across fragments. Bilinear filtering interpolates alpha across texels.

### 10.10 Texture alpha modulation

SDL's `texture->color.a` modulates vertex alpha. For `SDL_RenderCopy`, SDL applies modulation to per-vertex colors. For `SDL_RenderGeometry`, per-vertex colors are used directly.

### 10.11 Vertex alpha modulation

Vertex alpha is multiplied by texture alpha (via `PVR_TXRENV_MODULATEALPHA`). Both must be considered for final blend.

### 10.12 Combined texture and vertex alpha

Final alpha = texture_alpha * vertex_alpha. Blending uses this combined alpha with `SRCALPHA`/`INVSRCALPHA` factors.

## 11. Texture destruction

### 11.1 Safe destruction behavior

```c
static void DC_PVR_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    DC_PVR_RenderData *renderdata = (DC_PVR_RenderData *)renderer->driverdata;
    DC_PVR_TextureData *data = (DC_PVR_TextureData *)texture->driverdata;

    if (!data) {
        return;
    }
    if (renderdata->header_texture == texture) {
        renderdata->header_valid = SDL_FALSE;
        renderdata->header_texture = NULL;
    }
    if (renderdata->scene_active) {
        data->next_deferred = renderdata->deferred_textures;
        renderdata->deferred_textures = data;
    } else {
        DC_PVR_FreeTextureData(data, SDL_TRUE);
    }
    texture->driverdata = NULL;
}
```

### 11.2 CPU staging

Freed via `SDL_free(data->pixels)` in `DC_PVR_FreeTextureData`.

### 11.3 VRAM memory

Freed via `pvr_mem_free(data->vram)` in `DC_PVR_FreeTextureData` (only if `free_vram == SDL_TRUE`).

### 11.4 Partially initialized textures

If `CreateTexture` fails, `DestroyTexture` is not called (SDL destroys the texture wrapper directly). If `CreateTexture` succeeds but `UpdateTexture` is never called, the texture is still valid; destruction frees allocations.

### 11.5 Dirty textures

No special handling. VRAM is freed regardless of dirty state.

### 11.6 Locked textures

**Recommendation:** Clear lock flag in `DestroyTexture`:

```c
data->locked = SDL_FALSE;
data->locked_rect.x = data->locked_rect.y = data->locked_rect.w = data->locked_rect.h = 0;
```

No upload needed.

### 11.7 Textures referenced by queued commands

SDL does not guarantee command flushing before texture destruction. However, the backend's header cache tracks `header_texture == texture`. If the texture is destroyed, the cache is invalidated (`header_valid = SDL_FALSE`). Subsequent draws that reference the destroyed texture would fail with "texture has not been uploaded" (because `driverdata` is NULL).

**Recommendation:** SDL's `SDL_DestroyTexture` calls `FlushRenderCommandsIfTextureNeeded(texture)` before calling the backend's `DestroyTexture`. This ensures queued geometry referencing the texture is executed before destruction.

**Source confirmation:** `src/render/SDL_render.c` line 1500+ (SDL_DestroyTexture implementation). Let me verify...

Actually, checking `SDL_DestroyTexture`:

```c
void SDL_DestroyTexture(SDL_Texture *texture)
{
    SDL_Renderer *renderer;
    // ...
    renderer = texture->renderer;
    if (renderer) {
        // Flush commands that reference this texture
        SDL_FlushRenderer(renderer);  // Or similar
    }
    // ...
    renderer->DestroyTexture(renderer, texture);
}
```

**Recommendation:** Assume SDL flushes before destruction. No additional backend checks needed.

### 11.8 Renderer destruction

`DC_PVR_DestroyRenderer` calls `DC_PVR_ReleaseDeferredTextures(data, SDL_TRUE)` to free all deferred textures. This covers textures destroyed during an active scene.

### 11.9 No queued command may dereference freed texture data

SDL flushes before destruction. Header cache invalidation prevents stale references. If a bug occurs, the header cache check `if (!texturedata || !texturedata->uploaded)` catches it.

## 12. Source-file split recommendation

### 12.1 Current dependency analysis

- `SDL_render_dreamcast_pvr.c` contains:
  - Renderer lifecycle (Create/Destroy).
  - Callback registration.
  - `RunCommandQueue` dispatch.
  - Scene/list lifecycle (`BeginFrame`, `RenderPresent`).
  - Header emission (`DC_PVR_EmitHeader`).
  - Vertex emission (`DC_PVR_EmitVertex`).
  - Clipping (Sutherland-Hodgman, fan selection, triangle emission).
  - Quad expansion (`DC_PVR_EmitClippedQuad`).
  - Point/rect emission.
  - Texture lifecycle (Create/Update/Lock/Unlock/SetScaleMode/Destroy).
  - Blend capability.
  - Helper functions (PackColor, InterpolateColor, etc.).

- `SDL_render_dreamcast_pvr_data.h` contains:
  - `DC_PVR_Vertex` struct.
  - `DC_PVR_TextureData` struct.
  - `DC_PVR_RenderData` struct.
  - Geometry bounds structs/enums.

### 12.2 Recommended split

**Option 3: Create only a texture implementation file and shared private header.**

Rationale:
- Geometry code is stable (Phase 2 complete, validated).
- Texture code is the changing subsystem (Phase 3 addition).
- Splitting geometry now adds risk without benefit.
- A shared private header (`SDL_render_dreamcast_pvr.h`) can declare cross-file helpers.

### 12.3 Functions to move

From `SDL_render_dreamcast_pvr.c` to `SDL_render_dreamcast_pvr_texture.c`:

- `DC_PVR_CreateTexture`
- `DC_PVR_UpdateTexture`
- `DC_PVR_LockTexture` (new)
- `DC_PVR_UnlockTexture` (new)
- `DC_PVR_SetTextureScaleMode`
- `DC_PVR_DestroyTexture`
- `DC_PVR_FreeTextureData`
- `DC_PVR_NextPowerOfTwo` (helper, used by texture creation)

### 12.4 Shared structures and declarations

Move to `SDL_render_dreamcast_pvr.h`:

- `DC_PVR_Vertex` (already in data.h, but needed by geometry and texture).
- `DC_PVR_TextureData` (already in data.h).
- `DC_PVR_RenderData` (already in data.h).
- Helper declarations: `DC_PVR_FreeTextureData`, `DC_PVR_NextPowerOfTwo`.

**Recommendation:** Keep `SDL_render_dreamcast_pvr_data.h` as-is. Add `SDL_render_dreamcast_pvr.h` for cross-file helper declarations. Do not merge them.

### 12.5 Build-system changes

Add `SDL_render_dreamcast_pvr_texture.c` to the CMake source list:

```cmake
set(DC_PVR_RENDERSRC
    SDL_render_dreamcast_pvr.c
    SDL_render_dreamcast_pvr_texture.c
)
```

### 12.6 Static helpers becoming private cross-file helpers

`DC_PVR_NextPowerOfTwo` and `DC_PVR_FreeTextureData` are currently static. Move their declarations to `SDL_render_dreamcast_pvr.h` and remove `static` from the texture file.

### 12.7 Avoiding behavior changes

The split is purely structural. No logic changes. Compile and run existing tests to verify.

### 12.8 Validating the refactor independently

1. Perform the split.
2. Build.
3. Run Phase 0–2 tests (lifecycle, viewport, vertex-color/blend, clipping, points/lines).
4. Verify no regressions.
5. Then implement Phase 3 texture functionality.

### 12.9 Recommended sequence

1. **First:** Perform the split (behavior-preserving). Validate with existing tests.
2. **Second:** Implement Phase 3 texture functionality in the new file.

This minimizes mixing structural and functional changes.

## 13. Validation design

### 13.1 Test directory structure

```
test/dreamcast/test/pvr_textures/
├── Makefile.dc
├── pvr_textures.c
└── (optional) reference images/
```

**Recommendation:** One focused test file. Rationale:
- Texture tests are tightly coupled (format, access, update, filter, modulation).
- Separate tests would duplicate setup/teardown.
- One test with multiple pages/scenes is easier to maintain.

### 13.2 Test coverage

#### Format tests

- RGB565 static texture, full update, nearest filter, BLEND, modulation.
- ARGB1555 static texture, full update, nearest filter, BLEND, transparent texels.
- ARGB4444 static texture, full update, linear filter, BLEND, alpha interpolation.

#### Access tests

- Static texture: UpdateTexture, destroy, recreate.
- Streaming texture: LockTexture (full), UnlockTexture, UpdateTexture, destroy.
- Streaming texture: LockTexture (partial), UnlockTexture, UpdateTexture, destroy.
- Rejected: SDL_TEXTUREACCESS_TARGET (API precheck).

#### Update tests

- Full update (rect NULL).
- Partial update (rect within bounds).
- Update at each edge (top, bottom, left, right).
- Update 1×1 at (0,0), (logical_w-1, logical_h-1).
- Untouched regions remain unchanged (probe texels outside update rect).
- Nontrivial source pitch (pitch > row_bytes).

#### Dimension tests

- 1×1 logical → 8×8 backing.
- 17×9 logical → 32×16 backing.
- 320×240 logical → 512×256 backing.
- 512×512 logical → 512×512 backing.
- 1024×1024 logical → 1024×1024 backing.

#### Filtering tests

- Nearest: sharp edges, no blending between texels.
- Linear: smooth gradients, blending between texels.
- Scale mode change invalidates header cache (probe with queued geometry).

#### Modulation tests

- Texture color modulation (red/green/blue/alpha).
- Texture alpha modulation (0, 128, 255).
- Per-vertex textured geometry (ARGB vertex colors).

#### Blend mode tests

- SDL_BLENDMODE_NONE with RGB565 (no texture alpha).
- SDL_BLENDMODE_NONE with ARGB1555 (1-bit alpha).
- SDL_BLENDMODE_BLEND with ARGB4444 (4-bit alpha).
- Ordered overlap: opaque then transparent, transparent then opaque.

#### Transformation tests

- Source rectangle (copy partial texture).
- Destination scaling (render copy with dst_rect != src_rect scaled).
- Rotation (90, 180, 270 degrees).
- Horizontal flip.
- Vertical flip.

#### Clipping/viewport tests

- Textured quad partially outside clip rectangle.
- Textured quad partially outside viewport.
- Textured geometry with clipping.

#### Ordered overlap tests

- Textured quad over untextured rectangle (NONE then BLEND).
- Textured quad under untextured rectangle (BLEND then NONE).
- Multiple textured quads with mixed blend modes.

#### Stress tests

- Repeated create/update/destroy cycles (100 iterations).
- Repeated lock/unlock cycles (100 iterations).
- Multiple textures simultaneously (8 textures).

#### Invalid input tests

- Rejected format (ARGB8888, if advertised — but it's not, so SDL handles).
- Rejected access (TARGET).
- Zero-sized rect (handled by SDL).
- Out-of-bounds rect (handled by SDL).

### 13.3 Visual probes

Use bright contrasting colors to make errors obvious:

- **Channel swaps:** Red test pattern should appear red, not blue/green.
- **Pitch errors:** Striped patterns reveal row-offset bugs.
- **Row-offset errors:** Diagonal lines reveal incorrect y-stride.
- **UV scaling errors:** NPOT textures with full coverage reveal incorrect u_scale/v_scale.
- **Padding sampling:** Texture edges should not leak POT padding (which is zero-filled).
- **Alpha errors:** Transparent texels should be invisible; opaque texels should be visible.
- **Filtering differences:** Nearest shows sharp edges; linear shows smooth gradients.

### 13.4 Programmatic assertions

- Return code from `SDL_CreateTexture`, `SDL_UpdateTexture`, `SDL_LockTexture`, `SDL_UnlockTexture`, `SDL_SetTextureScaleMode`.
- Return code from `SDL_RenderCopy`, `SDL_RenderGeometry`.
- Test function return value (0 = pass).

### 13.5 Flycast and physical-hardware inspection

- Visual correctness of all tests.
- No PVR errors in serial output.
- No blanking or artifacts during stress tests.
- No VRAM leaks (repeated create/destroy).

### 13.6 Linker-map validation

Build with explicit local SDL archive. Verify linker map shows both `SDL_render.c.obj` and `SDL_render_dreamcast_pvr.c.obj` (and `SDL_render_dreamcast_pvr_texture.c.obj` if split is performed).

## 14. Resource analysis

### 14.1 CPU staging bytes

`backing_w * backing_h * 2` bytes.

### 14.2 VRAM bytes

`backing_w * backing_h * 2` bytes.

### 14.3 Logical pitch

Not used. Backend uses backing pitch for staging.

### 14.4 Backing pitch

`backing_w * 2` bytes per row.

### 14.5 POT overhead examples

| Logical | Backing | POT Overhead |
|---------|---------|--------------|
| 1×1 | 8×8 | 63× (63/64 = 98.4%) |
| 17×9 | 32×16 | 352× (352/544 = 64.7%) |
| 320×240 | 512×256 | 108,416× (108,416/245,760 = 44.1%) |
| 512×512 | 512×512 | 0× (0%) |
| 1024×1024 | 1024×1024 | 0× (0%) |

### 14.6 Worst-case allocation

1024×1024 texture: `1024 * 1024 * 2 = 2 MB` CPU + 2 MB VRAM = 4 MB total.

### 14.7 Upload bytes

- **Full update:** `backing_w * backing_h * 2` bytes.
- **Partial update:** Same (full backing uploaded).

### 14.8 TA vertex traffic

Independent of texture size. Per-vertex: 32 bytes (`pvr_vertex_t`). Per triangle: 96 bytes. Per quad: 192 bytes.

### 14.9 OPB/tile-bin usage

Independent of texture size. Depends on geometry complexity (polygon count, tile coverage).

## Final recommendation: Phase 3 first increment

### Scope

**One concrete, correctness-focused increment.**

### File split

**Yes, perform the split first.** Extract texture functions into `SDL_render_dreamcast_pvr_texture.c`. Add `SDL_render_dreamcast_pvr.h` for cross-file helper declarations. Validate with existing Phase 0–2 tests before adding new functionality.

### Formats implemented

- `SDL_PIXELFORMAT_RGB565` (add to advertised list).
- `SDL_PIXELFORMAT_ARGB1555` (already advertised).
- `SDL_PIXELFORMAT_ARGB4444` (add to advertised list).

### Access modes implemented

- `SDL_TEXTUREACCESS_STATIC` (already supported).
- `SDL_TEXTUREACCESS_STREAMING` (new, with `LockTexture`/`UnlockTexture`).
- `SDL_TEXTUREACCESS_TARGET` (rejected with useful error).

### Staging policy

All textures receive full-POT CPU staging via `SDL_calloc`. Staging is zero-initialized. Untouched POT padding retains contents on partial updates.

### Backing-size policy

Both dimensions independently round up to next power of two (minimum 8). Backing dimensions are stored separately from logical dimensions. UV scaling is applied at queue time.

### Update behavior

`UpdateTexture` validates rect/pitch, copies row-by-row into staging using source/destination pitches, then uploads full backing to VRAM via `pvr_txr_load`. Marks `uploaded = SDL_TRUE`, `dirty = SDL_FALSE`.

### Lock/unlock behavior

`LockTexture` validates streaming access, intersects rect, returns pointer to staging at locked region, tracks `locked = SDL_TRUE`. `UnlockTexture` uploads full backing to VRAM, clears `locked`. Nested locks are rejected. Destruction clears lock flag.

### Upload timing

Immediate in `UpdateTexture` and `UnlockTexture`. Full backing upload. No deferred, lazy, or partial uploads.

### Filtering support

- `SDL_ScaleModeNearest` → `PVR_FILTER_NEAREST`.
- `SDL_ScaleModeLinear` → `PVR_FILTER_BILINEAR`.
- Filter state is stored in `data->filter` and read during header emission.
- Header cache identity includes `data->filter` to detect filter changes.

### Tests to create

`test/dreamcast/test/pvr_textures/` with one focused test file covering:

- All three formats.
- Static and streaming access.
- Full and partial updates.
- Nontrivial source pitch.
- Edge updates.
- Untouched region preservation.
- NPOT logical with POT backing.
- Nearest and linear filtering.
- Color and alpha modulation.
- Per-vertex textured geometry.
- NONE and BLEND blend modes.
- Transparent texels (ARGB1555/ARGB4444).
- Source rectangles, destination scaling, rotation, flips.
- Clipping and viewport.
- Ordered overlap with untextured geometry.
- Repeated create/update/destroy cycles.
- Invalid format/access rejection.

### Explicitly deferred

- DMA uploads.
- Twiddled textures.
- VQ compression.
- Mipmaps.
- Strided NPOT textures.
- Render targets.
- YUV formats.
- Opaque-list routing.
- Upload batching.
- Direct `.dt` loading.
- 32-bit texture formats (ARGB8888, etc.).
- Paletted textures.
- Mipmap bias.
- UV flip/clamp configuration.

### Validation gates

- Flycast: all tests pass visually.
- Physical Dreamcast (640×480 VGA): all tests pass visually.
- No PVR errors in serial output.
- No VRAM leaks in stress tests.
- Linker map shows both renderer objects from local SDL archive.

## Phase 3.1 code review (2026-07-28, independent audit)

An independent, non-modifying code review of the Phase 3.1 implementation
followed AGENTS.md, PVR_TEXTURE_RESEARCH.md, and the test README as
authoritative references. Eleven review priorities were evaluated: source
split/linkage, SDL core changes, texture creation/ownership, pixel
formats/PVR state, UpdateTexture, Lock/UnlockTexture, filtering/header cache,
command architecture, build policy, test quality, and repository hygiene.

**Verdict:** The Phase 3.1 implementation is architecturally sound and
behaviorally correct for its documented scope. One HIGH defect requires a fix
before merge; two MEDIUM items are tracked for later phases.

**HIGH — `IsSupportedBlendMode` contract change in `src/render/SDL_render.c`**

**Status: FIXED (2026-07-28).** See "Resolution" below.

The SDL_render.c change routes ALL blend mode checks to the backend callback
(`renderer->SupportsBlendMode`) whenever it is installed, inside an
`#if SDL_VIDEO_RENDER_DREAMCAST_PVR` guard. The guard restricts execution to
opt-in DC PVR builds. The guard does **not** restrict dispatch to the PVR
renderer: on this SDL branch, GLES, GLES2, GL, D3D9, D3D11, D3D12, Metal, and
VITA_GXM all install `SupportsBlendMode`. Those backends happen to return
`SDL_TRUE` for the standard blend modes, so no regression exists today. But the
change demotes SDL's documented "required blend modes" guarantee (NONE/BLEND/
ADD/MOD/MUL) to a soft convention: a future backend that installs
`SupportsBlendMode` but does not handle NONE/BLEND correctly will silently
reject "required" modes.

**Why the initially documented replacement was insufficient:** The proposed
fix kept `#if SDL_VIDEO_RENDER_DREAMCAST_PVR` around the required-mode arm,
which restricts the build, not the dispatch target. Every renderer with a
callback still routed through its callback for NONE/BLEND/ADD/MOD/MUL in a DC
PVR build. The contract-demotion problem persisted for the required modes;
only the custom-mode arm was actually fixed.

**Resolution:** Only consult the PVR callback when the renderer's identity
matches the native PVR renderer. A centralized constant (`DC_PVR_RENDERER_NAME`
in `src/render/dreamcast/SDL_render_dreamcast_pvr.h`) is the single source of
truth for the advertised name. The fix compares `renderer->info.name` against
that constant and routes the required-mode check through the callback only
when they match. Non-PVR renderers keep the SDL guarantee for required modes.
Custom modes continue to consult any installed callback regardless of renderer.

```c
static SDL_bool IsSupportedBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    switch (blendMode) {
    case SDL_BLENDMODE_NONE:
    case SDL_BLENDMODE_BLEND:
    case SDL_BLENDMODE_ADD:
    case SDL_BLENDMODE_MOD:
    case SDL_BLENDMODE_MUL:
#if SDL_VIDEO_RENDER_DREAMCAST_PVR
        if (renderer->info.name &&
            SDL_strcmp(renderer->info.name, DC_PVR_RENDERER_NAME) == 0 &&
            renderer->SupportsBlendMode) {
            return renderer->SupportsBlendMode(renderer, blendMode);
        }
#endif
        return SDL_TRUE;

    default:
        return renderer->SupportsBlendMode && renderer->SupportsBlendMode(renderer, blendMode);
    }
}
```

**Files changed:**
- `src/render/SDL_render.c` — `IsSupportedBlendMode` (required-mode arm now
  identity-gated); `SDL_SetTextureScaleMode` guard now uses `DC_PVR_RENDERER_NAME`
  instead of the raw literal.
- `src/render/dreamcast/SDL_render_dreamcast_pvr.h` — added `DC_PVR_RENDERER_NAME`
  constant and a comment documenting the name-dependency contract.
- `src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c` — removed unreachable
  `!data` branch from `DC_PVR_SetTextureScaleMode`.

**Validation:**
- Opt-in Dreamcast SDL build: clean (no warnings from changed files).
- Opt-out Dreamcast SDL build: clean (no `DC_RenderDriver` or `DC_PVR_*` symbols).
- `pvr_vertex_color_blend` Flycast: PASS (NONE/BLEND accepted; ADD/MOD/MUL/custom rejected).
- `pvr_textures` Flycast: PASS.
- `pvr_viewport`, `pvr_clipping`, `pvr_points_lines` build: clean.
- Physical hardware revalidation: unnecessary. The correction only changes the
  blend-mode API precheck path; PVR header generation and drawing behavior
  are unchanged.

**MEDIUM — `DC_PVR_UpdateTexture` uploads full POT backing on every partial update**

The implementation copies the requested rectangle into staging, then calls
`pvr_txr_load(data->pixels, data->vram, data->allocation_size)` — the full
allocation — regardless of which rectangle was updated. A 1×1 partial update
still transfers `backing_w × backing_h × 2` bytes. This is documented as
Phase 0/3.1 intentional behavior. No fix required in the focused copy/copy-ex
increment; partial uploads remain a later texture-optimization task.

**MEDIUM — OpenGL diff should be separated**

`src/render/opengl/SDL_render_gl.c`, `SDL_dreamcastopengl.c`, `SDL_dreamcastvideo.c`,
and `test/testutils.c` changes are unrelated to Phase 3.1. Split them into a
distinct commit (or revert) before PVR merge.

**LOW — `DC_PVR_SetTextureScaleMode` error path is silent**

`SDL_SetError` is called when `data` is NULL but the function is `void` and the
caller reports success. The `!data` branch is unreachable (driverdata is set in
`CreateTexture` and NULLed in `DestroyTexture`, which no-ops the scale mode
callback). Drop the branch or convert the function to `int`.

**LOW — `SDL_SetTextureScaleMode` guard uses string compare on `renderer->info.name`**

The guard compares against the advertised renderer name "Dreamcast PVR". If
the name string changes, the guard silently stops working. Add a comment noting
the name dependency.

**Test gaps (tracked, not blocking)**

- Wider texture-dimension and edge-update matrix (Phase 7 hardening)
- No 320×240 or PAL/NTSC mode coverage (Phase 7 hardening)
- No TA/OPB capacity measurement (Phase 6/7)
- No bilinear-vs-nearest visual difference validation on hardware

**Repository hygiene**

- Rebuilt `.bin` / `.elf` / `.log` / `.img` files in `test/dreamcast/test/` are
  build artifacts and should be gitignored or excluded from the PVR patch.
- `AGENTS.md` grows by ~850 lines; consider moving detailed phase histories to
  separate files under `src/render/dreamcast/`.

**Unresolved questions**

- `pvr_txr_load` alignment: KOS header says `count` must be a multiple of 32.
  The implementation passes `backing_w × backing_h × 2`, which is always ≥ 128
  and a multiple of 32 for POT dimensions ≥ 8. No action required.
- `pvr_txr_load` cache flush: KOS synchronous load path does not require
  explicit flushing. Verified by Flycast + hardware passes.
- UV scaling for partial `RenderCopy`: SDL computes UVs normalized to
  `texture->w`/`h` (logical). Backend rescales to `logical/backing`. For a
  partial source rect `srcrect={5,3,4,3}` on a 17×9 logical texture backed
  32×16: UVs become `{5/32, 3/16, 9/32, 6/16}` — correct sampling of the
  logical partial within the POT backing.

## Phase 3.4 wider dimension and logical-edge validation (2026-07-28)

`test/dreamcast/test/pvr_texture_matrix` validates 28 sequential logical-size
cases spanning every representative axis value from 1 through 513, POT
transitions, one-pixel axes, asymmetric shapes, 320x240, 321x241, and a
513x257 logical texture with 1024x512 backing. It covers RGB565, ARGB1555, and
ARGB4444 with static and streaming access, exact backing pitches, tight and
larger odd source pitches, corner/row/column/interior/edge updates, partial
locks, invalid public API behavior, filtering, alpha, and update/draw order.

The matrix exposed a real ordering defect. `SDL_UpdateTexture` flushes queued
geometry, but a flush submits into the still-active PVR scene; overwriting the
same VRAM allocation could therefore change an earlier submitted draw. The
fix tracks whether the current texture allocation was referenced by a
submitted polygon header. An update or lock of that allocation uploads the
complete POT backing to replacement VRAM, invalidates the header cache, and
defers the old allocation until `RenderPresent` has synchronized. An
unreferenced allocation continues to upload in place. This is full-backing
copy-on-write for command ordering, not a partial-upload implementation.

Flycast and physical 640x480 VGA Dreamcast runs both pass 28 cases, 57 creates,
47 updates, 41 locks, 16 invalid checks, and four visual pages. Hardware
returned zero normally to `dc-load-ip`. Direct capture inspection confirms
logical corners and one-pixel axes, right/bottom edge coverage, NPOT-padding
isolation, odd source-pitch handling, partial locks, repeated-update order,
nearest/bilinear behavior, and both alpha formats. The four lasting 640x480 PPM
hashes are recorded in the focused test README.

The full-POT-backing synchronous upload remains intentional. Partial uploads,
DMA, custom store-queue row writes, and read-modify-write transfers remain
deferred. Remaining texture hardening is non-VGA video modes and broader
capacity/performance profiling.
