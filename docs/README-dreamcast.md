# Dreamcast SDL2 notes

## Native PVR renderer

Select `SDL_DC_DIRECT_VIDEO` before creating the window to use the native PVR
renderer. The renderer is independent of the GLdc-backed OpenGL renderer.

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

The implementation is currently experimental. SDL must be compiled with
`DC_PVR_ORDERED_RENDER_TARGETS`, and the application must set
`SDL_HINT_DC_PVR_RENDER_TARGETS` to `"1"` before renderer creation. The hint is
disabled by default and is intended to be removable after physical-hardware
validation.

The capability is reported on the created renderer instance. Because the
static driver description remains unchanged while the hint is experimental,
automatic `SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE)` driver
filtering does not discover it. Select the Dreamcast PVR driver explicitly (or
through `SDL_HINT_RENDER_DRIVER`) and query the created renderer information.
