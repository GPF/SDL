# Dreamcast PVR ADD/MUL validation

This focused test validates the native PVR renderer's single-pass RGB565
mappings for `SDL_BLENDMODE_ADD` and `SDL_BLENDMODE_MUL`:

- ADD: `PVR_BLEND_SRCALPHA / PVR_BLEND_ONE`
- MUL: `PVR_BLEND_DESTCOLOR / PVR_BLEND_INVSRCALPHA`

`SDL_BLENDMODE_MOD` and unsupported custom equations must remain rejected.
The test does not enable render targets, alpha-bearing destinations, multipass
composition, or Workload F from the separate capacity test.

## Build

From the SDL repository root, after loading the KallistiOS environment:

```sh
source /opt/toolchains/dc/kos/environ.sh
make -C test/dreamcast/test/pvr_blend_add_mul -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild"
```

The Makefile compiles the test with `-Wall -Wextra -Werror` and links the
requested SDL archives by explicit path.

The ordinary build must contain neither `vid_screen_shot` nor a hardware `/pc`
capture path. Build the physical-hardware capture variant explicitly:

```sh
make -C test/dreamcast/test/pvr_blend_add_mul -f Makefile.dc -B \
  SDL2_LIBDIR="$PWD/build-scripts/dcbuild" HARDWARE_SCREENSHOTS=1
```

Launch from the test directory so the Dreamcast `/pc` mount writes the four
PPMs there:

```sh
cd test/dreamcast/test/pvr_blend_add_mul
kos-tool -t 192.168.0.128 -x pvr_blend_add_mul.elf -m .
```

All runtime validation is performed on physical Dreamcast hardware. Serial
`PASS` is only the API/build result; the four hardware PPMs must also be opened
and inspected directly.

## Page contract

Each page is independently queued, presented, allowed three vertical blanks,
captured once, and held for 8 seconds. The capture paths are:

1. `pvr_blend_add_mul-hardware-page1.ppm`
2. `pvr_blend_add_mul-hardware-page2.ppm`
3. `pvr_blend_add_mul-hardware-page3.ppm`
4. `pvr_blend_add_mul-hardware-page4.ppm`

### Page 1: untextured numeric reference

- The upper six pairs exercise ADD source alpha `0, 1, 64, 128, 254, 255`.
- The lower six pairs exercise the same endpoints for MUL.
- In every pair, the left rectangle is the CPU-computed RGB565 result and the
  right rectangle is the native PVR blend result.
- Each pair should agree exactly or differ by no more than two RGB565 steps per
  channel. One input-quantization step plus one output-quantization step is the
  defined worst-case tolerance when the CPU and PVR quantize at different
  stages.
- Magenta two-pixel guards must remain intact.
- The right-side probes check additive saturation and low/high MUL behavior.

The MUL equation is `src*dst + dst*(1-srcA)`. Its source-color term is not
multiplied by source alpha, so a white source at alpha zero doubles the
destination instead of leaving it unchanged.

### Page 2: textured state

- The left ADD and MUL cells are CPU-generated RGB565 reference textures; the
  middle cells are the corresponding native blends and must agree within one
  RGB565 step per channel at equivalent texels.
- The right cells independently exercise native color/alpha modulation.
- All cells must remain inside their magenta guards.
- The nearest probe must remain blocky and the linear probe must be smooth.
- No stale texture state, padding leakage, diagonal seam, or missing geometry
  is acceptable.

### Page 3: painter order and cache transitions

- The left strips alternate `NONE`, `BLEND`, `ADD`, `MUL`, `ADD`, and `NONE`.
- The right panel uses a nonzero `(320,0 320x480)` viewport and alternates ADD
  and MUL using viewport-relative coordinates.
- Its second guarded cluster contains four genuinely overlapping ADD/MUL
  layers and must reflect their exact submission order.
- Every strip must be visible in submission order with intact guards.
- No right-panel geometry may disappear from a double-applied viewport offset.

### Page 4: per-vertex geometry

- One ADD triangle and one MUL triangle must show smooth per-vertex color and
  alpha interpolation over their guarded backgrounds.
- Each triangle is submitted once; unexpected doubled intensity is a failure.
- The textured ADD quad must cover its brown background at `(220,30 90x90)`.
- The RGB565 quantization probe at `(340,30 140x60)` must match the printed CPU
  reference within one RGB565 step per channel.

## Acceptance gate

The increment passes only when the capture build returns zero on physical
640x480 VGA hardware, prints each page and capture confirmation followed by
`PASS`, and direct inspection of all four retained images satisfies the page
contract. Any missing geometry, corruption, reset, freeze, failed capture,
guard leakage, unexpected state transition, or reference mismatch blocks the
capability from being treated as validated.

After hardware validation, rebuild the ordinary ELF and verify its symbol and
string tables contain no screenshot reference or `/pc` capture path.

## Physical-hardware result (2026-07-29)

Passed on a real Dreamcast at 640x480 VGA. The BBA launch completed every page,
wrote all four captures, printed `PASS`, returned zero, and returned control to
`dc-load-ip`. The complete serial transcript is
`hardware-20260729-100523.log`.

Direct inspection found all guards and geometry intact with no corruption,
missing primitive, seam, freeze, reset, or stale state. The Page 1 CPU/native
pairs and sampled corresponding Page 2 texels agreed exactly or within one
RGB565 channel step. Page 3's overlapping cluster preserved submission order,
and Page 4's single-submit triangles, textured ADD quad, and quantization probe
all rendered in their expected positions.

Capture hashes:

- Page 1: `983bf27b2968f357a683ae4e68194edfc8ddcc85fafd9965356bd9ddc2c548c9`
- Page 2: `da6c1e8af380dea3c745bcae2a865417af2bac3b35bbcf4fb5b5069c694bcfb9`
- Page 3: `cf4109826619fe7346fdc6185a42f3dbe2a7cb7a98117c1ebc47cbc7ea3d0a2b`
- Page 4: `08515098cd3dd584ef4dbfea1ec1169a1104bc63ae3a28a170d4979229673f15`

This result validates ADD and MUL only for the current RGB565 screen path.
Render targets and alpha-bearing destinations remain outside this increment.
