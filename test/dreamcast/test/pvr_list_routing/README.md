# Opaque-prefix list-routing focused test

**Guard:** `SDL_DC_PVR_OPAQUE_PREFIX_ROUTING` (CMake option `SDL_DC_PVR_OPAQUE_PREFIX_ROUTING`).
Disabled by default; the ordinary build behaves identically to the pre-routing baseline.

## Purpose

Validate the guarded opaque-prefix feasibility path for the native Dreamcast
PVR renderer. The opaque prefix allows a leading full-target clear and
untextured `SDL_BLENDMODE_NONE` geometry to be routed to `PVR_LIST_OP_POLY`
while the prefix is open; the first drawable that cannot use the opaque list
(per-textured `NONE`, `BLEND`, `ADD`, `MUL`, unsupported clear, or any
target-scene draw) permanently closes the prefix and all remaining draws go
to `PVR_LIST_TR_POLY`.

## Pages

### Page 1: Leading opaque run
- Full-target opaque black clear (routed to OP while prefix open).
- Opaque red rectangle at (50, 50, 200, 150) — routed to OP.
- Translucent green rectangle at (100, 100, 200, 150) with alpha 128 —
  closes prefix, routed to TR.
- Opaque blue rectangle at (150, 150, 200, 150) — after prefix closes,
  must be routed to TR (appears above green).
- Painter-order probe below: red → green blend → blue (top).
- **Expected:** red at bottom, green translucent overlap on red, blue on top
  of green (not directly on red).

### Page 2: Textured-NONE boundary
- Full-target opaque clear.
- Opaque untextured red rect at (20, 20) — OP.
- Textured green quad at (100, 70) — closes prefix, goes to TR.
- Opaque untextured blue rect at (180, 120) — prefix closed, goes to TR.
- **Expected:** textured quad and final opaque blue both in TR, preserving
  their SDL submission order (blue above green).

### Page 3: State-only and lifecycle
- Opaque clear + red rect in full output.
- Viewport/clip/draw-color changes (state-only, no prefix closure).
- Opaque green rect after state changes — still OP (prefix preserved).
- Empty queue + multiple presents.
- **Expected:** state-only commands do not close the prefix; the red full-output
  rectangle and the green viewport-relative rectangle are both visible.

## Screenshot paths

Captured via `/pc` path with three-vblank post-present wait:

- `pvr_list_routing-page1.ppm` — page 1 capture
- `pvr_list_routing-page2.ppm` — page 2 capture
- `pvr_list_routing-page3.ppm` — page 3 state-only capture

## Build

**Feature-on build:**
```sh
cd /home/gpf/code/dreamcast/SDL2
cmake -S . -B /tmp/sdl2-dc-opaque \
  -DCMAKE_TOOLCHAIN_FILE=/opt/toolchains/dc/kos/utils/cmake/kallistios.toolchain.cmake \
  -DSDL_VIDEO_RENDER_DREAMCAST_PVR=ON \
  -DSDL_DC_PVR_OPAQUE_PREFIX_ROUTING=ON \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON \
  -DCMAKE_C_FLAGS="-Wall -Wextra"
cmake --build /tmp/sdl2-dc-opaque -j2
cd test/dreamcast/test/pvr_list_routing
make -B -f Makefile.dc SDL2_LIBDIR=/tmp/sdl2-dc-opaque
```

**Feature-off (ordinary) build:**
```sh
cmake -S . -B /tmp/sdl2-dc-opaque-off \
  -DCMAKE_TOOLCHAIN_FILE=/opt/toolchains/dc/kos/utils/cmake/kallistios.toolchain.cmake \
  -DSDL_VIDEO_RENDER_DREAMCAST_PVR=ON \
  -DSDL_DC_PVR_OPAQUE_PREFIX_ROUTING=OFF \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON \
  -DCMAKE_C_FLAGS="-Wall -Wextra"
cmake --build /tmp/sdl2-dc-opaque-off -j2
cd test/dreamcast/test/pvr_list_routing
make -B -f Makefile.dc SDL2_LIBDIR=/tmp/sdl2-dc-opaque-off
```

## Hardware test plan

1. Upload the feature-on ELF to a real Dreamcast at 640×480 VGA.
2. Capture raw framebuffers for each page via `/pc` path.
3. Verify page 1: red below green, blue on top of green (not on red).
4. Verify page 2: textured quad and blue rect preserve TR order.
5. Verify page 3: state-only commands preserve the prefix and the
   viewport-relative green rectangle is visible.
6. Program must return zero normally to `dc-load-ip`.

## Hardware validation

**Passed on physical Dreamcast hardware (2026-07-30, 640×480 VGA).** The
feature-on test linked explicitly against `/tmp/sdl2-dc-opaque/libSDL2.a`,
completed all three pages with no KOS PVR errors, wrote the captures above,
and returned zero. Direct inspection confirmed the expected painter order on
pages 1 and 2 and both state-only prefix panels on page 3. SHA-256:

- page 1: `202f2a26de7b2e388becfaa787b1175cd8c0e46447178cbcc24fed05bad4b053`
- page 2: `647d0487b2933ebf1a4d6322b70620f12d8fbe1a91dfe03a54bf241ea535ab10`
- page 3: `22e6a4ab0e73c909d6ec1157a2ad6af87d2d89e2c36371b038ff927971a97013`

## Capacity comparison

Both builds produce the same visual output. The guarded build reserves
`PVR_BINSIZE_16` (64 bytes per list entry) for the opaque list at creation
time, reducing translucent capacity by the same amount. A measured
TA/OPB capacity comparison is deferred to a later increment.

---
