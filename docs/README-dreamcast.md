# Dreamcast SDL2 notes

## Native PVR renderer

Select `SDL_DC_DIRECT_VIDEO` before creating the window to use the native PVR
renderer. The renderer is independent of the GLdc-backed OpenGL renderer.

## Video mode selection

The SDL Dreamcast video driver selects 320x240, 640x480, PAL, and NTSC modes
via `SDL_HINT_DC_VIDEO_MODE` before window creation. The PVR renderer queries
the configured output size at creation time and adapts to it. The renderer
requires a positive width divisible by 32.

320x240 is a supported mode but requires only a smoke test — it is not a
separate renderer acceptance phase. Wider video-mode coverage and TA/OPB
capacity/stress remain Phase 7 hardening.

## Render-target ordering

The Dreamcast PVR renderer requires all texture-target rendering to be
completed before the final screen pass. After `SDL_RenderClear()` or the first
screen draw call executes, `SDL_SetRenderTarget(renderer, texture)` fails until
`SDL_RenderPresent()` completes the screen frame.

Correct order:

```c
SDL_SetRenderTarget(renderer, target);
SDL_RenderClear(renderer);
draw_target(renderer);

SDL_SetRenderTarget(renderer, NULL);
SDL_RenderClear(renderer);
draw_screen(renderer);
SDL_RenderPresent(renderer);
```

Check every `SDL_SetRenderTarget()` return value. `SDL_RenderFlush()` submits
work but does not reopen target selection.

Render targets are always enabled. Create all target textures before beginning
the frame's drawing scene, and render texture targets before the final screen
pass. The renderer does not use a target-texture hint.

The renderer advertises `SDL_RENDERER_TARGETTEXTURE`, so normal SDL renderer
selection can request that capability.

## Strided 16-bit textures

The hardware-validated native PVR renderer stores eligible ordinary static and
streaming RGB565, ARGB1555, and ARGB4444 textures using a Dreamcast strided
layout. The layout is selected at texture creation; textures that cannot use
the renderer's single active stride remain POT-backed automatically. This is
transparent to SDL callers, but all strided textures used in one presented
frame must share the same stride. POT textures may be mixed with them. `.dt`,
target, twiddled, VQ, and mipmapped textures retain their existing POT or
preformatted paths.

## PVR scene completion and texture retirement

`SDL_RenderPresent()` finishes the active PVR list and scene without waiting
for that scene's raster work to finish. Texture VRAM is retired by scene
generation: a destroyed or replaced allocation remains reserved until KOS
scene ordering proves that no pending render can still read it. Reclamation
runs at scene boundaries and texture destruction. Blocking drains remain for
allocation pressure, writes to VRAM still sampled by a pending scene, and
renderer shutdown.

This removes a scheduler wait from the common Present path; it does not by
itself make a frame render faster. On 640x480 VGA hardware, a warmed 30-frame
A/B showed Present time fall from 10.57 ms to 0.01 ms for 8 quads and from
70.27 ms to 0.01 ms for 20 fullscreen quads. The corresponding whole-frame
readings were 16.71 ms versus 16.82 ms and 81.04 ms versus 78.50 ms. Since the
wait moves to the next scene's start, and SDL2 has no public renderer flush to
fence each workload, treat these figures as diagnostic evidence of wait
relocation rather than a frame-rate improvement.
The workloads ran sequentially without an SDL2 renderer flush between them, so
each interval can include synchronization from the preceding workload.

The native `.dt` churn check passed on hardware: 20 load/destroy iterations
restored the exact 5,943,016-byte VRAM baseline. The texture DMA fixture also
passed its update-after-scene-reference and destruction pages. See
`test/dreamcast/test/pvr_dt_texture/README.md` and
`test/dreamcast/test/pvr_texture_dma/README.md` for those fixture contracts.
