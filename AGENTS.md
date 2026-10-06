# SDL3 Dreamcast — Project Instructions

## What this branch is

Working branch: `dreamcastSDL3`. Dreamcast is a first-class target here: video,
framebuffer, keyboard, mouse, window, joystick, haptics, audio, and filesystem
backends under `src/*/dreamcast/`, plus the OpenGL renderer driven through GLdc.

Current Dreamcast surface:

- `src/video/dreamcast/` — video driver, window, framebuffer, keyboard, mouse,
  GL context (`SDL_dreamcastopengl.c`).
- `src/render/opengl/SDL_render_gl.c` — the renderer used on Dreamcast via
  GLdc; Dreamcast-specific texture formats and stride handling live here.
- `src/audio/dreamcast/` — sound backend, including the ADPCM/SFX fast path
  (`SDL_DreamcastQueueADPCMSfx`, slot table, `SDL_LoadDreamcastADPCM_IO`).
- `src/filesystem/dreamcast/` — POSIX-based filesystem ops.
- `test/dreamcast/` — hardware test programs, each with its own `Makefile.dc`.
- `.dt` / pvrtex loading via `SDL_LoadDreamcastTexture_IO`.

A native KallistiOS/PVR `SDL_Renderer` backend exists under
`src/render/dreamcast/`. It is selected with `SDL_HINT_DC_VIDEO_MODE` and covers
the Phase 0 geometry and texture paths; Gates 1–9 have passed on physical
hardware. An experimental RGB565 target-preservation prototype is available
only with `SDL_VIDEO_RENDER_DREAMCAST_PVR_TARGETS=ON`, which defaults OFF. Its
initial hardware matrix passed, but stress and failure-recovery coverage remains
before target support can be promoted. See `test/dreamcast/TESTING.md`,
`test/dreamcast/results/phase0-baseline.md`, and
`test/dreamcast/results/phase1-targets-prototype.md`. The SDL2 tree
(`/home/gpf/code/dreamcast/SDL2`) is the historical algorithmic reference; SDL3
core remains authoritative for SDL-facing behavior.

## Build and test

Library build (installs to the KOS addon tree):

```sh
cd build-scripts
./dreamcast.sh              # configure (kos-cmake) + build + install
./dreamcast.sh install      # rebuild and install into the prefix
./dreamcast.sh clean        # cmake clean target
./dreamcast.sh uninstall    # remove installed files via install_manifest.txt
./dreamcast.sh distclean    # uninstall + remove build dir
