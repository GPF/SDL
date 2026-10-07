# Direct 16-bit `.dt` hardware gate (ARGB1555 / RGB565 / ARGB4444)

**Result: PARTIAL — direct panels HARDWARE PASS, sampled-target row OPEN.**
Physical Dreamcast, 2026-10-06, 640×480 VGA, console `192.168.0.128`.

## What is covered

Fixture: `test/dreamcast/pvr_dt_direct/` (`pvr_dt_direct.c`, `Makefile.dc`,
`compare_capture.py`). Assets regenerated with the active KOS `pvrtex`
(`/opt/toolchains/dc/kos/utils/pvrtex/pvrtex`) and were byte-for-byte identical
to the previously stored assets. All three are `DcTx` version-zero, 256×256,
twiddled direct 16-bit payloads; the format bits match ARGB1555, RGB565, and
ARGB4444 respectively.

The `.dt` parser was not modified.

## Hardware log (gate run)

```text
RGB565 LOAD PASS
ARGB1555 LOAD PASS
ARGB4444 LOAD PASS
MALFORMED-REJECTION PASS
RGB565 TARGET PASS   (logged as of this run; now "TARGET SUBMIT OK (pixels unverified)")
ARGB1555 TARGET PASS
ARGB4444 TARGET PASS
DIRECT 16-BIT .DT GATE failures=0
Program returned 0
```

## Capture comparison

Capture retained as
`results/phase1-captures/pvr_dt_direct_hw3_black_target_row.ppm`.

```text
rgb565   panel0 direct: 6400/6400 pixels match
argb1555 panel1 direct: 6400/6400 pixels match
argb4444 panel2 direct: 6400/6400 pixels match
rgb565   panel0 target:  600/6400
argb1555 panel1 target: 2400/6400
argb4444 panel2 target: 1400/6400
total 38400 pixels, 14805 outside tolerance
```

All three **direct** panels match fully. The **target** row is black at panel
centers, with a one-pixel placement skew in some panels. A rerun produced an
identical capture, so the black target row is reproducible on hardware.

The earlier stored capture `results/pvr_dt_direct.ppm` matched all six panels,
but no hardware log was recorded for it, so it is not counted as a documented
hardware pass.

## Diagnostic bisect (`pvr_dt_diag.c`)

Separate, revertible diagnostic fixture; the gate itself was not modified.

| Test | Result |
|---|---|
| First run, no warmup | Entire capture black (documented first-frame capture timing) |
| Three rendered-frame warmup | Direct `.dt` panel correct; target panel black, including an untextured marker drawn inside the target |
| POT 64×64 vs NPOT 80×64 | Both black |
| Warmup before target scenes | Both black |
| Same target upscaled vs 1:1 | Both black |
| `SDL_HINT_DC_VIDEO_MODE` + named `"Dreamcast PVR"` renderer | Video mode still reported `default`; targets black |
| Exact promotion-fixture page2 pattern, single target | Entire frame black |

## Control that proves RTT sampling works on hardware

Re-running `test/dreamcast/pvr_phase1_targets/pvr_phase1_targets.elf` produced
correct target content on hardware (`page2`): target A `80×64` sampled upscaled
at `(80,160)` shows the blue clear with the green rect at target-local
`(8..28, 8..28)` and the white rect at `(48..68, 8..28)`. Capture retained as
`results/phase1-captures/pvr_phase1_targets_page2_control.ppm`.

So the black diagnostic target panels are **not** a general RTT sampling failure.

## Two new findings

1. **Some diagnostic captures are stale frames, not the rendered frame.** The
   clean POT-vs-NPOT capture reproduces the warmup frame exactly (white spans
   screen `32..96`, including `95,95` outside the panel). Any run whose capture
   cannot be identified as the final frame is invalid. Keep an untextured marker
   in the final scene so the capture is provably the final frame.
2. **A screen scene must execute before the first target scene.** Adding a
   flushed screen clear before the first `SDL_SetRenderTarget(target)` made the
   final frame render correctly (red background + white marker visible), but the
   target panel stayed black. That is the only run that proves a target scene
   produced nothing on hardware.

Size is not the discriminator: `80×64` works in the promotion fixture and fails
in the diagnostic fixture.

## Open contradiction

The promotion fixture and the diagnostic fixture use the same renderer, the same
target format, the same target size, the same content pattern, and the same
capture helper. The promotion fixture's target scenes produce content; the
diagnostic fixture's do not. Remaining structural differences:

- The promotion fixture creates four targets (`80×64`, `80×64`, `64×48`,
  `127×93`) before the warmup; the diagnostic fixture creates one or two.
- The promotion fixture creates a target through properties with
  `SDL_PROP_TEXTURE_CREATE_SDR_WHITE_POINT_FLOAT = 2.0f`.
- The promotion fixture asserts that `ARGB1555` target creation is rejected.
- The promotion fixture exercises failure injection and a failed-switch rollback.
- The promotion fixture commits target scenes by target→target switches, not
  only target→screen.

## Review corrections

- **Gate false-green.** `TARGET PASS` only meant the SDL calls succeeded; the
  capture shows the rows black with `failures=0`. The log line is renamed to
  `TARGET SUBMIT OK (pixels unverified)`; the target row is verified only by
  `compare_capture.py`.
- **Commit path is not the discriminator.** `pvr_dt_diag.c` already commits
  `pot` via a target->target switch and `pot` is still black, so the
  "target->target vs target->screen" test is dropped.
- **Aliasing is not supported by the evidence.** The clean POT-vs-NPOT capture
  (`phase1-captures/pvr_dt_diag_pot_vs_npot.ppm`) is the **warmup frame**, not the
  final frame: white extends to screen `(95,95)`, which is inside the warmup rect
  `(32,32,64,64)` but **outside** the 64×64 panel region `(16..79)`, and
  `(100,100)` is black. A panel-local mapping could not produce pixels outside the
  panel, so that run is invalid and aliasing is dropped as an inference.
  - **Confirmed by the 2026-10-06 second pass.** Checked against the fixture's
    expected final frame (red clear, white marker at (300,300), pot panel at
    (16,16) with the red/green checker at target (8,8,48,48)): the stored capture
    mismatches expected at 4192 px, the new capture at 1001 px. The new capture's
    panel content matches the expected checker block-for-block, so the stored
    capture is the wrong frame and the new one is right.
  - **Still unexplained in the new capture:** 704 px at y=0..1 (x=128..639) and
    ~16 px at y=479 (x=608..637) are black where the expected final frame is red.
    Those pixels equal the previous frame's black clear, so they look like pixels
    the final clear polygon did not cover rather than a capture-edge artifact.
    Not settled; a corner-marker run would be needed.
- **Valid target-scene evidence is the red-background run**: three warmup frames,
  a flushed screen scene, one target scene, then a final screen scene with a red
  clear and an untextured white marker at `(16,464)`. The marker and background
  appeared, so the final frame was visible, and the target panel was black. That
  capture was overwritten by the later run — save one capture per run from now on.
- **Finding 2** used a clear-only screen scene; whether that "flushes" is unproven.
- **Video mode is not a difference.** Both fixtures print
  `SDL3 Dreamcast video initialized: 640x480 (default)` with the same hint, so the
  video-mode bisect axis is removed.
- **Unexplained:** the one-pixel placement skew in the target row.
- "RTT sampling works" holds only for the promotion fixture.

## Next steps

0. Log target VRAM address, framebuffer addresses and `current_valid` at each
   target switch in both fixtures and diff the output. Keep the red clear + white
   marker in the final scene so the run proves the captured frame is the final
   frame, not a stale one, and save the capture under a per-run name.

1. Bisect the promotion fixture against the diagnostic fixture by adding one
   known-good structural difference at a time (target count, creation order,
   properties, rejection, failure injection, target→target commit).
2. Inspect backend target-scene behavior in detail: `DC_PVR_EmitClear()`,
   `DC_PVR_EmitPreservationSeed()`, `DC_PVR_RunGeometryCommand()`, `sample_vram`,
   `current_valid`, target VRAM allocation, and buffer swapping — specifically
   whether target VRAM can alias the screen framebuffer.
3. Record a formal hardware log for the direct 16-bit gate once the target row
   is explained, then decide whether the gate should assert the target row at all.

## Process notes

- Every launch went through `tools/dc-load/kos-tool-locked.sh` with a task label.
- An empty `SDL3_CONFIG_INCLUDEDIR` swallowed `-DPVR_PHASE0_HARDWARE_SCREENSHOT`
  as an `-I` argument, so the screenshot code was not compiled. The diagnostic
  Makefile rule now includes `CPPFLAGS`, and the define must be verified to reach
  the compiler.
- A failed compile silently ran the stale ELF and produced a misleading capture;
  build output should not be discarded, and captures should be deleted before a
  run so a stale file cannot be mistaken for a fresh one.

## Trace run with vblank-2/3 capture (decisive)

`pvr_dt_diag.elf` built against a separate trace library
(`build-scripts/local-install-trace`, `-DDC_PVR_TRACE_TARGETS`), captured at
vblanks 1/2/3 after the final present
(`results/phase1-captures/pvr_dt_diag_marker_run_vbl{1,2,3}.ppm`, log
`pvr_dt_diag_trace_run2.log`).

- vbl1: stale page (black, old cyan marker) — the earlier "black target" capture
  was this.
- vbl2 and vbl3: red background, white marker at (300,300), and the target panel
  shows the green rect (0,252,0) and blue clear (0,0,248). **The target scene
  wrote and sampled correctly.**
- Trace: first switch vram=a4457180 next=a4455160; after the scene they swap and
  sample_vram=a4455160 (the buffer rendered to). No overlap with screen buffers.
- The texel-0 readback via the `0xA4` pointer read 0x0000 even though the pixels
  are correct, so that readback is **not valid** for RTT content (interleave or
  render race) and should not be used as evidence.

**Superseded.** `vid_waitvbl()` only waits for the display (framebuffer)
vblank; it does not wait for PVR rendering. The vbl1/2/3 difference in the diag
was the page flip, not render timing. Re-running the gate with vbl1/2/3 captures
gave an identical black target row (14805 px outside tolerance, skew unchanged),
so timing does **not** explain the gate. The helper no longer calls
`vid_waitvbl()`; the diag is open again. Differences from the gate still to test:
a sampled texture drawn inside the RTT scene (gate) vs untextured fills (diag),
and a presented screen scene before the first target scene (diag only).

## Resolution (2026-10-06): gate passes on hardware

Two causes, neither in the renderer logic:

1. **Capture read the wrong page.** `vid_screen_shot()` reads `vram_l`
   (`0xa5094880`), which is not the page the PVR displays. The PVR front page was
   `0xa5494880` (`pvr_get_front_buffer()`). `pvr_phase0_capture()` now reads the
   PVR front buffer directly (RGB565 only, logs `capture: front64/linear/back64/vram_l`)
   and needs no `vid_waitvbl()`. `vid_waitvbl()` only waits for the display
   vblank, not for PVR rendering.
2. **The `local-install` library was stale** relative to the working tree
   (confirmed: the old install with the new front-buffer helper still gave the
   black target row, 14805 px outside tolerance, log
   `pvr_dt_direct_gate_local_install.log`). A
   fresh build of the current source (separate prefix
   `build-scripts/local-install-trace`, trace define off) renders targets
   correctly; the older install did not. The trace hunk
   (`-DDC_PVR_TRACE_TARGETS`) is not the cause (same result with and without it).

Gate run (fresh library, front-buffer capture, locked loader):
`results/phase1-captures/pvr_dt_direct_gate_fresh.log` and
`pvr_dt_direct_fresh.ppm`: all six panels 6400/6400, 0 of 38400 pixels outside
tolerance, no placement skew reported. The earlier black target row, the skew and
the "aliasing" finding came from the stale capture and the stale library.

Remaining: `TARGET SUBMIT OK` still only reflects API success; decide whether the
gate should assert target pixels. Reinstall `local-install` from current source
before treating it as the documented-pass artifact.

### Canonical library reinstalled and re-verified

The old install is kept as `build-scripts/local-install.stale-20261006`.
`local-install` was reinstalled from the current source (untraced build,
byte-identical to the fresh library). Control run against it:
`pvr_dt_direct_gate_reinstalled.log` / `pvr_dt_direct_reinstalled.ppm`:
`GATE failures=0`, `compare_capture.py` 38400 px, 0 outside tolerance, exit 0.

Decisions: the texel readback is removed from the trace hunk (CPU read through the
`0xa4` 64-bit view is not valid for framebuffer/RTT content; the `0xa5` view with
the `/2` conversion is what works). The in-program target pixel check is not
added; `compare_capture.py` (nonzero exit on mismatch) is the pixel gate.
Other fixtures that use `pvr_phase0_capture()` now capture the PVR front buffer
and reject non-RGB565 modes; re-run the Phase 1 hardening control and the
promotion fixture against this library before citing their earlier passes.

Note: the 1-pixel horizontal offset seen in the hardening captures was reproduced
with the stale library plus the front-buffer helper (x=441..536 vs 440..535 with
the current library), so it is a library difference. The promotion fixture did
not show it (pixel-identical), so which draws are affected is not yet known.
