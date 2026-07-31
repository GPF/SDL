# 320x240 smoke test

Focused physical-Dreamcast smoke test for the native PVR renderer at 320x240.
This is not a separate renderer acceptance phase; it verifies that the renderer
adapts to the SDL video driver's configured output size.

## What it covers

- **Page 1:** Full-target clear, opaque (`BLENDMODE_NONE`) and translucent
  (`BLENDMODE_BLEND`) quad overlap.
- **Page 2:** Nearest and bilinear texture filtering, viewport translation,
  and clip rectangle edges at 320x240.
- **Page 3:** Target rendering before the final screen pass, with a 2x2
  colored-corner pattern copied to the screen.

## Build

```sh
make -f Makefile.dc SDL2_LIBDIR=/path/to/sdl2/archive clean all
```

## Run

```sh
make -f Makefile.dc SDL2_LIBDIR=/path/to/sdl2/archive run
```

## Hardware captures

For physical captures:

```sh
make -f Makefile.dc SDL2_LIBDIR=/path/to/sdl2/archive \
    HARDWARE_SCREENSHOTS=1 clean all
kos-tool -t 192.168.0.128 -x pvr_320x240_smoke.elf -m .
```

Captures are written to:

```text
/pc/pvr_320x240-page1.ppm
/pc/pvr_320x240-page2.ppm
/pc/pvr_320x240-page3.ppm
```

## Status

**Physical-hardware validated on a real Dreamcast at 320x240 VGA.**

The run reported 0 checks, 0 failures, and returned zero normally to
`dc-load-ip`. All three pages presented correctly:

- **Page 1:** Full-target clear, opaque (`BLENDMODE_NONE`) and translucent
  (`BLENDMODE_BLEND`) quad overlap rendered as expected.
- **Page 2:** Nearest and bilinear filtering, viewport translation, and clip
  rectangle edges at 320x240 rendered as expected.
- **Page 3:** Target rendering before the final screen pass rendered as
  expected.

The SDL2 video driver correctly switched from 640x480 (initial) to 320x240
and back to 640x480. Captured files and SHA-256 values:

```text
pvr_320x240-page1.ppm  a58810789c9f50a81239b3ad1d7e6bf6a1b84d717ea59703ac40950e83ae8fe2
pvr_320x240-page2.ppm  2bce0970327927647e5d94b34074556eccec2fb058acd7aa653b54bf43bc89f5
pvr_320x240-page3.ppm  8307bb4a21fedbec07a9ccc0fe23fc7c8b2c670de0567a4ba614ed95f621fa00
```
