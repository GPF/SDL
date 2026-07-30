# Dreamcast Native PVR Renderer: Phase 6.2 Strided NPOT Texture Research

**Status:** Research and physical-hardware microprobe complete.
**Date:** 2026-07-29
**Scope:** KOS strided texture API audit, stride-register semantics, multi-texture
coexistence, UV endpoint rules, bilinear overread bounds, upload/lock semantics,
`.dt` integration, scene-transition policy, candidate-policy comparison, bounded
physical microprobe design, guarded implementation plan, VRAM/bandwidth equations,
validation matrix, acceptance gates, and rejection conditions.

**Related research:**
- `PVR_TEXTURE_RESEARCH.md` — Phase 3 baseline texture subsystem (POT,
  nontwiddled, full-backing upload).
- `PVR_DT_TEXTURE_RESEARCH.md` — Phase 6.1 direct `.dt` loading (twiddled,
  VQ, mipmapped; strided and partial `.dt` rejected).
- `PVR_PARTIAL_UPLOAD_RESEARCH.md` — Partial-upload strategy audit; deferred.
- `PVR_RENDER_TARGET_RESEARCH.md` — RTT preservation prototype and
  performance results (paired-POT allocation cost).
- `AGENTS.md` — Renderer architecture, validation workflow, and phase gates.

---

## 1. Verdict

**Feasible with a strict global-stride constraint.** Physical Dreamcast
hardware confirms strided RGB565 textures, same-stride pairs, ordinary POT
coexistence, padded-pitch updates, edge sampling, and stride changes between
completed scenes/frames. Different strides in one list do not coexist: page 5
shows that the final register value applies globally and corrupts the earlier
texture while leaving the matching-stride texture and POT control correct. Use
**Policy A + D**: one active stride per scene, batch only matching strides, and
use POT fallback for conflicts.

---

## 2. Source-Backed KOS/PVR Findings

### 2.1 Stride mechanism

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_txr.h`

```c
void pvr_txr_set_stride(size_t texture_width);
size_t pvr_txr_get_stride(void);
```

`pvr_txr_set_stride()` configures the `PVR_TEXTURE_MODULO` register (offset
`0x00e4`). The bottom 5 bits store `texture_width / 32`. The stride value is
the multiplier read back as `FIELD_GET(reg, PVR_TXR_STRIDE_MULT) * 32`.

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_regs.h`

```c
#define PVR_TEXTURE_MODULO      0x00e4   /* Output texture width modulo */
#define PVR_TXR_STRIDE_MULT     GENMASK(4, 0)   /* Bottom 5 bits: size/32 */
```

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_texture.c`
(lines 25-35)

```c
void pvr_txr_set_stride(size_t texture_width) {
    uint32_t temp = texture_width / 32;
    /* Width must be non-zero multiple of 32, fits in 5 bits (max 992). */
    assert(temp && __is_aligned(texture_width, 32) && !(temp & ~PVR_TXR_STRIDE_MULT));
    temp = FIELD_PREP(PVR_TXR_STRIDE_MULT, temp);
    temp |= PVR_GET(PVR_TEXTURE_MODULO) & ~PVR_TXR_STRIDE_MULT;
    PVR_SET(PVR_TEXTURE_MODULO, temp);
}
```

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_texture.c`
(lines 40-43)

```c
size_t pvr_txr_get_stride(void) {
    uint32_t reg = PVR_GET(PVR_TEXTURE_MODULO);
    return FIELD_GET(reg, PVR_TXR_STRIDE_MULT) * 32;
}
```

**Findings:**
- The stride register is **global** — one value per frame, last writer wins.
- All textures using `PVR_TXRFMT_X32_STRIDE` in the same frame share this
  single stride width.
- Width must be a non-zero multiple of 32, up to 992 pixels (5-bit limit:
  `992 / 32 = 31`, which fits in 5 bits; `1024 / 32 = 32` does not).
- The register is reset to `0x00000000` at `pvr_init()` time
  (`pvr_init_shutdown.c` line 201: `PVR_SET(PVR_TEXTURE_MODULO, 0x00000000)`).
- When stride is zero, the PVR defaults to POT stride interpretation (the
  `PVR_TXRFMT_POW2_STRIDE` path).

### 2.2 Stride format flag

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr.h`
(lines 316-321)

```c
#define PVR_TXRFMT_POW2_STRIDE  (0 << 25)   /* Stride is a power of two */
#define PVR_TXRFMT_X32_STRIDE   (1 << 25)   /* Stride is multiple of 32 */
static const uint32_t PVR_TXRFMT_NOSTRIDE __depr(...) = PVR_TXRFMT_POW2_STRIDE;
static const uint32_t PVR_TXRFMT_STRIDE   __depr(...) = PVR_TXRFMT_X32_STRIDE;
```

`PVR_TXRFMT_X32_STRIDE` is bit 25, OR-able with format selects:

```
PVR_TXRFMT_RGB565       = (1 << 27)
PVR_TXRFMT_ARGB1555     = (0 << 27)
PVR_TXRFMT_ARGB4444     = (2 << 27)
PVR_TXRFMT_NONTWIDDLED  = (1 << 26)
PVR_TXRFMT_TWIDDLED     = (0 << 26)
PVR_TXRFMT_VQ_DISABLE   = (0 << 30)
PVR_TXRFMT_VQ_ENABLE    = (1 << 30)
```

**Constraint:** Twiddled textures **cannot** use `PVR_TXRFMT_X32_STRIDE`.
Paletted textures require twiddling. Mipmapped textures require twiddling.
Therefore **strided textures cannot be twiddled, paletted, or mipmapped**.

### 2.3 Example: strided texture usage

**File:** `/opt/toolchains/dc/kos/examples/dreamcast/pvr/strided_texture/strided_texture.c`

```c
/* Format: RGB565 | NONTWIDDLED | X32_STRIDE */
pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
    PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_X32_STRIDE,
    TEXTURE_PADDED_WIDTH,  /* POT width, e.g. 1024 */
    TEXTURE_PADDED_HEIGHT, /* POT height, e.g. 512 */
    board_texture,          /* VRAM pointer */
    PVR_FILTER_NONE);

/* Set global stride before drawing */
pvr_txr_set_stride(TEXTURE_WIDTH);  /* e.g. 640 */
```

**Key observations from the example:**
- The context `width` and `height` parameters remain POT values (1024x512).
- The stride register is set globally, not per-texture.
- UV endpoints use `TEXTURE_WIDTH / TEXTURE_PADDED_WIDTH` (e.g. 640/1024),
  not `1.0f`.
- The example uses `PVR_FILTER_NONE` (nearest), not bilinear.

### 2.4 RTT stride requirements

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr.h`
(lines 976-982, `pvr_scene_begin_rtt` documentation)

```
stride_px must be >= render_w.
stride_px must be a multiple of 4 pixels (for 16-bit targets).
```

**File:** `/opt/toolchains/dc/kos/examples/dreamcast/pvr/pvr_rtt_sized/pvr_rtt_sized.cc`
(line 107)

```c
return mode->stride_px * mode->h * RGB565_BYTES;
```

RTT allocation uses `stride_px * height * 2` bytes, not POT width.

### 2.5 Texture upload

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_txr.h`
(lines 86-93)

```c
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count);
```

`pvr_txr_load` copies a contiguous block of `count` bytes (multiple of 32)
from main RAM to VRAM. It does **not** accept stride or pitch parameters.
Partial VRAM updates require the caller to compute source offsets and call
`pvr_txr_load` multiple times or with a pre-pitched buffer.

### 2.6 VRAM allocation

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_mem.h`

`pvr_mem_malloc()` returns 32-byte-aligned VRAM pointers. Allocation granularity
is 32 bytes. `pvr_mem_available()` reports remaining texture-heap bytes.

---

## 3. Current SDL Native-Renderer Constraints

### 3.1 Texture data layout (`SDL_render_dreamcast_pvr_data.h`)

```c
typedef struct DC_PVR_TextureData {
    struct DC_PVR_TextureData *next_deferred;
    void *pixels;                  /* CPU staging (zero-initialized) */
    pvr_ptr_t vram;                /* VRAM base */
    pvr_ptr_t sample_vram;         /* VRAM base (adjusted for reduced VQ) */
    pvr_ptr_t lock_replacement_vram;
    struct DC_PVR_DeferredVRAM *lock_deferred_vram;
    size_t allocation_size;
    Uint32 sdl_format;
    int pvr_format;                /* Format flags (includes twiddle/stride) */
    int logical_w, logical_h;
    int backing_w, backing_h;      /* Current: POT dimensions */
    int cpu_pitch;                 /* Bytes per row in staging */
    int vram_pitch;                /* Bytes per row in VRAM */
    SDL_TextureAccess access;
    pvr_filter_mode_t filter;
    SDL_bool uploaded, dirty;
    SDL_bool referenced_in_scene;
    SDL_bool locked;
    SDL_bool preformatted;
    SDL_Rect locked_rect;
    /* RTT prototype fields under DC_PVR_RENDER_TARGET_PROTOTYPE */
} DC_PVR_TextureData;
```

### 3.2 Current CreateTexture path (`SDL_render_dreamcast_pvr_texture.c`)

```c
data->backing_w = DC_PVR_NextPowerOfTwo(texture->w);
data->backing_h = DC_PVR_NextPowerOfTwo(texture->h);
data->cpu_pitch = data->backing_w * 2;
data->vram_pitch = data->cpu_pitch;
data->allocation_size = (size_t)data->vram_pitch * data->backing_h;
data->pvr_format = pvr_format | PVR_TXRFMT_NONTWIDDLED;
```

VRAM allocation: `pvr_mem_malloc(data->allocation_size)`.
CPU staging: `SDL_calloc(1, data->allocation_size)`.
Both use POT backing dimensions.

### 3.3 Current header emission (`SDL_render_dreamcast_pvr.c`, `DC_PVR_EmitHeader`)

```c
pvr_poly_cxt_txr(&context, PVR_LIST_TR_POLY,
    texturedata->pvr_format,
    texturedata->backing_w,   /* POT width */
    texturedata->backing_h,   /* POT height */
    texturedata->sample_vram,
    texturedata->filter);
```

No stride register is set. UV scaling uses `logical / backing` (logical / POT).

### 3.4 Header cache identity

The header cache key includes: texture pointer, blend mode, texture environment,
and filter. **Stride is not part of the cache key.** This is correct for the
current POT-only path but will need revision if strided textures are introduced
with per-texture stride.

### 3.5 Current capability advertising (`DC_RenderDriver`)

```c
{
    "Dreamcast PVR",
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
    3,
    { SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_ARGB1555, SDL_PIXELFORMAT_ARGB4444 },
    1024,   /* max texture width */
    1024    /* max texture height */
}
```

No flag indicates strided support. Capability advertising may need revision
only if strided textures enable NPOT dimensions beyond the current POT-backed
NPOT support (which already handles NPOT logical sizes via POT backing).

---

## 4. Answering the Research Questions

### 4.1 What exact PVR/KOS APIs, fields, alignment constraints, and legal width ranges govern strided textures?

| Item | Value |
|------|-------|
| API | `pvr_txr_set_stride(width)` |
| Register | `PVR_TEXTURE_MODULO` (0x00e4), bits [4:0] |
| Width constraint | Non-zero multiple of 32, max 992 pixels |
| Format flag | `PVR_TXRFMT_X32_STRIDE` (bit 25) |
| KOS assertion | `__is_aligned(width, 32) && width > 0 && width <= 992` |
| Context width/height | Must still be POT values ≥ actual width/height |
| VRAM stride bytes | `stride_px * 2` (for 16-bit) |
| Allocation size | `stride_px * backing_h * 2` (not POT width) |
| UV scaling | `logical / backing` (backing is POT, not stride) |
| Reset | Stride resets to 0 at `pvr_init()` |

### 4.2 Is stride configured when a polygon header is compiled/submitted, when the scene renders, or through one global register?

**Through one global register.** `pvr_txr_set_stride()` writes
`PVR_TEXTURE_MODULO` directly. The value persists until the next call or
`pvr_init()` reset. All textures with `PVR_TXRFMT_X32_STRIDE` in the same
frame share this single stride width. The stride takes effect when the TA
processes the compiled polygon header's texture fetches during rendering.

### 4.3 Can two strided textures with different strides render correctly?

| Scenario | Answer | Evidence |
|----------|--------|----------|
| Same polygon list | **No** | Hardware page 5: final 384 stride corrupts the earlier 640 texture. |
| Same scene, different lists | **No** | Hardware page 6: opaque-list 640 texture corrupts after transparent-list stride changes to 384. |
| Separate completed frames | **Yes** | Hardware page 7: the captured 640 frame is correct after a completed 384 frame. |
| Same frame, same stride | **Yes** | Hardware page 4: logical widths 512 and 384 both render correctly with physical stride 512. |

**Hardware result:** Stride applies per frame, not per texture or polygon list.
All strided textures in a frame use the final register value. Ordinary POT
textures are unaffected.

### 4.4 Can strided and ordinary POT/twiddled textures coexist in one scene?

**Yes.** `PVR_TXRFMT_POW2_STRIDE` (the default, bit 25 = 0) and
`PVR_TXRFMT_X32_STRIDE` (bit 25 = 1) are mutually exclusive format flags.
The PVR interprets stride width based on the flag in each texture's context.
POT textures ignore the `PVR_TEXTURE_MODULO` register entirely. Twiddled
textures also ignore it (KOS doc: "Textures that are twiddled cannot use the
`PVR_TXRFMT_X32_STRIDE` flag so the stride set here will not apply to them").

### 4.5 What widths, pitches, alignments, formats, filtering modes, and UV rules are valid?

| Item | Valid range / rule |
|------|-------------------|
| Logical width | 1–992 (must be multiple of 32 for strided) |
| Logical height | 1–1024 (no stride constraint) |
| Stride width | 32–992, multiple of 32 |
| Backing (POT) width | POT ≥ stride width, max 1024 |
| Backing (POT) height | POT ≥ logical height, max 1024 |
| VRAM alignment | 32 bytes (from `pvr_mem_malloc`) |
| VRAM allocation | `stride_px * backing_h * 2` bytes |
| Formats | RGB565, ARGB1555, ARGB4444 (all 16-bit) |
| Filtering | Nearest and bilinear (untested for strided) |
| UV max U | `(logical_w - 1) / backing_w` |
| UV max V | `(logical_h - 1) / backing_h` |
| UV scale | `stride / backing` (not `logical / backing`) for full coverage |

### 4.6 Does bilinear filtering read past the logical right edge?

**Yes, bilinear filtering reads one texel past the logical edge on each axis.**
For a strided texture of logical width `W` and stride `S`:

- The rightmost sampled column is column `W-1`.
- Bilinear interpolation samples columns `W-2` and `W-1` (or `W-1` and `W`
  if UV endpoint reaches 1.0).
- Column `W` lies in the POT padding region (columns `W` through `S-1`).
- If the padding region is zero-initialized (which it will be for
  `SDL_calloc`), stale data is not visible, but the **read still occurs** and
  the result is influenced by zeros.

**Implication:** For bilinear textures, the POT padding beyond the stride must
be zero-initialized or intentionally filled, or UV endpoints must be clamped
to `(logical - 1) / backing` to prevent sampling into padding. The RTT
preservation research (AGENTS.md Phase 5.2) already validated the
`(logical - 1) / backing` UV endpoint policy for edge safety.

### 4.7 How should partial updates and streaming locks behave with differing CPU pitch, logical width, and PVR stride?

**CPU pitch** = `stride_px * 2` (bytes per row in the CPU staging buffer that
matches the VRAM stride layout). For streaming textures, the CPU staging must
mirror the VRAM stride layout so that `pvr_txr_load` can copy row-by-row
without reordering.

**Update semantics:**
- `rect->x, rect->y, rect->w, rect->h` are in logical coordinates.
- Source rows advance by `pitch` bytes (caller-provided).
- Destination rows advance by `cpu_pitch = stride_px * 2` bytes.
- The destination region in VRAM occupies rows `stride_px * rect->y` through
  `stride_px * (rect->y + rect->h - 1)`, offset by `rect->x * 2` bytes
  within each row.

**Upload strategy:** Full-backing upload remains the simplest correct path:
upload `stride_px * backing_h * 2` bytes per `pvr_txr_load` call. The CPU
staging buffer must be allocated at `stride_px * backing_h * 2` bytes.

**Streaming lock:** `*pitch` should return `cpu_pitch = stride_px * 2` so
that caller writes rows that match the VRAM stride layout directly.

### 4.8 Can strided `.dt` payloads be supported by the same policy?

**Initially no.** Phase 6.1 rejects strided `.dt` payloads. The `DcTx` parser
currently validates POT dimensions only. Supporting strided `.dt` requires:
1. Extending the `DcTx` parser to accept non-POT widths (multiple of 32).
2. Storing stride in the `SDL_DreamcastTextureData` layout.
3. Setting the stride register before the first draw that samples the texture.
4. Ensuring the `.dt` payload size matches `stride * height * 2` (not
   `backing_w * height * 2`).

**Recommendation:** Phase 6.2 should initially cover **renderer-created**
strided textures only. Strided `.dt` support can be added in a later increment
once the renderer-side policy is validated.

### 4.9 At what point must queued draws be flushed or split if the required stride changes?

**At the point a draw command requires a different stride than the currently
active stride register.** Because the stride register is global and affects all
strided textures in the frame, changing it mid-scene would corrupt draws that
already reference the old stride.

**Two safe policies:**
- **Policy A (flush/split):** Before emitting geometry that requires a new
  stride, flush any pending geometry for the current stride, set the new
  stride, begin a new scene (or at minimum a new list), and continue.
- **Policy D (same-stride batching):** Reject or fall back to POT padding when
  a new texture's stride conflicts with the stride already in use for queued
  draws.

**Recommendation:** Start with Policy D. Flush/split (Policy A) is more
complex and risks destination-destructive scene transitions, as observed in
Phase 5.1 RTT persistence failures.

### 4.10 Would scene splitting preserve SDL painter order and destination contents?

**Unknown — requires hardware validation.** Phase 5.1 demonstrated that simple
scene transitions can lose destination contents (screen S1 lost across
screen→RTT→screen, target T1 lost after reselection). Phase 5.2 validated a
preservation-scene architecture that seeds the next destination from the
previous one before transition.

**Risk:** If scene splitting is required for stride changes, the preservation
seed mechanism must be applied to avoid losing previously rendered content.
This adds complexity and performance cost.

### 4.11 What VRAM and upload-bandwidth savings are expected?

**VRAM savings vs. POT padding:**

| Logical size | POT allocation (bytes) | Strided allocation (stride=logical, 32-aligned) | Savings |
|-------------|-----------------------|------------------------------------------------|---------|
| 640×480 | 1024×512×2 = 1,048,576 | 640×480×2 = 614,400 | 434,176 (41.4%) |
| 320×240 | 512×256×2 = 262,144 | 320×240×2 = 153,600 | 108,544 (41.4%) |
| 512×256 | 512×256×2 = 262,144 | 512×256×2 = 262,144 | 0% (POT = stride) |
| 384×288 | 512×512×2 = 524,288 | 384×288×2 = 221,184 | 303,104 (57.9%) |
| 128×96 | 128×128×2 = 32,768 | 128×96×2 = 24,576 | 8,192 (25.0%) |
| 960×540 | 1024×1024×2 = 2,097,152 | 960×540×2 = 1,036,800 | 1,060,352 (50.5%) |

**Upload bandwidth savings:** Proportional to VRAM savings for full-backing
uploads. A 640×480 strided texture uploads 614,400 bytes vs. 1,048,576 for
POT — a 41.4% reduction.

### 4.12 What failure and rollback behavior is required?

| Failure point | Required behavior |
|---------------|------------------|
| `pvr_txr_set_stride()` called with invalid width | KOS assertion failure; caller must validate before calling |
| `pvr_mem_malloc()` fails for strided allocation | Return `SDL_SetError("Could not allocate Dreamcast PVR texture VRAM")`; fall back to POT if the caller permits |
| Stride conflict (two textures, different strides, same scene) | Policy A: flush/split; Policy D: fall back to POT padding |
| Bilinear overread into zero padding | Acceptable; result is correct interpolation toward zero |
| Bilinear overread into stale padding | **Reject** — must zero-init padding or clamp UVs |

---

## 5. Candidate-Policy Comparison

| Policy | Description | Pros | Cons | Complexity | Hardware risk |
|--------|-------------|------|------|------------|---------------|
| **A** | One strided width per scene; POT fallback for conflicts | Simple, correct, no scene splitting | Wastes VRAM on conflict textures | Low | Low |
| **B** | Flush/split scenes on stride change | Maximizes strided usage | Destination-destructive transitions; SDL painter-order risk | High | **High** |
| **C** | Renderer-wide fixed stride | Simplest; no per-texture stride tracking | One stride for all strided textures; limits use cases | Low | Medium |
| **D** | Same-stride batching; POT fallback for conflicts | Safe; no scene splitting; simple | Same constraint as A but enforced at draw time | Low | Low |
| **E** | Reject strided `.dt`; allow renderer strided | Keeps `.dt` parser simple | Inconsistent .dt support | Low | Low |
| **F** | Reject all strided support | No risk; POT padding works | Misses VRAM savings | None | None |

**Recommendation: Policy A + D.** Use one stride per scene (A) as the
fundamental constraint, with same-stride batching (D) as the enforcement
mechanism. Fall back to POT padding when a new texture's stride conflicts.
This avoids scene-splitting risks while still capturing most VRAM savings.

---

## 6. Bounded Physical Microprobe Design

### 6.1 Design goals

- Use **public KOS/PVR APIs only**.
- **Fixed iteration and allocation bounds.**
- **Return normally** to `dc-load-ip`.
- **Capture unique raw PPM** for each page.
- **Print machine-readable serial results** but treat framebuffer inspection
  as authoritative.

### 6.2 Test structure: `test/dreamcast/test/pvr_strided_texture_microprobe/`

**Pages and captures:**

| Page | Test | Capture | Expected outcome |
|------|------|---------|-----------------|
| 1 | Single 640×480 strided texture, nearest | `phase6_2-page1.ppm` | Correct full-screen pattern |
| 2 | Single 640×480 strided texture, bilinear | `phase6_2-page2.ppm` | Correct filtered pattern; tight-row edge behavior observed |
| 3 | Strided and ordinary POT panels in one scene | `phase6_2-page3.ppm` | Both correct; POT unaffected |
| 4 | Two logical widths sharing stride 512 | `phase6_2-page4.ppm` | Both correct with logical UV extents |
| 5 | Strides 640 then 384 in one list | `phase6_2-page5.ppm` | 640 panel corrupted; 384 and POT controls correct |
| 6 | Different strides in opaque/transparent lists of one scene | `phase6_2-page6.ppm` | Earlier 640 panel corrupted; final 384 panel correct |
| 7 | Different strides in consecutive frames | `phase6_2-page7.ppm` | Captured second frame correct |
| 8 | Partial update from padded CPU pitch | `phase6_2-page8.ppm` | Exact yellow 64×64 update |
| 9 | Logical edges and one-pixel source regions | `phase6_2-page9.ppm` | Corner, final-column, and final-row probes correct |
| 10 | Ordinary POT plus strided recovery reference | `phase6_2-page10.ppm` | Both correct; exact VRAM recovery |

### 6.3 Fixed bounds

- Maximum texture allocations per page: 3.
- Maximum stride values tested: 384, 512, 640.
- Maximum logical dimensions: 640×480.
- Maximum POT backing: 1024×512.
- Peak page allocation: 1,179,648 bytes (page 5).
- Dynamic allocations use fixed dimensions and bounded loops.

### 6.4 Expected outcomes (pre-hardware)

| Page | Expected |
|------|----------|
| 1 | Correct full-screen display, no overread |
| 2 | Correct bilinear pattern; tight-row right edge is observational |
| 3 | Both textures visible, POT texture unaffected by stride setting |
| 4 | Both textures correct (same stride works) |
| 5 | Final global stride likely corrupts the first-set texture |
| 6 | Final global stride applies across list boundaries |
| 7 | Captured consecutive frame correct after stride change |
| 8 | Updated region matches source; surrounding area unchanged |
| 9 | Logical edge and one-pixel source probes correct |
| 10 | POT and strided references correct; no retained allocations |

### 6.5 Hardware commands

```sh
# Upload and run
kos-tool -t 192.168.0.128 -x pvr_strided_texture_microprobe.elf -m .

# Build with HARDWARE_SCREENSHOTS=1.
# Each page writes to /pc/pvr_strided_texture_microprobe-phase6_2-page<N>.ppm
```

### 6.6 Physical-hardware result (2026-07-29)

The screenshot-enabled strict build passed on a real Dreamcast at 640×480 VGA.
All requested stride values read back exactly, ten raw framebuffer captures
were written successfully, PVR shutdown completed, VRAM returned exactly to
the post-allocator-warmup baseline of 5,789,416 bytes, and dc-load reported
`Program returned 0`.

Direct inspection confirms pages 1–4 and 7–10 match their contracts. Page 5
shows that after submitting the 640-pitch panel and changing the register to
384 in the same list, the left panel displays regular stride-corruption bands;
the 384-pitch right panel and ordinary POT control remain correct. Page 6
repeats the conflict across opaque and transparent list boundaries in one
scene and produces the same result: the earlier 640 panel is corrupted and the
final 384 panel is correct. The register is frame-global, not per-list.
Same-stride batching and POT fallback remain safe.

---

## 7. Guarded Implementation Plan

### 7.1 Files to modify

| File | Change |
|------|--------|
| `src/render/dreamcast/SDL_render_dreamcast_pvr_data.h` | Add `int stride_px` to `DC_PVR_TextureData`; add `int active_stride` to `DC_PVR_RenderData` |
| `src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c` | Add stride-aware allocation in `CreateTexture`; add stride register management |
| `src/render/dreamcast/SDL_render_dreamcast_pvr.c` | Add stride register setup in `DC_PVR_EmitHeader`; add stride conflict detection in `RunCommandQueue` |
| `src/render/dreamcast/SDL_render_dreamcast_pvr.h` | No changes required (public API unchanged) |
| `CMakeLists.txt` (SDL2 root) | No changes required (Dreamcast-specific code is in `src/render/dreamcast/`) |
| `test/dreamcast/test/pvr_strided_texture/` | New test directory |

### 7.2 Compile-time guards

All strided texture code must be inside `#if SDL_VIDEO_RENDER_DREAMCAST_PVR`
and guarded by a new define `DC_PVR_STRIDED_TEXTURES` (disabled by default):

```c
#if SDL_VIDEO_RENDER_DREAMCAST_PVR && defined(DC_PVR_STRIDED_TEXTURES)
// strided texture code
#endif
```

### 7.3 Texture layout and allocation rules

**When stride is enabled:**

1. Check if logical width is a multiple of 32 and ≤ 992.
2. If yes, allocate strided: `stride_px = logical_w`,
   `allocation_size = stride_px * backing_h * 2`.
3. If no (logical width not multiple of 32, or > 992), fall back to POT.
4. CPU staging pitch: `cpu_pitch = stride_px * 2`.
5. VRAM pitch: `vram_pitch = stride_px * 2`.
6. Format flag: `pvr_format = base_format | PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_X32_STRIDE`.

**Stride conflict detection (Policy A):**

- Track `renderdata->active_stride` (initially 0 = POT mode).
- Before emitting a strided texture header:
  - If `stride_px != active_stride`, flush pending geometry (if any),
    set stride, invalidate header cache.
  - If flush is not viable (destination-destructive), fall back to POT.

### 7.4 Renderer state for active stride

```c
typedef struct DC_PVR_RenderData {
    // ... existing fields ...
#if defined(DC_PVR_STRIDED_TEXTURES)
    int active_stride;          /* Current PVR_TEXTURE_MODULO value; 0 = POT */
    SDL_bool stride_dirty;      /* Stride register needs update */
    int pending_stride;         /* Stride required by next draw (0 = no pending) */
#endif
} DC_PVR_RenderData;
```

### 7.5 Command-queue / scene transition rules

**Rule 1:** Queue callbacks remain CPU-only. They capture the texture pointer;
stride decisions happen at emission time.

**Rule 2:** `RunCommandQueue` checks `pending_stride` at each geometry command
that samples a strided texture. If `pending_stride != active_stride`, flush
and switch.

**Rule 3:** If flushing is not viable (e.g., destination is a render target
with uncommitted content), fall back the conflicting texture to POT padding.

**Rule 4:** Stride changes at scene boundaries are free (no flush needed).
Set stride in `DC_PVR_BeginFrame` or `DC_PVR_EmitHeader` before the first
strided draw.

### 7.6 Upload and lock semantics

- **UpdateTexture:** Full-backing upload unchanged. Upload
  `stride_px * backing_h * 2` bytes. CPU staging buffer allocated at that size.
- **LockTexture:** `*pitch` returns `cpu_pitch = stride_px * 2`. Caller writes
  rows that match VRAM stride layout.
- **UnlockTexture:** Full-backing upload unchanged.
- **Strided partial uploads:** Deferred to a later phase. Full-backing upload
  remains the correct simple path.

### 7.7 Header-cache invalidation

- **Stride change invalidates header cache.** When stride changes, set
  `renderdata->header_valid = SDL_FALSE` and `renderdata->header_texture = NULL`.
- Stride is **not** part of the cache key for POT textures (stride is 0).
- For strided textures, stride is implicitly part of the texture's identity
  (each strided texture has a fixed stride).

### 7.8 Fallback to POT padding

A texture falls back to POT padding when:
1. Logical width is not a multiple of 32.
2. Logical width > 992.
3. Stride conflict cannot be resolved by flush (destination-destructive).

Falling back uses the existing POT path: `backing_w = next_pot(logical_w)`,
`allocation_size = backing_w * backing_h * 2`.

### 7.9 Capability/advertising impact

No change to `DC_RenderDriver` capability flags. The current POT-backed NPOT
support already allows NPOT logical dimensions via POT backing. Strided
textures are an internal optimization, not a new capability.

### 7.10 Test additions

New test: `test/dreamcast/test/pvr_strided_texture/`

```
Makefile.dc
pvr_strided_texture.c
pvr_strided_texture.bin
pvr_strided_texture.elf
pvr_strided_texture-hardware-page1.ppm
pvr_strided_texture-hardware-page2.ppm
... (up to page 10)
README.md
```

The test covers all 10 pages described in Section 6.

### 7.11 Build commands

```sh
# Opt-in build with strided support
cd SDL2
cd build-scripts && ./dreamcast.sh build

# With strided support enabled (via CMake or define)
# Add -DCMAKE_C_FLAGS="-DDC_PVR_STRIDED_TEXTURES" to the build

# Build test
cd test/dreamcast/test/pvr_strided_texture_microprobe
make -f Makefile.dc HARDWARE_SCREENSHOTS=1
```

### 7.12 Physical capture workflow

1. Build with `HARDWARE_SCREENSHOTS=1`.
2. Run only on physical Dreamcast hardware.
3. Inspect each raw framebuffer capture for:
   - Correct texture display (no corruption, no leakage).
   - Edge behavior (no overread artifacts).
   - Stride conflict behavior (page 5).
4. Record physical-hardware observations for each page.

### 7.13 Physical hardware commands and expected captures

```sh
# Page 1: Single strided nearest
kos-tool -t 192.168.0.128 -x pvr_strided_texture_microprobe.elf -m .
# Expected: full-screen chessboard, no artifacts

# Page 2: Single strided bilinear
# Expected: correct filtered pattern; tight-row edge is observational

# Page 3: Alternating strided/POT
# Expected: both visible, POT unaffected

# Page 4: Same-stride pair
# Expected: both correct

# Page 5: Different-stride same list
# Hardware: earlier 640 panel corrupt; final 384 and POT control correct

# Page 6: Different-stride opaque/transparent lists in one scene
# Hardware: earlier 640 panel corrupt; final 384 panel correct

# Page 7: Consecutive frames with different strides
# Expected: captured final frame correct

# Page 8: Partial upload
# Expected: updated region correct

# Page 9: Logical edges and one-pixel source probes
# Expected: corner, final-column, and final-row probes correct

# Page 10: POT plus strided recovery reference
# Expected: both correct and exact VRAM recovery
```

### 7.14 Acceptance gates

1. **Microprobe gate:** All 10 pages render as expected on physical hardware.
   Page 5 (different-stride same list) must demonstrate the actual behavior
   (corruption pattern) to inform the conflict policy.
2. **POT fallback gate:** Conflicting textures fall back to POT without
   visual corruption.
3. **Bilinear edge gate:** No visible artifacts from bilinear overread on
   strided textures.
4. **Upload gate:** Partial updates and streaming locks produce correct results.
5. **Lifecycle gate:** Create/destroy/UpdateTexture/LockTexture/UnlockTexture
   cycles match existing POT behavior.

### 7.15 Rejection/rollback conditions

- **Page 5 shows irrecoverable corruption** that cannot be explained by a
  simple "last stride wins" model → reconsider Policy B (flush/split) or
  Policy C (fixed stride).
- **Bilinear overread artifacts are visible** and cannot be fixed by UV
  clamping → reject bilinear for strided textures or require zero-padded
  POT backing.
- **VRAM savings are negligible** for the target workload → defer or adopt
  Policy F (reject all strided).
- **Stride register reset at scene boundaries** causes visible corruption →
  Policy B required; re-evaluate feasibility.

---

## 8. Validation Matrix

| Test | Strided width | POT fallback | Nearest | Bilinear | Partial update | Streaming lock | Edge probe | Guard color |
|------|---------------|--------------|---------|----------|----------------|----------------|------------|-------------|
| Single 640×480 | ✓ | — | ✓ | ✓ | — | — | — | — |
| Single 384×288 | ✓ | — | ✓ | ✓ | — | — | — | — |
| Alternating 640/512 | ✓ | ✓ (POT) | ✓ | ✓ | — | — | — | — |
| Same-stride pair | ✓ | — | ✓ | ✓ | — | — | — | — |
| Diff-stride same list | — | — | ✓ | ✓ | — | — | — | — |
| Diff-stride scenes | ✓ | ✓ | ✓ | ✓ | — | — | — | — |
| Edge one-pixel | ✓ | — | ✓ | ✓ | — | — | ✓ | ✓ |
| Partial upload | ✓ | — | — | — | ✓ | — | — | — |
| Streaming lock | ✓ | — | — | — | — | ✓ | — | — |
| Lifecycle stress | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |

---

## 9. Acceptance Gates

1. **Physical hardware visual pass:** All 10 microprobe pages render correctly
   on real Dreamcast at 640×480 VGA. Unique raw PPM captures for each page.
2. **Stride conflict behavior documented:** Page 5's actual hardware behavior
   (corruption pattern) is recorded and informs the conflict policy.
3. **Bilinear edge behavior documented:** Page 2 and 9 confirm overread behavior.
4. **POT fallback verified:** Conflicting textures render correctly via POT.
5. **Upload/lock verified:** Pages 8 and streaming test show correct results.
6. **No regression:** Existing POT texture tests still pass.
7. **Serial PASS + hardware visual match:** Serial PASS line present AND
   framebuffer inspection confirms expected output.

---

## 10. Rejection/Rollback Conditions

1. **Page 5 irrecoverable corruption:** If different-stride textures in one
   list produce unrecoverable visual corruption that cannot be addressed by
   flush/split or POT fallback, strided support is rejected.
2. **Bilinear artifacts:** If bilinear overread on strided textures produces
   visible artifacts that UV clamping cannot fix, bilinear is rejected for
   strided textures.
3. **VRAM savings negligible:** If the target workload does not benefit
   significantly from strided storage, defer to later.
4. **Stride register instability:** If the stride register resets unpredictably
   between scenes or frames, the global-stride model is infeasible.

---

## 11. Files a Later Implementation Would Modify

| File | Change type |
|------|-------------|
| `src/render/dreamcast/SDL_render_dreamcast_pvr_data.h` | Add `stride_px`, `active_stride`, `stride_dirty`, `pending_stride` |
| `src/render/dreamcast/SDL_render_dreamcast_pvr_texture.c` | Stride-aware allocation, `pvr_txr_set_stride` calls, fallback logic |
| `src/render/dreamcast/SDL_render_dreamcast_pvr.c` | Stride register management in `DC_PVR_EmitHeader`, conflict detection in `RunCommandQueue`, header-cache invalidation |
| `test/dreamcast/test/pvr_strided_texture_microprobe/` | Standalone 10-page KOS/PVR probe |
| `test/dreamcast/test/pvr_strided_texture_microprobe/Makefile.dc` | Strict standalone build |
| `test/dreamcast/test/pvr_strided_texture_microprobe/README.md` | Test description and hardware results |
| `AGENTS.md` | Phase 6.2 completion entry |

---

## 12. Hardware Answers and Remaining Questions

1. **Resolved — same and different lists:** The final stride applies to all
   strided textures in the frame. The earlier mismatched texture is corrupted
   both within one list and across opaque/transparent list boundaries. The
   matching texture and ordinary POT control remain correct.

2. **Resolved for tested UVs — bilinear sampling:** The tight-row 640×480
   bilinear page shows no visible corruption. A strided row has no implicit
   right-side POT padding, so broader clamp/half-texel cases remain hardening
   rather than evidence of zero-padding behavior.

3. **Resolved — frame transition:** Page 7 changes stride after one completed
   frame and the following frame renders correctly.

4. **Resolved — initialization:** The register reads 0 immediately after
   `pvr_init_defaults()` and every programmed value reads back exactly.

5. **Maximum strided width:** Can stride = 992 (the KOS assertion limit)
   render correctly at 640×480 screen resolution?

6. **VRAM alignment edge cases:** Are there alignment issues when stride ×
   backing_h × 2 does not align to 32 bytes? (Unlikely: stride is multiple
   of 32, height is integer, 32 × integer × 2 is always 32-byte aligned.)

7. **ARGB4444 strided:** Does ARGB4444 (16-bit) work with stride the same
   as RGB565 and ARGB1555, or are there format-specific stride constraints?

---

## 13. VRAM and Bandwidth Equations

### 13.1 Allocation size

```
strided_allocation = stride_px * backing_h * 2
pot_allocation     = backing_w * backing_h * 2

savings = pot_allocation - strided_allocation
        = (backing_w - stride_px) * backing_h * 2
```

Where `backing_w = next_pot(stride_px)` and `stride_px = logical_w` (rounded
up to multiple of 32).

### 13.2 Upload bandwidth (full-backing)

```
strided_upload = stride_px * backing_h * 2 bytes
pot_upload     = backing_w * backing_h * 2 bytes

reduction = 1 - (strided_upload / pot_upload)
          = 1 - (stride_px / backing_w)
```

### 13.3 Representative examples

| Logical | Stride | Backing | Strided (KB) | POT (KB) | Savings |
|---------|--------|---------|-------------|----------|---------|
| 640×480 | 640 | 1024×512 | 614.4 | 1024.0 | 41.4% |
| 512×384 | 512 | 512×512 | 498.0 | 512.0 | 2.7% |
| 384×288 | 384 | 512×512 | 230.4 | 512.0 | 55.0% |
| 320×240 | 320 | 512×256 | 150.0 | 256.0 | 41.4% |
| 960×540 | 960 | 1024×1024 | 1012.5 | 2048.0 | 50.6% |
| 128×96 | 128 | 128×128 | 24.0 | 32.0 | 25.0% |
| 672×448 | 672 | 1024×512 | 604.8 | 1024.0 | 40.9% |

---

## 14. Summary

Phase 6.2 strided NPOT textures are **hardware-validated with a frame-global
stride constraint**. KOS provides a
clear, documented stride mechanism (`pvr_txr_set_stride` +
`PVR_TXRFMT_X32_STRIDE`) that can eliminate POT padding for NPOT 16-bit
textures. The stride register is global per frame, which constrains multi-texture
coexistence to same-stride or scene-boundary switching.

The recommended path is:

1. **Adopt Policy A** (one stride per frame/scene); hardware confirms that
   neither a texture header nor a polygon-list boundary latches the stride.
2. Program the stride before submitting the frame that uses it.
3. **Add Policy D** (same-stride batching) as a performance optimization.
4. **Fall back to POT padding** for textures that cannot use strided storage.
5. **Reject strided `.dt`** until a later increment.
6. Retain the ten passing physical captures as the RGB565 hardware baseline.

The remaining gates are renderer implementation, other 16-bit formats, the
full legal stride range, additional filtering coordinates, other video modes,
and stress/capacity coverage. Existing POT-backed NPOT support remains the safe
fallback for every conflict or unsupported case.
