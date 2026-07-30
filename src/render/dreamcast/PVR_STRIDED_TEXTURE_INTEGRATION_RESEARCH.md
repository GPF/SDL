# Native PVR strided-texture integration research

> Implementation status (2026-07-30): the fixed-class integration described
> here is complete and hardware-validated for ordinary static and streaming
> RGB565, ARGB1555, and ARGB4444 textures at 640x480 VGA. The original
> RGB565-only scope below is retained as the initial design decision; target,
> `.dt`, twiddled, VQ, mipmapped, and natural per-texture stride layouts remain
> outside the implementation.

## 1. Executive recommendation

**Recommendation: GO, but only for a deliberately narrow first increment.**

Add strided storage for renderer-created, non-target RGB565 static and
streaming textures behind the existing Dreamcast-native build guard. Select an
immutable layout at texture creation. Use one renderer-wide preferred stride
class, derived from the physical output width, so every strided texture that
can save memory uses the same PVR modulo value. Textures that do not fit that
class, or would use more memory than POT storage, remain POT permanently.

This is intentionally more conservative than “the first strided draw chooses
the stride.” `PVR_TEXTURE_MODULO` is global across the active frame; the Phase
6.2 hardware probe showed that changing it in another list corrupts an earlier
strided draw. A per-texture choice made before that conflict is discovered
would require a second copy or a migration. The fixed class avoids both
corruption and duplicate VRAM.

The increment should:

* keep the current SDL command queue and synchronous upload model;
* use `PVR_TXRFMT_X32_STRIDE` and `pvr_txr_set_stride()` only for the selected
  fixed stride class;
* keep POT storage for `.dt`, target, twiddled, VQ, and mipmapped textures;
* preserve the current `logical / POT-header` UV convention;
* use the existing replacement-allocation/deferred-free path without changing
  a texture's layout after creation; and
* reset the frame's stride bookkeeping only after a completed screen-present
  boundary, never at `SDL_RenderFlush` or a PVR list boundary.

No new public SDL capability is needed: striding is an internal optimization
of already-supported RGB565 textures. No runtime hint is required for this
first increment. The implementation remains behind the Dreamcast PVR
compile-time guard until the focused physical test passes.

## 2. Current source architecture

### Texture allocation and metadata

`DC_PVR_TextureData` already retains the fields needed to make layout explicit:
CPU staging (`pixels`), current and replacement VRAM, logical and backing
dimensions, CPU/VRAM pitch, allocation size, PVR format, filter, upload and
scene-reference state, and target-specific paired storage. See
`src/render/dreamcast/SDL_render_dreamcast_pvr_data.h`.

`DC_PVR_CreateTexture()` currently computes

```
backing_w     = NextPowerOfTwo(logical_w)
backing_h     = NextPowerOfTwo(logical_h)
cpu_pitch     = backing_w * 2
vram_pitch    = cpu_pitch
allocation    = vram_pitch * backing_h
```

and zero-allocates the staging buffer before allocating one VRAM block
(`SDL_render_dreamcast_pvr_texture.c:114-243`). `texture->pitch` is the full
backing pitch, not the logical row width. Target textures additionally reserve
the paired `vram_next` allocation; ordinary textures do not.

Dreamcast `.dt` textures take a separate preformatted path
(`DC_PVR_CreateDreamcastTexture`). Their payload layout, VQ offset, and
immutable dimensions are supplied by the file and must not be reinterpreted as
ordinary strided storage in this increment.

### Uploads, locks, and allocation replacement

`DC_PVR_UpdateTexture()` validates the logical rectangle and caller pitch,
copies each source row into staging at `cpu_pitch`, then synchronously uploads
the complete backing allocation. `DC_PVR_LockTexture()` returns a pointer into
the same staging allocation at `y * cpu_pitch + x * 2`; unlock performs the
same complete upload. Consequently a strided implementation can reuse these
paths by changing only the pitch and allocation calculations.

If a texture has already been referenced by an active scene, the current code
allocates a replacement VRAM block, uploads to it, commits the new pointer,
and defers the old block until scene completion. The replacement has exactly
the same `allocation_size`; this is the correct mechanism for strided textures
and must not be changed into an in-place overwrite.

### Command execution and UVs

Queue callbacks copy SDL geometry into renderer-owned vertices. PVR submission
occurs only in `RunCommandQueue`. `SDL_RenderCopy` and `SDL_RenderCopyEx`
therefore arrive through SDL-generated geometry; source rectangles, flips,
rotation, clipping, and viewport translation are already represented in the
queued positions and normalized UVs.

At emission, ordinary textures currently use

```
u *= logical_w / backing_w
v *= logical_h / backing_h
```

while a completed render target has a separate edge-safe path. The texture
header is compiled with `backing_w`, `backing_h`, `pvr_format`, VRAM pointer,
and filter (`SDL_render_dreamcast_pvr.c:477-511`). The header cache is keyed by
texture, blend, texture environment, and filter. A strided layout must add its
layout identity (or the active stride generation) to that cache key and must
set the modulo before the first header/draw that uses the class.

### OpenGL/GLdc reference

The Dreamcast OpenGL renderer has an existing guarded strided path in
`src/render/opengl/SDL_render_gl.c`. `GL_DreamcastCanUseStridedTexture()`
excludes targets, already-POT dimensions, non-32-multiple widths, widths over
992, and YUV formats. The GL path uses normalized logical dimensions for the
strided case and otherwise pads NPOT textures to POT dimensions, storing
`texture_w`/`texture_h` ratios in `texw`/`texh`. Geometry multiplies SDL UVs by
those ratios. This confirms that padding is a storage/layout decision and
does not resample the logical image. The native PVR implementation should
reuse the logical/POT UV convention, but not copy GLdc state or private APIs.

The older framebuffer textured-stride path likewise sets the PVR format flag,
uses a POT header size, and computes `u2 = logical_w / pvr_header_w` and
`v2 = logical_h / pvr_header_h` (`src/video/dreamcast/SDL_dreamcastframebuffer.c`).

## 3. Evaluated layout-selection policies

### A. First strided draw establishes the frame stride

Correct only if every eligible texture was allocated with that same stride or
can be migrated before submission. With permanent per-texture allocations it
allows texture A (for example 320) to be followed by texture B (640), which is
the exact corruption demonstrated by Phase 6.2 pages 5 and 6. A queue
pre-scan could discover the conflict, but it would have to change allocations
or reject a draw after SDL has already accepted it. `SDL_RenderFlush`, ordered
RTT scene transitions, streaming locks, and deferred replacement allocations
make that migration substantially more complex. **Not recommended.**

### B. Renderer-wide stride class selected at creation

This is deterministic, has no duplicate copies, and makes every active
strided draw use one modulo value. It slightly reduces opportunities for
textures whose natural rounded width differs from the class. The output-sized
class is a useful first choice: it is available in the common 320/640 modes,
and textures use it only when it is legal and cheaper than their POT backing.
For output widths above 992, no strided class is selected. **Recommended.**

### C. Queue pre-scan selects the widest required stride

The pre-scan must see all commands, including commands added after a flush,
and must coordinate with target transitions and texture updates. Selecting a
new value after any PVR scene has begun is too late. It also either delays all
texture allocation decisions or requires duplicate/migrated storage. **Defer.**

### D. Texture creation selects a permanent strided or POT layout

Immutability is excellent for locks, updates, scene references, and rollback,
but is unsafe by itself when each texture chooses a different stride. D is
safe when combined with B: creation chooses strided only if the renderer's
single class applies; otherwise it chooses POT. **Recommended as the storage
part of B.**

### E. Recreate or migrate between layouts

Migration requires a second VRAM block, a complete copy/upload, header and
scene lifetime tracking, and a defined result if the second allocation fails.
It is possible using the existing replacement mechanism but defeats the
low-cost goal and creates a failure window after a texture has already been
drawn. **Defer.**

## 4. Recommended first-increment state machine

### Renderer creation

Add guarded fields equivalent to:

```
preferred_stride_px   /* 0 when no legal class is available */
frame_stride_px      /* 0 until a strided draw is submitted */
stride_generation    /* increments at successful frame boundaries */
```

`preferred_stride_px` is derived once from the output width, rounded down to
a legal multiple of 32 and capped at 992. It is not changed by texture
creation, flushing, target selection, or a viewport change.

### Texture creation

For RGB565 static/streaming textures only, select strided storage when:

```
preferred_stride_px != 0
logical_w <= preferred_stride_px
preferred_stride_px < NextPowerOfTwo(logical_w)
logical_h <= 1024
```

and the resulting allocation is no larger than POT storage. Otherwise select
POT storage. Record `layout = STRIDED`, `vram_pitch = preferred_stride_px`,
and `pvr_format |= PVR_TXRFMT_X32_STRIDE`; POT textures retain the current
format and pitch. The selected layout never changes for the texture's
lifetime.

This intentionally means a 300-pixel texture on a 640-wide renderer may stay
POT: a 640-pixel pitch would be larger than its 512-pixel POT row. A later
increment can add a creation-time preferred-stride hint or a safe migration
policy if that trade-off is insufficient.

### Queue and scene execution

Before emitting the first strided header/draw in a frame, assert that
`frame_stride_px` is zero or equals `preferred_stride_px`. Set
`pvr_txr_set_stride(preferred_stride_px)` once and record the frame value. POT
draws do not modify this state and remain valid alongside strided draws.

The value is frame-global, not list-global. A list finish, scene finish used
for an internal ordered-RTT transition, or `SDL_RenderFlush` does not reset
it. State-only commands and empty queues do not select it.

After a successful screen-selected `SDL_RenderPresent`, reset
`frame_stride_px` and increment `stride_generation`. Reset on renderer
destruction and on a failed scene startup as part of existing renderer error
cleanup. Do not reset on a target-selected present; ordered RTT must recover by
selecting the screen and presenting it normally.

The first increment keeps render-target textures POT. Thus an ordered RTT
target scene cannot introduce a second stride class. If strided target storage
is considered later, it must obey the same frame-global class and scene
generation rather than assuming a list boundary isolates the register.

## 5. Exact backing and UV calculations

### Backing dimensions and pitch

For a strided RGB565 texture:

```
stride_px       = preferred_stride_px       /* one class, multiple of 32 */
backing_w       = NextPowerOfTwo(stride_px)
backing_h       = NextPowerOfTwo(logical_h)
cpu_pitch       = stride_px * 2 bytes
vram_pitch      = stride_px * 2 bytes
allocation_size = stride_px * backing_h * 2 bytes
```

The image is copied into the first `logical_w` texels of each row. Columns
`logical_w .. stride_px-1` and rows `logical_h .. backing_h-1` are padding and
remain zero. There is no resampling. `pvr_poly_cxt_txr()` still receives
power-of-two header dimensions; the X32 stride flag and modulo register define
the actual row pitch.

For POT storage, the existing formulas remain `pitch = backing_w * 2` and
`allocation = backing_w * backing_h * 2`.

KOS limits make 1024 the practical header dimension and 992 the largest legal
strided row pitch. Logical widths that round above 992, and dimensions whose
POT backing exceeds the existing 1024 limit, use the current POT/error path.

Concrete natural-width calculations (useful for the later flexible-stride
increment) are:

| Logical | Natural legal stride | POT header | Natural strided bytes | POT bytes |
|---|---:|---:|---:|---:|
| 300x180 | 320 | 512x256 | 163,840 | 262,144 |
| 321x241 | 352 | 512x256 | 180,224 | 262,144 |
| 640x480 | 640 | 1024x512 | 655,360 | 1,048,576 |
| 641x480 | 672 | 1024x512 | 688,128 | 1,048,576 |
| 992x512 | 992 | 1024x512 | 1,015,808 | 1,048,576 |
| 993x512 | invalid (>992) | 1024x512 | POT fallback | 1,048,576 |

The first fixed-class policy may choose POT for some rows in this table when
the renderer-wide class is wider than the natural pitch. The table describes
the layout arithmetic, not a promise that every output mode selects every
natural stride.

### UV mapping

SDL supplies normalized UVs for full copies, source rectangles, rotations,
flips, and clipped triangles. The native queue preserves those values and
the emitter scales them. The denominator is the PVR **header dimension**, not
the byte pitch:

```
u_max = logical_w / backing_w
v_max = logical_h / backing_h
emitted_u = SDL_u * u_max
emitted_v = SDL_v * v_max
```

This follows the existing native path, the OpenGL Dreamcast path, the
framebuffer textured-stride path, and the KOS strided example. `stride_px`
controls row addressing; it is not substituted into normalized texture
coordinates. Source rectangles, flips, and rotations require no special
strided branch because SDL has already transformed their UVs. CPU clipping
continues to interpolate those UVs with position and color.

Do not import the completed-RTT edge-safe `(logical-1)/backing` formula into
ordinary textures without a test. For the first increment retain the current
`logical/backing` behavior for nearest and bilinear. Zero-filled padding makes
bilinear overread deterministic, and the Phase 6.2 page 2 is specifically an
observational tight-row test. A follow-up may add a filter-specific texel-edge
clamp only if a physical SDL edge comparison proves it necessary.

## 6. Conflict and fallback behavior

There is no late “fallback” from strided to POT in this increment. Layout is
chosen once, before any upload or draw, and is visible in texture driver data.
Therefore a conflicting width cannot corrupt an existing strided draw: it was
already allocated POT if it does not belong to the renderer-wide class.

POT textures ignore the modulo register and may appear in the same scene as
the active strided class. A strided texture is submitted only when
`frame_stride_px == preferred_stride_px`; a mismatch is an internal invariant
failure, not a draw-time conversion opportunity. Never silently draw a
texture with the wrong modulo and never retain a hidden second copy merely to
make a late fallback possible.

## 7. Texture-category scope

| Category | First increment |
|---|---|
| Static RGB565 / ARGB1555 / ARGB4444 | Strided when fixed class is legal and cheaper; otherwise POT |
| Streaming RGB565 / ARGB1555 / ARGB4444 | Same immutable choice; locks expose the selected pitch |
| SDL render-target textures | POT paired RTT allocation only |
| Direct `.dt` | Existing preformatted POT/twiddled/VQ path only |
| Twiddled / VQ | POT/format-specific path; incompatible with X32 stride |
| Mipmapped | POT/twiddled path; not strided |
| Palettes, bump maps, YUV | Existing unsupported/deferred paths |

This boundary deliberately excludes DMA, partial `.dt`, palettes, bump maps,
YUV, list routing, batching, and render-target promotion.

## 8. Update, lock, and filtering implications

* Full updates copy logical rows into the selected staging pitch and upload
  the full `stride_px * backing_h * 2` allocation.
* Partial updates honor the caller's source pitch while advancing the
  destination by `cpu_pitch`; untouched columns and rows remain zero.
* Streaming locks return `pixels + y * cpu_pitch + x * 2` and `pitch =
  cpu_pitch`. A partial lock must not expose padding as logical pixels.
* Updates after scene reference use the existing same-layout replacement
  allocation. If allocation fails, the old VRAM pointer and layout remain
  unchanged and no deferred entry is installed.
* Staging is zero-initialized, including row and bottom padding. This is
  required for deterministic bilinear overread and prevents stale VRAM data.
* Nearest filtering must show the logical last column/row and no POT leakage.
  Bilinear filtering must be measured, not inferred: page 2 of the Phase 6.2
  probe uses a tight row specifically because a natural stride may have no
  right-side padding.

## 9. Expected files and functions changed

Implementation should be limited to the guarded native PVR files:

1. `SDL_render_dreamcast_pvr_data.h`: layout enum, selected pitch, preferred
   stride, and frame stride-generation fields.
2. `SDL_render_dreamcast_pvr_texture.c`: fixed-class eligibility and backing
   calculations in `DC_PVR_CreateTexture`; preserve pitch/allocation through
   update, lock, unlock, replacement, and destruction.
3. `SDL_render_dreamcast_pvr.c`: initialize/reset frame stride, set the modulo
   before the first strided submission, include layout in header-cache identity,
   and reset only after successful screen presentation.
4. `SDL_render_dreamcast_pvr_geometry.c`: only if the existing UV helper needs
   to read explicit per-texture scales; do not change SDL-generated UVs.
5. A new focused Dreamcast-native test directory and its Makefile, using the
   established `/pc` PPM capture convention.
6. A short Dreamcast renderer README section if the project documentation
   needs to mention the experimental build guard.

No SDL-wide queue behavior, OpenGL source, KOS headers, GLdc code, `.dt`
loader, or unrelated platform file should change.

## 10. Hardware test matrix

The test must compare raw hardware captures, not just serial PASS output.
Use ordinary and screenshot-enabled strict builds, and let `kos-tool` return
normally. At 640x480 VGA, include at least:

1. POT RGB565 baseline and a 640x480 fixed-class strided texture; verify
   identical logical pixels and the expected 655,360-byte versus 1,048,576-byte
   allocation.
2. Two fixed-class strided RGB565 textures with distinct row markers; both
   remain correct.
3. One strided plus one POT texture in one scene; POT remains intact.
4. Widths 300, 321, 641, 992, and 993; print layout, pitch, header, and
   allocation, and verify the documented POT fallbacks.
5. Consecutive frames with strided content followed by POT-only content and
   vice versa; modulo is reset/selected only at the frame boundary.
6. Nearest full copy, one-pixel right/bottom source rectangles, rotated and
   flipped copies, and viewport/clip crossings.
7. Bilinear right/bottom edge guards compared against the existing POT path;
   record whether any darkening or row-wrap appears before proposing a UV
   correction.
8. Full and partial updates with padded source pitch, followed by rendering
   while the texture is scene-referenced.
9. Streaming full and partial locks; inspect the reported pitch and updated
   region.
10. Ordered RTT workload: target A -> target B -> screen, then strided screen
    textures. Confirm target scenes remain POT and no old stride corrupts the
    final screen.
11. VRAM exhaustion/failure injection: a failed allocation leaves no second
    copy, no changed driver pointer, and exact post-destruction VRAM recovery.
12. Destroy/recreate cycles and renderer destruction with queued texture work.

The test should save one PPM for each distinct page and print the selected
layout and `pvr_txr_get_stride()` readback. A serial PASS without inspecting
the PPMs is not acceptance.

## 11. Risks and stop conditions

Stop implementation if any of the following cannot be demonstrated on physical
hardware:

* a POT texture is corrupted while a fixed-class strided texture is visible;
* two same-class strided textures disagree in row addressing;
* a stride write occurs after an earlier strided draw in the same frame;
* nearest/source-rectangle/flip/rotation/clipping UVs leak into padding;
* bilinear edges differ from the expected SDL/POT reference without a clearly
  justified correction;
* an update or lock replacement changes the visible texture before the old
  scene retires;
* ordered RTT introduces a second active stride or fails to restore normal
  screen rendering;
* allocation failure leaks VRAM or changes the old texture; or
* the ordinary non-strided build changes behavior or symbols.

The fixed-class policy sacrifices some potential savings. Do not expand it to
per-texture natural strides, mid-frame migration, DMA, or strided RTT until a
separate design resolves those lifetime and frame-global conflicts.

## 12. GO/NO-GO verdict

**GO completed:** the guarded fixed-class RGB565, ARGB1555, and ARGB4444
increment is source-compatible with the texture/update architecture, agrees
with the Phase 6.2 physical evidence, keeps POT fallback automatic, and avoids
duplicate VRAM. **NO-GO** for per-texture natural stride selection or changing
the modulo at list boundaries; those policies remain unsafe without migration
or a much larger queue and allocation design.
