# Native PVR render-target preservation and edge microprobe

This records a private hardware experiment run against a separate archive built
with the former `DC_PVR_RENDER_TARGET_PROTOTYPE` implementation. That prototype
has been retired: the current native renderer enables ordered target textures
by default. These results do not validate the current implementation; use
`../pvr_ordered_render_target` for its acceptance test.

Pages 1-4 measure VRAM and completed-render time while preserving screen-sized,
128x128, and 512x512 RGB565 destinations with a PVR current-to-next seed draw.
Pages 5-8 compare standard SDL copy UVs against explicit half-texel-center UVs
for 128x128, 127x93, 160x120, and 256x128 targets under nearest and linear
filtering.

The historical run used a locally retained prototype archive:

```sh
make -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/../../../../build-scripts/dcbuild_prototype" \
  HARDWARE_SCREENSHOTS=1
```

Launch from this directory with `kos-tool ... -m .`. The capture build writes
unique raw PPM files through `/pc`. Physical Dreamcast captures and
serial timing are authoritative; do not substitute emulator output. The old
prototype archive, its source revision, logs, and captures are local research
artifacts and are not produced by the current build scripts or included here.

## Physical result

The corrected preservation run passed on real Dreamcast hardware. Explicit
current-to-next seed draws preserved both old and new markers for a 640x480
logical screen, 128x128 and 512x512 targets, and a 160x120 target across
target-selected `SDL_RenderPresent`.

Initial available PVR texture memory was 5,943,040 bytes. A pair of POT-backed
640x480 logical-screen targets consumed about 2 MiB and left 3,845,800 bytes.
Completed seed scenes averaged 6.680 ms at 640x480, 3.351 ms at 128x128,
5.988 ms at 512x512, and 3.411 ms at 160x120.

The edge controls proved that RTT writes and KOS clipping are correct. Current
and half-texel 1:1 captures are byte-identical. For enlarged targets, current
UV maxima reproduce periodic bottom/right contamination and half-texel maxima
are insufficient. Mapping the maximum to the last PVR texel center,
`(logical - 1) / backing`, produces complete nearest and linear borders for
both 127x93 and 256x128 physical captures.

Authoritative logs are `hardware-20260729-132149.log`,
`hardware-20260729-133148.log`, and `hardware-20260729-133408.log`. Use the
`final-page1..8`, `1to1-page9..10`, and `max-inset-page11..12` PPMs as the
visual record. Development logs and the older `hardware-pageN` captures are
not validation evidence.
