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
`src/render/dreamcast/`. It is selected with `SDL_HINT_DC_VIDEO_MODE`; Phase 0
Gates 1–9 and the RGB565 render-target preservation path have passed physical
hardware validation. Normal PVR builds support RGB565 targets with paired-buffer
preservation, switching, sampling, validated NPOT edges, and SDL3 rollback.
Sampled `.dt` textures load through `SDL_LoadDreamcastTexture_IO`; the PVR path
validates version-zero payload sizes, accepts 16-bit direct/VQ/mipmap layouts,
and rejects strided, partial, and paletted payloads. Its first SDL3 hardware
gate passed with a full-codebook RGB565 VQ texture; see
`test/dreamcast/results/pvr-dt-texture.md`. Other target formats remain
unsupported. The native renderer accepts `SDL_BLENDMODE_NONE` and
`SDL_BLENDMODE_BLEND`; screen and RGB565 target paths passed alpha and color
modulation checks on hardware. Failure-injection hooks are excluded from normal
builds and can be enabled with `SDL_VIDEO_RENDER_DREAMCAST_PVR_TESTS` for
dedicated test builds. See `test/dreamcast/TESTING.md` and the Phase 1 results.
The SDL2 tree
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
