# Native PVR Phase 3 texture validation

This focused test validates the first native Dreamcast PVR texture increment.
It covers RGB565, ARGB1555, and ARGB4444; static and streaming access; full and
partial updates and locks; nontrivial and invalid source pitch; NPOT logical
dimensions with POT backing; nearest and bilinear filtering; color and alpha
modulation; RGB565 sampled-alpha suppression; rejected target and unsupported
filter modes; and `NONE`/`BLEND` painter order.

From the repository root, the ordinary strict-warning build uses the explicit
local SDL archive:

```sh
make -C test/dreamcast/test/pvr_textures -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild"
```

It compiles the test with `-Wall -Wextra -Werror`. The default build does not
define `PVR_TEXTURE_HARDWARE_SCREENSHOT` and does not access `/pc`, so normal
Flycast execution is unaffected.

## Physical-hardware result (2026-07-27)

Passed on a real Dreamcast at 640x480 VGA. The ELF was uploaded and executed
through the Broadband Adapter with:

```sh
kos-tool -t 192.168.0.128 -x pvr_textures.elf -m .
```

For this run only, the test was built with
`EXTRA_CFLAGS=-DPVR_TEXTURE_HARDWARE_SCREENSHOT`. After the final presented
frame and a vblank wait, it called:

```c
vid_screen_shot("/pc/pvr_textures-hardware.ppm");
```

The `-m .` mapping returned the raw framebuffer to:

```text
test/dreamcast/test/pvr_textures/pvr_textures-hardware.ppm
```

SHA-256:

```text
96ac33fc4320a1f4e17556615f026848bc34fd589c9b587bb64126e5d1ed2c94
```

KOS reported that the screenshot was written successfully. The test reached
its PASS line, returned zero, and emitted no SDL, PVR, KOS, dc-load, or transfer
errors.

Direct inspection of the 640x480 hardware framebuffer confirms:

- the RGB565 NPOT border and partial update are correct;
- no POT-padding pixels leak across logical edges;
- the streaming checker and partial yellow streaming-lock update are correct;
- ARGB1555 transparent cutouts expose the expected background;
- the ARGB4444 alpha gradient is correct;
- nearest filtering is sharp and bilinear filtering is smooth;
- color and alpha modulation are correct;
- RGB565 red texels remain opaque; and
- blue/translucent-texture/green painter order is correct.

The hardware framebuffer closely matches the Flycast reference. After capture,
the ordinary ELF was rebuilt without `PVR_TEXTURE_HARDWARE_SCREENSHOT`.

## Remaining coverage

Focused rotation/flip and source-rectangle cases, a wider dimension and
edge-update matrix, repeated create/destroy/update/lock stress, and
VRAM-exhaustion rollback remain Phase 3 validation work. Other video modes and
performance, TA, and OPB capacity measurements remain Phase 7 hardening.

## Review findings (2026-07-28)

An independent code review of Phase 3.1 identified one HIGH defect in
`src/render/SDL_render.c` (`IsSupportedBlendMode` contract change) and two
LOW items (`DC_PVR_SetTextureScaleMode` unreachable error path, raw renderer-name
literal in `SDL_SetTextureScaleMode` guard). The HIGH defect was resolved by
identity-gating the required-mode callback lookup on `DC_PVR_RENDERER_NAME`
instead of only restricting the build via `#if SDL_VIDEO_RENDER_DREAMCAST_PVR`.
Two MEDIUM items (full-backing upload on every partial update, OpenGL diff
separation) are tracked for later phases. The test itself passes Flycast and
hardware validation with no defects. Detailed findings are recorded in
AGENTS.md and PVR_TEXTURE_RESEARCH.md.
