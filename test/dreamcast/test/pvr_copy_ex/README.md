# Dreamcast native PVR copy/copy-ex validation

This focused test exercises the public `SDL_RenderCopy`, `SDL_RenderCopyF`,
`SDL_RenderCopyEx`, and `SDL_RenderCopyExF` paths through the native Dreamcast
PVR renderer. It uses an asymmetric 13x9 RGB565 logical texture backed by the
renderer with a larger power-of-two allocation.

Build against an explicit local SDL archive:

```sh
make -C test/dreamcast/test/pvr_copy_ex -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild"
```

The ordinary build contains three timed pages and no `/pc` access. A physical
hardware capture build may be made with:

```sh
make -C test/dreamcast/test/pvr_copy_ex -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild" \
  EXTRA_CFLAGS=-DPVR_COPY_EX_HARDWARE_SCREENSHOT
```

That define captures the three completed pages to distinct
`/pc/pvr_copy_ex-hardware-pageN.ppm` paths after presentation and a vblank.
It reports each write failure, still attempts every later page, and suppresses
PASS if any capture failed. It is never enabled by the normal build.

For deterministic Flycast inspection of one timed page, pass
`EXTRA_CFLAGS=-DPVR_COPY_EX_VISUAL_PAGE=N`, where `N` is 1, 2, or 3. The
ordinary and hardware-capture builds leave this undefined and execute all
three pages in one run.

## Visual pages

1. Full and partial source rectangles, one-pixel strips, logical-edge
   rectangles, 1:1/enlarged/reduced/nonuniform scaling, and nearest versus
   bilinear filtering.
2. Horizontal, vertical, and combined flips; 0/90/180/270/45-degree rotation;
   default and explicit centers; and source rectangles combined with rotation
   and flips.
3. Matching left/right panels for zero and nonzero viewports, active clipping,
   rotated/flipped copies, texture color/alpha modulation, NONE/BLEND painter
   order, and boundary guards.

The four texture corners are red, green, blue, and yellow, and a bright arrow
points right through a checker/grid interior. Magenta guards expose padding or
boundary leakage. API checks also exercise unindexed geometry and 8-, 16-, and
32-bit indices, and require invalid index sizes, out-of-range indices, and
non-finite positions/UVs to fail. Follow-up source-validation checks require
non-finite rectangle width and height and null indices with a positive index
count to fail, while finite empty rectangles and valid unindexed geometry must
still succeed. NaN and Infinity are constructed from IEEE-754 bit patterns so
the checks remain meaningful under KOS `-ffast-math` builds.

## Validation status

Validated on 2026-07-28 with a fresh unified Dreamcast SDL build containing
both the GLdc-backed OpenGL renderer and the native PVR renderer. This test and
the existing native-PVR tests build with `-Wall -Wextra -Werror` against the
explicit local archives. A separate PVR-disabled build contains no native-PVR
objects and no `DC_RenderDriver`.

Flycast reached
`PASS (API/runtime): pvr_copy_ex completed page range 1..3`. Direct inspection
of all three pages confirmed correct source boundaries and corner orientation;
horizontal, vertical, and combined flips; clockwise positive SDL rotation;
default and explicit centers; nearest and bilinear filtering; NPOT UV scaling
without POT-padding leakage; matching zero/nonzero viewport panels; exact clip
guards; modulation, alpha, and painter order; and no seams or missing
triangles. The disposable Flycast captures were
`/tmp/pvr-copy-ex-flycast-page1.png` through `page3.png`.

An independent Phase 3.2 review confirmed that `SDL_RenderFillRectsF` can pass
non-finite dimensions to the backend and that `SDL_RenderGeometryRaw` can pass
a null index pointer with a positive index count. The backend now rejects both
at queue time. The unified archive was rebuilt, the changed renderer source and
this test passed strict-warning compilation, and Flycast repeated the
API/runtime PASS with the expected rejection messages. Because valid command
storage and rendering are unchanged, no new physical-hardware run was required;
the existing framebuffer PPMs and hashes below remain unchanged.

The capture build was uploaded to a physical 640x480 VGA Dreamcast through the
Broadband Adapter from the repository root:

```sh
kos-tool -t 192.168.0.128 \
  -x test/dreamcast/test/pvr_copy_ex/pvr_copy_ex.elf \
  -m .
```

KOS wrote all three images, the test printed its PASS line, returned zero, and
control returned normally to `dc-load-ip`. The lasting raw framebuffer
references are:

- `pvr_copy_ex-hardware-page1.ppm` — 640x480 — SHA-256
  `120aa843cd10c259f5c67ed852fab6eee08fd22a882bbba09edf1f9da3f56c93`
- `pvr_copy_ex-hardware-page2.ppm` — 640x480 — SHA-256
  `c4580265159a1dc529fd45427a98d87a007ed6569530f37161cc229308b9d528`
- `pvr_copy_ex-hardware-page3.ppm` — 640x480 — SHA-256
  `787660b80b92933242117868f327626a532639c40a9909cd69e812e6665dcb66`

Direct inspection matches Flycast: page 1 retains every logical-edge and
one-pixel source selection, the asymmetric corner/arrow orientation, sharp
nearest and smooth bilinear results, and clean NPOT backing edges. Page 2 has
the expected flip combinations, 90/180/270-degree orientations, centered
45-degree rotation, corner-pivot motion, and cropped rotation/flip results.
Page 3 has identical viewport panels, exact clipped guards, correct modulation
and alpha, and intact blue/texture/green painter order. No page shows padding
leakage, boundary leakage, cracks, seams, or missing triangles.

After capture, the ordinary ELF was rebuilt without
`PVR_COPY_EX_HARDWARE_SCREENSHOT`. Its symbol and string tables contain no
`vid_screen_shot` reference and no `/pc/pvr_copy_ex-hardware` path.
