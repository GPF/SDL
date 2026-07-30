# pvr_partial_upload — Partial VRAM upload focused test

Focused test for the native Dreamcast PVR renderer's partial-VRAM-upload
increment. Validates that partial updates to nontwiddled RGB565/ARGB1555/
ARGB4444 textures write only the expanded 32-byte-aligned byte range to VRAM
for a valid unreferenced POT rectangle, and that full-backing upload is
retained when the allocation is referenced by an active scene.

## POT-only scope

Partial uploads are implemented **only for POT-backed textures**. Strided
textures always fall through to full-backing upload. This is enforced in the
renderer (`DC_PVR_PartialUpload` checks `data->layout == DC_PVR_TEXTURE_LAYOUT_POT`)
and tested on page 3 with a strided texture that asserts correct full-upload
behavior.

## Pages

| Page | Content |
|------|---------|
| 1 | POT RGB565: nonzero-x right-edge, interior patch,
     multi-row bottom-edge, top/bottom row edges, bottom-right corner,
     neighboring-texel preservation, padded source pitch, 32×32 right-edge |
| 2 | POT ARGB1555/ARGB4444 partial updates; partial
     streaming lock/unlock (bottom-edge, multi-row); update-after-scene-
     reference (full-backing fallback) |
| 3 | Strided RGB565 full-upload fallback exercise; diagnostic report
     (SHOW_UPLOAD_PATH=1); VRAM recovery; renderer-continues probe |

## Build

```
cd /home/gpf/code/dreamcast/SDL2
cmake -S . -B /tmp/sdl2-dc-partial \
  -DCMAKE_TOOLCHAIN_FILE=/opt/toolchains/dc/kos/utils/cmake/kallistios.toolchain.cmake \
  -DSDL_VIDEO_RENDER_DREAMCAST_PVR=ON \
  -DSDL_DC_PVR_PARTIAL_UPLOAD=ON \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON \
  -DCMAKE_C_FLAGS="-Wall -Wextra"
cmake --build /tmp/sdl2-dc-partial -j2

cd test/dreamcast/test/pvr_partial_upload
make -B -f Makefile.dc \
  SDL2_LIBDIR=/tmp/sdl2-dc-partial \
  EXTRA_CFLAGS="-DPVR_TEXTURE_HARDWARE_SCREENSHOT"
```

The CMake cache must report both `SDL_VIDEO_RENDER_DREAMCAST_PVR:BOOL=ON`
and `SDL_DC_PVR_PARTIAL_UPLOAD:BOOL=ON`. The explicit archive path is
important: KOS's default addon-library path otherwise takes precedence. The
`EXTRA_CFLAGS` define enables hardware screenshot capture.

## Run on physical Dreamcast

```
kos-tool -t <IP> -x pvr_partial_upload.elf -m .
```

Replace `<IP>` with the Dreamcast's IP address.

## Diagnostic hints

| Hint | Purpose | Default |
|------|---------|---------|
| `DC_PVR_SHOW_UPLOAD_PATH` | Report partial/SQ path per upload | 0 (disabled) |
| `DC_PVR_FORCE_SQ_FALLBACK` | Force SQ path (diagnostic) | 0 (disabled) |

## Partial-upload policy

Partial uploads are attempted when:

1. The texture allocation is **not** referenced by an active submitted scene.
2. The texture layout is **POT** (strided textures always full-upload).
3. The expanded physical range is strictly smaller than the full allocation.

The physical byte range is computed from the staging buffer:

```
byte_start = y * cpu_pitch + x * 2
byte_end   = byte_start + (h - 1) * cpu_pitch + w * 2
```

Expanded to 32-byte boundaries (`exp_start = byte_start & ~31`,
`exp_end = (byte_end + 31) & ~31`), validated against `allocation_size`.
The exact contiguous byte range is copied from `data->pixels + exp_start`
into a 32-byte-aligned temporary buffer. No zero-filling of expanded bytes.
If that range is the complete backing allocation, the renderer keeps the
existing full-upload DMA/SQ chooser instead of making a temporary copy.

Synchronous store-queue `pvr_txr_load` is used. Partial DMA is deferred
until the contiguous span model and a hardware-validated threshold are
established.

## Hardware validation

**Validated on physical Dreamcast hardware (2026-07-30).** The final run at
640x480 VGA returned zero and captured all three pages after a three-vblank
post-present wait. Page 1 shows every RGB565 update and intact neighbors;
page 2 shows the alpha/streaming/replacement panels; page 3 shows the visible
yellow right-edge update on the blue strided panel, plus recovery panels. See
AGENTS.md for capture hashes and the physical-hardware workflow.

## Files

- `pvr_partial_upload.c` — test source
- `Makefile.dc` — KOS build rules
- `README.md` — this file
