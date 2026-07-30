# Dreamcast `.dt` texture validation

This focused Phase 6.1 test validates `SDL_LoadDreamcastTexture_RW()` against
both renderers in the unified Dreamcast SDL archive. The assets are generated
at build time with the active KallistiOS `pvrtex` utility; no generated `.dt`
file is checked in.

The matrix contains 64x64 twiddled RGB565, ARGB1555, and ARGB4444 textures,
full-codebook ARGB1555 VQ, reduced-codebook ARGB1555 VQ, and reduced-codebook
ARGB4444 VQ with mipmaps. It also rejects bad magic, unsupported versions and
formats, strided layouts, inconsistent sizes, and truncated payloads. Twenty
load/destroy cycles check recovery; the native run additionally checks exact
VRAM recovery after renderer synchronization. Loader-created textures are
static and immutable.

Build both renderers with strict warnings:

```sh
make -f Makefile.dc -B
```

Build one-shot hardware captures:

```sh
make -f Makefile.dc -B HARDWARE_SCREENSHOTS=1
```

Run each ELF, allowing the program to return normally to dc-load-ip:

```sh
kos-tool -t 192.168.0.128 -x native.elf -m .
kos-tool -t 192.168.0.128 -x opengl.elf -m .
```

The capture paths are `/pc/pvr_dt_texture-phase6_1-native.ppm` and
`/pc/pvr_dt_texture-phase6_1-opengl.ppm`. Each frame has six intact texture
panels, colored format markers, four white framebuffer-corner markers, and a
solid green status bar. The two renderers need not be pixel-identical because
their filtering paths differ, but all panel content, orientation, alpha,
rotation, and borders must agree visually.

Physical 640x480 VGA validation on 2026-07-29 passed for both ELFs: each
reported 15 checks, zero failures, captured one raw framebuffer, returned
zero, and returned control normally. The retained capture SHA-256 values are:

- native: `88b5e0ab714e83f3a146c2571942d198c0949f8e25c998b1bafc07da98744c28`
- OpenGL: `cf4f5ec73b7dfb13b698fcb8f960005165b65271da7e9d8d93c9c67da0fa7497`

Direct inspection confirms all six panels are complete and correspond across
the native PVR and GLdc-backed OpenGL renderers. Strided `.dt`, partial `.dt`,
paletted, bump-map, and YUV formats remain unsupported by this increment.
