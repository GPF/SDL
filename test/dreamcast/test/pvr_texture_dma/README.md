# pvr_texture_dma — DMA texture-upload focused test

Focused test for the native Dreamcast PVR renderer's DMA texture-upload
increment. Validates that `aligned_alloc(32, ...)` staging buffers are
correctly used for DMA when source pointer is 32-byte aligned and upload
size is a multiple of 32, and that SQ fallback works when DMA fails.

## Pages

| Page | Content |
|------|---------|
| 1 | Static RGB565, ARGB1555, and ARGB4444 textures with the 8×8 minimum 128-byte POT backing and a 16×16 512-byte backing |
| 2 | Partial UpdateTexture with padded source pitch; streaming LockTexture/UnlockTexture; update-after-scene-reference replacement |
| 3 | Forced SQ fallback (via hint); texture destruction and VRAM recovery |

## Build

```
cd test/dreamcast/test/pvr_texture_dma
make -B -f Makefile.dc \
  SDL2_LIBDIR=/path/to/SDL2/build-scripts/dcbuild \
  EXTRA_CFLAGS="-DPVR_TEXTURE_HARDWARE_SCREENSHOT"
```

The `EXTRA_CFLAGS` define enables hardware screenshot capture.

## Run on physical Dreamcast

```
kos-tool -t <IP> -x pvr_texture_dma.elf -m .
```

Replace `<IP>` with the Dreamcast's IP address.

## Diagnostic hints

| Hint | Purpose | Default |
|------|---------|---------|
| `DC_PVR_FORCE_SQ_FALLBACK` | Force SQ fallback for validation | 0 (disabled) |
| `DC_PVR_SHOW_UPLOAD_PATH` | Report DMA vs SQ path per frame | 0 (disabled) |

## Hardware validation

**Passed on physical Dreamcast hardware (640×480 VGA, 2026-07-30).** The
test returned zero, wrote all three `/pc/pvr_texture_dma-pageN.ppm` captures,
and returned normally to dc-load-ip. Page 1 shows the three native 16-bit
formats and large RGB565 backing; Page 2 shows the padded partial update,
streaming update, and replacement result; Page 3 shows the forced-SQ fallback
and texture-destruction recovery workload.

The PVR presentation path is three-buffered. The screenshot build waits three
vblanks after each present before calling `vid_screen_shot()`, so each PPM
captures the current page rather than the previous framebuffer.

## Files

- `pvr_texture_dma.c` — test source
- `Makefile.dc` — KOS build rules
- `README.md` — this file
