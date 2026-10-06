# Experimental Phase 1 RGB565 target prototype — hardware PASS

Build guard: `SDL_VIDEO_RENDER_DREAMCAST_PVR_TARGETS=ON` (default OFF). The
prototype remains opt-in; target capability has not been promoted to normal
Dreamcast builds.

Built the screenshot-enabled `pvr_phase1_targets` fixture against the guarded
local `libSDL3.a`, then ran it on physical hardware at `192.168.0.128` through
`tools/dc-load/kos-tool-locked.sh` with `-m .`. The fixture asserted the native
renderer name and returned exit code 0.

Observed checks:

- Screen → target A → screen preserved the red screen region and later yellow
  region.
- A → B → A preserved both targets; completed targets sampled correctly to
  screen.
- 127×93 NPOT target sampling showed all four red, green, blue, and white edge
  texels.
- `SDL_RenderPresent()` rejected while a target was selected; that target
  remained selected and usable afterward.
- Injected target-switch failure restored the screen destination. The selected
  target had a 2× SDR white point; the subsequent screen gray patch captured as
  RGB 128, verifying color-scale rollback.

Exact hardware markers:

```
pvr_targets renderer=Dreamcast PVR
pvr_targets warmup=PASS frames=3
pvr_targets failed_switch_rollback=PASS target=screen
pvr_targets screen_target_screen=PASS
pvr_targets target_a_b_a_sample=PASS
pvr_targets npot_edges=PASS
pvr_targets selected_target_present_rejected=PASS
pvr_targets target_usable_after_rejection=PASS
pvr_targets result=PASS (inspect pages 1-4)
Program returned 0
```

Captures: [`phase1-captures/`](phase1-captures/). Page 1 includes the gray
rollback patch; page 3 contains the NPOT edge probe. Captures come from the
physical framebuffer and are not renderer readback.

Still untested: repeated switch/seed stress, VRAM allocation failure and
recovery, and runtime fail-closed behavior after PVR scene completion failure.
Do not remove the build guard or advertise target support until those pass.
