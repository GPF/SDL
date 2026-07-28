# Dreamcast PVR texture stress test

This focused Phase 3.3 test exercises the native PVR texture ownership and
rollback contract only through public SDL APIs. It covers 50 static lifecycle
cycles, 20 streaming cycles, 12 simultaneous mixed-lifetime textures,
a texture draw followed by SDL's pre-destruction queue flush and synchronized
cleanup, renderer teardown with five associated live textures, deterministic
failure at each native allocation stage, VRAM exhaustion/recovery, renderer
recreation, and a final three-format visual page.

The streaming phase includes full and partial locks, right/bottom edge locks,
nested-lock rejection, unlock without a successful lock (the backend's
documented no-op), lock-state reset, and four destroy-while-locked cases. The
exhaustion phase uses 256×256 RGB565 textures with equal logical/backing
dimensions, so each allocation represents 131,072 VRAM bytes. A 64-texture
safety cap prevents an infinite loop. Three known-good textures remain
separate and must draw after both exhaustion failures.

## Build

The Makefile links `libSDL2.a` and `libSDL2main.a` by explicit path. Point it
at the requested local unified archive:

```sh
cd test/dreamcast/test/pvr_texture_stress
make -f Makefile.dc -B \
  SDL2_LIBDIR=/tmp/sdl2-dc-phase33-ordinary
```

For deterministic injection, build SDL once with
`-DDC_PVR_TEXTURE_FAILURE_INJECTION`, then build the test with:

```sh
make -f Makefile.dc -B FAILURE_INJECTION=1 \
  SDL2_LIBDIR=/tmp/sdl2-dc-stress-injection
```

The test sets the clearly private, unsupported hints
`SDL_DC_PVR_TEST_ALLOC_STAGE` and `SDL_DC_PVR_TEST_ALLOC_COUNTDOWN` only in
that variant. They select driver-data, CPU-staging, or VRAM allocation and the
Nth matching allocation. The renderer-side state and branches are guarded by
`DC_PVR_TEXTURE_FAILURE_INJECTION` and are absent from ordinary builds.

The hardware capture variant additionally uses:

```sh
make -f Makefile.dc -B FAILURE_INJECTION=1 \
  EXTRA_CFLAGS=-DPVR_TEXTURE_STRESS_HARDWARE_SCREENSHOT \
  SDL2_LIBDIR=/tmp/sdl2-dc-stress-injection
```

It presents the final page, waits through the three-buffer display path, and
writes `/pc/pvr_texture_stress-hardware.ppm`.

## Validated results

Flycast and a physical 640×480 VGA Dreamcast completed the same accounting:

- 180 successful creates, 5 expected failures, and 180 destroys;
- 192 updates and 115 draws;
- 84 locks, 80 unlocks, 4 locked destroys, and 20 unlock-without-lock calls;
- all three injected failures and all three immediate recovery textures;
- 45 initial exhaustion allocations (5,898,240 represented bytes);
- 23 freed and 23 recovered allocations before the second VRAM failure; and
- 3 renderer creates and 2 renderer destroys with one active renderer before
  final cleanup.

Both exhaustion attempts returned `Could not allocate Dreamcast PVR texture
VRAM`. KOS also prints its generic out-of-PVR-memory/heap-probably-corrupted
diagnostic at each intentional exhaustion point; continued allocation,
drawing, exact uniform recovery, and matching hardware/Flycast counts confirm
that these two messages are expected exhaustion diagnostics.

Hardware was run with:

```sh
kos-tool -t 192.168.0.128 \
  -x test/dreamcast/test/pvr_texture_stress/pvr_texture_stress.elf \
  -m .
```

It printed PASS, returned zero, and returned normally to `dc-load-ip`. The
preserved 640×480 capture is
`pvr_texture_stress-hardware.ppm`, SHA-256
`ea26ac35a8664399f01fb8e64bd505de16b013b783a3449717986eca26996c7a`.
Direct inspection matches Flycast: the RGB565, ARGB1555, and ARGB4444 gradient
panels and their red/green/blue borders are intact, with no stale tiles,
corruption, blanking, seams, or missing texture data.

`pvr_mem_available()` is not available in the standard non-`PVR_KM_DBG` KOS
configuration used here. Recovery is therefore demonstrated by uniform
reallocation, not by claiming exact allocator-byte restoration.
