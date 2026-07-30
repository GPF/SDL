# PVR Strided Texture Test

Focused physical-hardware test for the native PVR fixed-class strided texture
optimization. At 640x480, ordinary RGB565, ARGB1555, and ARGB4444 textures
with a 600-pixel logical width use the 640-pixel stride class. A 300-pixel
RGB565 control remains POT-backed because the fixed class would cost more.

## Build

```bash
# Ordinary build
make -f Makefile.dc clean
make -f Makefile.dc -B SDL2_LIBDIR=/path/to/build-scripts/dcbuild

# Hardware screenshot capture
make -f Makefile.dc -B HARDWARE_SCREENSHOTS=1 \
    SDL2_LIBDIR=/path/to/build-scripts/dcbuild
```

## Test pages

| Page | Description |
|------|-------------|
| 1 | RGB565 static + streaming + POT, including a padded-pitch partial update |
| 2 | ARGB1555 static blend and streaming modulation probes |
| 3 | ARGB4444 static blend and streaming modulation probes |
| 4 | RGB565, ARGB1555, ARGB4444, and POT together in one scene |
| 5 | Update after scene reference and normal texture destruction |

## Hardware capture

When `HARDWARE_SCREENSHOTS=1` is set, the test writes raw 640×480 captures to:

```text
/pc/pvr_strided_textures-page1-rgb565.ppm
/pc/pvr_strided_textures-page2-1555-blend.ppm
/pc/pvr_strided_textures-page2-1555-mod.ppm
/pc/pvr_strided_textures-page3-4444-blend.ppm
/pc/pvr_strided_textures-page3-4444-mod.ppm
/pc/pvr_strided_textures-page4-mixed.ppm
```

The capture helper waits through three vblanks after presentation, following
the established Dreamcast test convention so each path records its own page.

## Physical validation

On 2026-07-30, the screenshot build returned normally on a 640x480 VGA
Dreamcast. All six captures were directly inspected: RGB565 retained its
partial-update marker and POT control; ARGB1555 and ARGB4444 retained their
alpha/blend and modulation patterns; and the mixed page contained all three
strided formats beside the POT control without corruption. The replacement
lifecycle completed normally after an update to a scene-referenced texture.
