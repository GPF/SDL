# Phase 0 baseline — Gates 1–9 hardware pass

## Level 1: host build

```
SDL_VIDEO_RENDER_DREAMCAST_PVR 1        # generated build config, Dreamcast build
```

- `./dreamcast.sh` completes with no warnings attributable to the new renderer.
- The two warnings in the Dreamcast video block (`SDL_dreamcastwindow.c:261`
  bool compare, unused `DREAMCAST_SetDisplayMode`) are pre-existing and not from
  Phase 0.
- `git diff --check` clean.
- `./dreamcast.sh install` updates the KOS addon tree.
- All nine Phase 0 test targets build and link with `kos-cc`.

## Level 2: Gate 1 — hardware PASS

Run on physical hardware at `192.168.0.128` through
`tools/dc-load/kos-tool-locked.sh` with task `pvr-gate`, Gate 1 alone.

Observed on physical hardware through the locked console launcher. Gate 1 verifies
the PVR mode, renderer name, clear/present return, and clean exit. The lifecycle
fixture is a log gate; visual color verification is covered by later captures.

```
Initializing SDL_DC_DREAMCAST_PVR_VIDEO mode
SDL3 Dreamcast video initialized: 640x480 (default), texture hints: (null)x(null)
[dcwin_probe] CreateWindow vid_set_mode final disp=4097 pixel=1
pvr_gate renderer=Dreamcast PVR
pvr_gate vsync=PASS default=0 toggle=0,1,0
pvr_gate vsync_interval_rejected=PASS value=2
pvr_gate output=640x480
pvr_gate format[0]=SDL_PIXELFORMAT_RGB565
pvr_gate format[1]=SDL_PIXELFORMAT_ARGB1555
pvr_gate format[2]=SDL_PIXELFORMAT_ARGB4444
pvr_gate clear_present=PASS
pvr_gate destroy_returned=PASS
pvr_gate lifecycle=PASS
Program returned 0
```

`disp=4097` is `DM_640x480`, `pixel=1` is `PM_RGB565`. Renderer name, output
size, supported formats, clear/present, and clean exit all match the expected
block. No hang, so scene/list lifetime is balanced and PVR ownership is not
contended by the video layer.

### Defects found by Gate 1 and fixed

1. **Window layer had no PVR branch.** `DREAMCAST_CreateWindow` selects
   `disp_mode` from the video-mode flags (opengl, textured, DMA, direct). The PVR
   mode matched none, so `disp_mode` stayed `-1` and window creation failed with
   "Unsupported display mode for 640x480". PVR now uses the standard
   resolution/refresh table with `PM_RGB565`, matching the SDL2 PVR path.
2. **`renderer->name` was never assigned.** SDL3 core does not set it from the
   driver; each backend assigns it in `CreateRenderer`. Ours did not, so
   `SDL_GetRendererName()` returned NULL. A stray `SDL_Log` of the name macro was
   the only thing printing it, which is why the first run looked like the name
   existed. Fixed, and the stray log removed.

Both fixes are hardware-verified by Gate 1. Gate 1 itself does not exercise
deferred-VRAM ordering or line rejection; the latter remains covered by source
behavior and is not claimed as line support.

### Phase 0 VSync behavior — hardware verified

SDL3 calls `SetVSync(0)` during renderer creation. The backend now accepts
intervals 0 and 1 without leaving a stale SDL error, and rejects interval 2 and
adaptive mode. Gate 1 verified default 0, toggling 0→1→0, and interval-2
rejection. Interval 0 disables SDL's simulated pacing; the Dreamcast video
layer still page-flips at vblank.

## Gates 2–5 — hardware PASS

Gates 2–5 each selected `Dreamcast PVR`, exited with code 0, and passed their
assertions. Captures are in [`phase0-captures/`](phase0-captures/).
The five fixtures were rebuilt with `SCREENSHOT=1` and rerun after removing the
START-button wait; each now captures its frame and exits without controller input.

- **Gate 2:** clear and solid triangle passed. The screenshot showed the expected
  clear background and colored triangle.
- **Gate 3:** nearest and linear textured quad passed. The first capture exposed
  transparent ARGB1555 fixture texels; the fixture was corrected to set the
  opaque bit (`0x801f` blue, `0xfc00` red), then rebuilt and rerun. Captures show
  the expected checkerboard in both modes.
- **Gate 4:** coordinate transform passed. Captured sample locations matched the
  expected red, green, blue, yellow, cyan, and black regions.
- **Gate 5:** all seven rejection/boundary cases passed. The first run showed
  unsupported blend errors could be lost by `SDL_RenderPresent`; the backend
  first rejected at queue time. The PVR-specific SDL core blend gate now rejects
  MOD in `SDL_SetRenderDrawBlendMode`, with the queue guard retained as defense
  in depth. The revised fixture and library were rebuilt and rerun on hardware.
  Its updated capture is uniform black after the final clear and intentionally
  rejected geometry; Gate 5 is an API/log gate, not visual rendering evidence.

These are visual checks of captured frames plus API/log assertions; they do not
provide renderer readback or prove untested Phase 0 paths.

## Gate 6 — copy, rotation, and flip — hardware PASS

Built the screenshot-enabled `pvr_phase0_copy_transform` fixture and ran it on
the physical console. It selected `Dreamcast PVR`, all four copy/transform calls
queued, presentation and screenshot capture succeeded, and the program returned
0. The retrieved capture shows the expected corner mapping for the plain copy,
90-degree clockwise rotation, horizontal flip, and vertical flip. See
[`pvr_phase0_copy_transform.ppm`](phase0-captures/pvr_phase0_copy_transform.ppm).

The first one-frame version captured black despite successful queue and present
returns. Repeating the same scene for three presents, as the other visual gates
do, produced the expected image. Gate 6 uses that repeated-present pattern; this
first-frame capture behavior is a remaining presentation timing question, not a
failure of the verified transform mapping.

## Gate 7 — scale-1 points and line rejection — hardware PASS

Built `pvr_phase0_point_line` with screenshot capture and ran it on the physical
console. It selected `Dreamcast PVR`, confirmed render scale `1.0,1.0` with
logical presentation disabled, drew the colored point pattern, and observed the
expected queue-time rejection for `SDL_HINT_RENDER_LINE_METHOD=2`. The fixture
captured the frame and returned 0. The screenshot shows red, green, and blue
dotted blocks; see
[`pvr_phase0_point_line.ppm`](phase0-captures/pvr_phase0_point_line.ppm).

This verifies SDL3's scale-1 route to `QueueDrawPoints` and method-2 route to
`QueueDrawLines`. Gate 9 separately verifies the default POINTS path's
rectangle/Bresenham rasterization. A final Gate 7 fixture rebuild added an
explicit assertion that logical presentation is disabled; that rebuilt fixture
was also rerun successfully on hardware after the outside mGBA loader released
the console.

## Gate 8 — deferred texture destruction — hardware PASS

Built the screenshot-enabled `pvr_phase0_deferred_texture` fixture and ran it on
the physical console. It selected `Dreamcast PVR`, presented three textured
warmup frames, queued a draw from the red/blue checker, destroyed that texture
before `SDL_RenderPresent`, and then presented successfully. The capture shows
the red/blue checker from the destroyed source plus a green/yellow retained
control checker; see
[`pvr_phase0_deferred_texture.ppm`](phase0-captures/pvr_phase0_deferred_texture.ppm).
The fixture returned 0, and the screenshot has five colors including the
background.

This follows the SDL2 `pvr_texture_stress` queued-destroy probe: draw, destroy
before present, then present, with no explicit flush. SDL3's destroy path flushes
commands that reference the texture, so the PVR scene is active when the backend
defers VRAM release. `RenderPresent` waits for rendering before releasing it.

The initial one-scene captures were all black, including a retained-texture
control, so that symptom did not implicate deferred freeing. Repeating actual
textured warmup frames before the tested frame produced the visible capture. The
exact cause of the black captures remains unproven; no renderer presentation
change was made.

## Gate 9 — default SDL3 line method — hardware PASS

Ran `pvr_phase0_default_lines` on the physical console. Before renderer
creation, it confirmed `SDL_HINT_RENDER_LINE_METHOD` was unset; after creation,
it asserted `Dreamcast PVR`, exact scale `1.0,1.0`, and disabled logical
presentation. Horizontal, vertical, shallow, steep, reversed, zero-length, and
connected translucent lines all returned success through SDL3's default
POINTS-method path. The screenshot shows the expected axis-aligned rectangle
and diagonal Bresenham rasterization, the isolated zero-length pixel, and
connected joints without brighter double coverage. See
[`pvr_phase0_default_lines.ppm`](phase0-captures/pvr_phase0_default_lines.ppm).

The fixture returned 0 on hardware. As with the other visual gates, the image
is the rendering evidence; successful API returns alone do not prove coverage.

## Driver selection policy (current)

The Dreamcast default video mode is `SDL_DC_OPENGL_VIDEO`; the PVR renderer is
opt-in through `SDL_HINT_DC_VIDEO_MODE=SDL_DC_DREAMCAST_PVR_VIDEO`, which sets
`SDL_HINT_RENDER_DRIVER` when no explicit driver is requested. `DC_PVR_RenderDriver`
sits after the other hardware drivers, so a non-PVR window is not disturbed by a
failed PVR attempt. The fallback path for a PVR video mode combined with an
explicit non-PVR driver name has not been exercised on hardware.
