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
