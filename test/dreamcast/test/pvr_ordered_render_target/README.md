# Dreamcast ordered render-target test

This is the focused physical-hardware gate for the experimental
`DC_PVR_ORDERED_RENDER_TARGETS` build. It validates direct screen rendering,
ordered target transitions, target-pair preservation, rejection recovery,
allocation rollback, and incomplete-frame cleanup. It does not replace or
modify the retained Phase 5 preservation evidence.

The SDL archive must be compiled with:

```text
-DDC_PVR_ORDERED_RENDER_TARGETS
-DDC_PVR_TEXTURE_FAILURE_INJECTION
```

Set `SDL2_LIBDIR` to that archive directory when building:

```sh
make -f Makefile.dc SDL2_LIBDIR=/path/to/ordered/archive clean all
```

For physical captures:

```sh
make -f Makefile.dc SDL2_LIBDIR=/path/to/ordered/archive \
    HARDWARE_SCREENSHOTS=1 clean all
kos-tool -t 192.168.0.128 -x pvr_ordered_render_target.elf -m .
```

The five raw captures are:

```text
/pc/pvr_ordered_render_target-page1.ppm
/pc/pvr_ordered_render_target-page2.ppm
/pc/pvr_ordered_render_target-page3.ppm
/pc/pvr_ordered_render_target-page4.ppm
/pc/pvr_ordered_render_target-page5.ppm
```

Page 1 shows target A and B together, including A's preserved partial second
pass. Page 2 proves a rejected post-clear switch leaves both same-frame screen
panels intact. Page 3 proves presentation reopens target selection. Page 4
proves `SDL_RenderFlush()` is not a frame boundary. Page 5 covers non-batched
rejection plus recovery from target-selected present and selected-target
destruction.

Serial checks additionally cover instance capability, absence of the logical
screen allocation, paired-target payload cost plus KOS allocator metadata,
injected second-allocation rollback, repeated same-target/screen selection,
empty/state-only switches, and renderer destruction with incomplete target and
screen work. KOS retains a small allocator bookkeeping delta after a
malloc/free cycle, so rollback checks reject payload leaks while allowing the
documented 128-byte metadata bound. A serial `PASS` is not sufficient; all
five physical framebuffer captures must be inspected.

## Physical validation

Validated on a real 640x480 VGA Dreamcast. The run reported 23 checks, zero
failures, wrote all five captures, and returned zero normally to `dc-load-ip`.
The focused pages show correct A/B target preservation, clear and geometry
ordering rejection, flush recovery, next-frame target selection, target
selected-present recovery, selected-target destruction, and incomplete-frame
renderer destruction. The ordered renderer reported 5,943,040 bytes available
after creation; the injected failed allocation left only a 24-byte allocator
metadata delta and the 128x128 target pair consumed 65,624 bytes (65,536 bytes
of RGB565 payload plus allocator metadata).
