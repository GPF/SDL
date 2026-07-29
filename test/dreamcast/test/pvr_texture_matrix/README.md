# Native PVR texture dimension and logical-edge matrix

This focused public-SDL test validates the existing synchronous full-backing
texture upload path. It does not inspect backend-private state and does not
implement partial VRAM uploads, DMA, custom store-queue transfers, or
read-modify-write logic.

## Dimension and backing matrix

Each case is created, initialized, checked, and destroyed before the next case
so the test does not become a VRAM-exhaustion test. Expected backing dimensions
are independently rounded up to a power of two with a minimum of 8. Expected
CPU and VRAM pitch is `backing_w * 2`; expected allocation size is
`pitch * backing_h`.

| Logical size | Expected backing | Format | Access |
|---|---|---|---|
| 1x513 | 8x1024 | RGB565 | streaming |
| 2x257 | 8x512 | ARGB1555 | static |
| 3x129 | 8x256 | ARGB4444 | streaming |
| 7x65 | 8x128 | RGB565 | static |
| 8x8 | 8x8 | ARGB1555 | streaming |
| 9x63 | 16x64 | ARGB4444 | static |
| 15x15 | 16x16 | RGB565 | streaming |
| 16x16 | 16x16 | ARGB1555 | static |
| 17x31 | 32x32 | ARGB4444 | streaming |
| 31x17 | 32x32 | RGB565 | static |
| 32x32 | 32x32 | ARGB1555 | streaming |
| 33x15 | 64x16 | ARGB4444 | static |
| 63x9 | 64x16 | RGB565 | streaming |
| 64x64 | 64x64 | ARGB1555 | static |
| 65x7 | 128x8 | ARGB4444 | streaming |
| 127x3 | 128x8 | RGB565 | static |
| 128x128 | 128x128 | ARGB1555 | streaming |
| 129x2 | 256x8 | ARGB4444 | static |
| 255x255 | 256x256 | RGB565 | streaming |
| 256x256 | 256x256 | ARGB1555 | static |
| 257x127 | 512x128 | ARGB4444 | streaming |
| 319x240 | 512x256 | RGB565 | static |
| 320x240 | 512x256 | ARGB1555 | streaming |
| 321x241 | 512x256 | ARGB4444 | static |
| 511x129 | 512x256 | RGB565 | streaming |
| 512x128 | 512x128 | ARGB1555 | static |
| 513x65 | 1024x128 | ARGB4444 | streaming |
| 513x257 | 1024x512 | RGB565 | static |

This covers every requested representative axis value, exact POT squares,
NPOT values immediately below and above transitions, 1-pixel axes, narrow/tall
and wide/short textures, 320x240, 321x241, and one sequential 1 MiB backing
allocation without exhausting VRAM. Every format occurs at multiple backing
transitions and with both static and streaming access across the complete test.

Static initialization uses tight, larger, and odd-extra-byte source pitches;
padding bytes are filled with a sentinel. Streaming initialization verifies
that the returned pitch is exactly the expected backing pitch. Visible edge
patterns are the public-API proxy for the private backing calculations.

## Edge operations and API contract

The static probe performs full, four-corner 1x1, top/bottom row, left/right
column, interior, right-edge, bottom-edge, bottom-right, overlapping, and final
full-logical updates. The streaming probe performs corresponding partial locks,
repeated bottom-right locks, and a final full lock. Writes stay inside each
requested logical rectangle. Untouched staging contents therefore exercise the
full-backing upload's preservation behavior.

Malformed public calls distinguish SDL-core behavior from backend behavior:

- fully outside and zero-area updates are successful SDL-core no-ops;
- updates extending past right/bottom or beginning at negative x are clipped
  by SDL core before the backend callback;
- null pixels are rejected by SDL core;
- too-small update pitch is rejected by the backend;
- zero-area, outside, extended, and negative streaming locks are rejected by
  the backend because SDL passes lock rectangles through unchanged;
- dimensions above 1024 are rejected by SDL core from renderer info; and
- target access reaches and is rejected by the backend.

The test checks call status, exact streaming backing pitch, page completion,
live-texture accounting, capture accounting, and cleanup. It does not read back
framebuffer pixels, so the `PASS` line proves API/control-flow checks only.

## Visual pages

1. Small square dimensions: 1, 7, 8, 9, 15, 16, 17, 31, 32, and 33.
2. Asymmetric/practical cases: 1x129, 129x1, 13x9, 17x33, 63x65,
   320x240, and 321x241.
3. Static and streaming edge-operation results plus update/draw ordering,
   texture-A update while texture B is queued, repeated updates, and nearest
   then bilinear draws.
4. RGB565, ARGB1555, and ARGB4444 in nearest and bilinear rows over alpha
   checkerboards where applicable.

Magenta guards surround each probe. Red, green, blue, and yellow identify the
logical corners; white borders, cyan/magenta center markers, and contrasting
interiors expose missing edges, swapped axes, pitch errors, or POT-padding
sampling. Normal bilinear interpolation inside the logical image is expected.

## Build and capture

Build against an explicit local SDL archive:

```sh
make -f Makefile.dc \
  SDL2_INCLUDEDIR=/path/to/SDL2/build-scripts/dcbuild/include-config-/SDL2 \
  SDL2_LIBDIR=/path/to/SDL2/build-scripts/dcbuild
```

The linker names `libSDL2.a` and `libSDL2main.a` explicitly because the KOS
wrapper otherwise prioritizes installed addon libraries.

For physical-hardware capture:

```sh
make -f Makefile.dc clean
make -f Makefile.dc HARDWARE_SCREENSHOT=1
```

That narrowly scoped build waits for three vblanks after each presentation and
attempts all four captures even after a failure:

```text
/pc/pvr_texture_matrix-hardware-page1.ppm
/pc/pvr_texture_matrix-hardware-page2.ppm
/pc/pvr_texture_matrix-hardware-page3.ppm
/pc/pvr_texture_matrix-hardware-page4.ppm
```

Capture failure suppresses PASS and produces a nonzero exit. Rebuild without
`HARDWARE_SCREENSHOT=1` after hardware validation.

## Validation status

Validated on 2026-07-28 in Flycast and on a physical 640x480 VGA Dreamcast.
Both runs completed 28 dimension cases, 57 successful creates, 47 updates, 41
locks, 16 invalid-contract checks, all four pages, and all three formats with
both access modes. Both printed PASS. Physical hardware returned zero normally
to `dc-load-ip`; no SDL, PVR, KOS, or allocator warning occurred in the final
runs.

Direct inspection of Flycast and the hardware captures found intact corner and
one-pixel probes, complete right/bottom edges, no POT-padding or pitch-sentinel
leakage, matching static and streaming patterns, correct repeated updates and
partial locks, pixel-sharp nearest filtering, smooth bilinear filtering, and
the intended ARGB1555 cutouts and ARGB4444 alpha ramp.

The ordering probe exposed one real renderer defect: SDL flushed submitted
geometry before an update, but the upload overwrote the same VRAM allocation
before the active PVR scene consumed it. The renderer now marks allocations
referenced by submitted texture headers. Updating or locking such a texture
uses a synchronous **full-backing** replacement allocation, invalidates the
header cache, and defers the old allocation until presentation synchronization.
Unreferenced textures continue uploading in place. No partial upload, DMA,
custom store-queue row transfer, or read-modify-write path was added.

An initial hardware run also found a test-only unaligned write in the odd-pitch
source generator. Source pixels are now stored with `SDL_memcpy`, preserving
the intended odd source-pitch coverage on SH-4.

The lasting 640x480 raw PPM captures and SHA-256 values are:

- `pvr_texture_matrix-hardware-page1.ppm`:
  `2328280da0b43144d7bef900d3cdaca3cbe9ead4f48ccd3f9127bae3d5541315`
- `pvr_texture_matrix-hardware-page2.ppm`:
  `77723298ec9452a97fd4a165becf6ef2aa7f135bb62267b0ba6cfc3fb85caf01`
- `pvr_texture_matrix-hardware-page3.ppm`:
  `e5984402f1b9ba3a9414a5580ea4bfbdb62cfb3a5ebfba707d327c424ef4dec1`
- `pvr_texture_matrix-hardware-page4.ppm`:
  `623655c59c2f5d23e32bf096c4506bbeb11962444e2bbf7b392900ba2ae73ea9`

The ordinary ELF was rebuilt afterward. It contains no `vid_screen_shot`
reference, hardware capture path, or screenshot define. The synchronous
full-POT-backing upload remains intentional. Remaining texture hardening is
non-VGA video modes and broader capacity/performance profiling.
