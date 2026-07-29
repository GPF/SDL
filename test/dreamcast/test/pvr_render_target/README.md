# Native PVR render-target prototype validation

This test exercises the private `DC_PVR_RENDER_TARGET_PROTOTYPE` build. It is
not an ordinary renderer capability test. The prototype remains guarded until
physical Dreamcast evidence establishes screen and target destination
persistence across PVR scene boundaries.

The ten pages cover API behavior, target rendering and orientation, screen
persistence, two targets, RGB565 blend modes, filtering/modulation, target
reselection persistence, persistence across `SDL_RenderPresent`, feedback and
lifecycle recovery, and NPOT guard geometry.

Build the prototype SDL archive separately, then build the capture ELF:

```sh
make -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/../../../../build-scripts/dcbuild_prototype" \
  HARDWARE_SCREENSHOTS=1
```

Launch from this directory with `kos-tool ... -m .` so the ten unique `/pc`
PPMs are written here. Each page waits three VBLs before capture and is held
for eight seconds. Serial `PASS (API/build)` is not a visual verdict; inspect
every retained hardware PPM. Pages 3, 7, and 8 are the critical persistence
gates, and each must show both labeled-in-serial color markers described by
the program.

## Physical result: rejected prototype

The 2026-07-29 physical Dreamcast run completed all API checks and captured all
ten pages, but failed the semantic gate. Page 3 lost screen marker S1, Page 7
lost target marker T1 after reselection, and Page 8 lost target marker T1 after
`SDL_RenderPresent`. Page 9 lost earlier screen content after later target
transitions. Page 10 also showed periodic holes on the sampled target's extreme
bottom/right edges. See `hardware-20260729-114256.log` and the ten
`pvr_render_target-hardware-pageN.ppm` files.

The prototype must remain guarded and ordinary target capability must remain
disabled. The next step is an exact destination-preservation design plus a
separate diagnosis of the bottom/right sampling holes, followed by another
physical-hardware run.
