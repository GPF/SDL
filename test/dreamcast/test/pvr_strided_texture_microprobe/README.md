# Phase 6.2 Strided NPOT Texture Microprobe

This is a bounded, standalone KallistiOS/PVR hardware probe for the strided
texture design in `src/render/dreamcast/PVR_STRIDED_TEXTURE_RESEARCH.md`. It
does not link SDL, OpenGL, or GLdc and does not change renderer functionality.

The probe executes ten pages. An execution pass only proves that allocation,
submission, synchronization, capture, and cleanup completed. The physical
framebuffer captures are the visual verdict, especially for page 5.

## Correctness rules exercised

- A strided texture uses `PVR_TXRFMT_X32_STRIDE` and a global
  `pvr_txr_set_stride()` value in the legal 32–992, multiple-of-32 range.
- Logical width controls the destination geometry and U extent. Row pitch
  controls CPU/VRAM layout only.
- Header dimensions remain power-of-two. For a texture with pitch `S` and
  header height `H`, this probe allocates and uploads `S * H * 2` bytes.
- Ordinary POT textures use their POT width as row pitch and ignore the global
  strided-texture register.
- Every rendered scene completes through `pvr_wait_render_done()` before its
  texture can be modified or freed and before a framebuffer capture.
- PVR is initialized and shut down exactly once.
- A 32-byte allocate/free warms dlmalloc before the VRAM baseline, excluding
  its one-time arena metadata. Recovery must then be exact.

## Page matrix

### Page 1 — 640×480 strided, nearest

- Pitch 640, header 1024×512, allocation 640×512×2 = 655,360 bytes.
- Full logical image mapped to 640×480.
- Expect stable checker bands, colored top/left orientation markers, a white
  right column, and a colored bottom row.

### Page 2 — 640×480 strided, bilinear edge observation

- Same storage as page 1, with bilinear filtering.
- This is observational. A tight strided row has no hidden right-side padding;
  the first and last columns deliberately differ so any right-edge row-wrap or
  clamping behavior is visible. Do not describe the result as interpolation
  toward zero until the capture proves it.

### Page 3 — strided plus ordinary POT in one scene

- Left: logical 640×480, pitch 640, header 1024×512.
- Right: ordinary 512×512 POT control.
- Both are displayed as distinct panels. Expect the ordinary control to remain
  correct while the global stride is 640.

### Page 4 — two strided textures sharing pitch 512

- A: logical 512×256, pitch 512, header 512×256, 262,144 bytes.
- B: logical 384×288, **pitch 512**, header 512×512, 524,288 bytes.
- The register is set to 512 once. The narrower B panel still uses its logical
  width for U and destination extents.

### Page 5 — different pitches in the same list (hinge)

- Left: logical 640×480, pitch 640.
- Right: logical 384×288, pitch 384.
- Bottom-center: ordinary 256×256 POT control.
- Submission sequence: set 640, submit left; set 384, submit right and POT.
- The panels do not overlap. Record which strided panel is distorted and
  whether the pattern is consistent with the final global stride winning.
- This page is never an automatic visual pass. One ordering does not establish
  order independence, and it does not establish that scene splitting is safe.

### Page 6 — different pitches in different lists of one scene

- The opaque list receives a 640-pitch left panel and is finished.
- The register changes to 384 before the transparent list receives the right
  panel; both lists belong to the same scene.
- This distinguishes a per-list latch from a truly frame-global register.
- Hardware result: the opaque-list 640 panel is corrupted while the
  transparent-list 384 panel is correct. The final stride wins across lists.

### Page 7 — stride change across consecutive frames

- Frame 1 uses pitch 384 and completes.
- The consecutive captured frame uses pitch 640.
- The capture contains **only frame 2**. Expect the full 640×480 pattern to be
  correct after the prior frame used a different global stride.

### Page 8 — padded-pitch CPU source update

- Texture: logical 640×480, pitch 640, header 1024×512.
- A 64×64 yellow source has a pitch of 128 words (256 bytes).
- Rows are copied into the texture staging buffer at (100,100), using the
  source pitch only to advance source rows. The upload is then exactly the
  texture's 655,360-byte strided allocation.
- Expect one yellow 64×64 region and unchanged surrounding pixels.

### Page 9 — logical edges and one-pixel source regions

- Main panel maps the complete 640×480 logical source.
- Four 56×56 blocks sample the four individual corner texels.
- Two strips separately sample the final logical column and final logical row.
- Expect all probes to match the source edge markers with no POT-width leakage.

### Page 10 — recovery/final reference

- Left: ordinary 512×512 POT control, 524,288 bytes.
- Right: logical 320×240 strided texture, pitch 320, header 512×256,
  163,840 bytes.
- Expect both distinct panels to render correctly. After capture, both are
  freed and available VRAM must exactly match the initial value.

## Build

Ordinary strict build:

```sh
cd test/dreamcast/test/pvr_strided_texture_microprobe
make -f Makefile.dc clean
make -f Makefile.dc -B
```

Screenshot-enabled strict build:

```sh
make -f Makefile.dc clean
make -f Makefile.dc -B HARDWARE_SCREENSHOTS=1
```

Both variants produce:

- `pvr_strided_texture_microprobe.elf`
- `pvr_strided_texture_microprobe.bin`
- `pvr_strided_texture_microprobe.map`

The ordinary build holds each page for approximately six seconds. The capture
build waits briefly after synchronized rendering, captures once, and advances.

## Physical hardware launch

Coordinate capture access to `/pc` before launching the screenshot build, then
allow the program to return normally:

```sh
kos-tool -t 192.168.0.128 -x pvr_strided_texture_microprobe.elf -m .
```

Do not interrupt `kos-tool` while the ELF is running.

## Capture paths

```text
/pc/pvr_strided_texture_microprobe-phase6_2-page1.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page2.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page3.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page4.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page5.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page6.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page7.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page8.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page9.ppm
/pc/pvr_strided_texture_microprobe-phase6_2-page10.ppm
```

A capture failure is a test failure; the program cannot print its final
execution-pass line after an ignored screenshot error.

## Expected serial milestones

1. `=== Phase 6.2 Strided NPOT Texture Microprobe ===`
2. `OK: pvr_init_defaults() succeeded`
3. Initial available VRAM.
4. Initial stride-register readback.
5. For each page: texture layout, every stride transition and its readback,
   capture result, and
   `EXECUTION OK (hardware image is verdict)`.
6. Exact zero-byte VRAM delta against the post-warmup baseline.
7. `OK: pvr_shutdown() completed`.
8. `EXECUTION PASS; PHYSICAL CAPTURES REQUIRE INSPECTION`.

## Interpretation and limitations

- Page 5 is the hardware hinge. Do not convert its serial execution result
  into a renderer policy without inspecting its image.
- Page 6 proves that list boundaries do not latch independent strides. Page 7
  proves that a completed frame boundary permits a stride change.
- Page 2 measures real tight-row bilinear behavior; it does not assume a
  nonexistent right-side padding region.
- The probe covers RGB565, synchronous upload, 640×480 VGA, and pitches through
  640. It does not cover DMA, VQ, twiddled layouts, `.dt` loading, PAL/NTSC,
  every legal pitch through 992, or stress/capacity behavior.

## Physical-hardware validation (2026-07-29)

Passed on a real Dreamcast at 640×480 VGA using the screenshot-enabled ELF.
All stride writes read back correctly, all ten captures succeeded, VRAM
returned exactly to the post-warmup baseline of 5,789,416 bytes, PVR shut down
cleanly, and dc-load reported `Program returned 0`.

Direct inspection found:

- Pages 1–4 and 7–10 match their documented layouts and edge/update probes.
- Page 5's 640-pitch left panel is corrupted after the same-list stride change;
  its 384-pitch right panel and ordinary POT control remain correct.
- Page 6 produces the same final-stride-wins result across opaque and
  transparent list boundaries in one scene.
- Page 8 contains the exact yellow 64×64 update with its surroundings intact.
- Page 9's corner texels, final column, and final row match their source
  markers without POT-width leakage.

Implementation policy: one active stride per frame/scene, same-stride batching,
and POT fallback for conflicts. A list boundary is not sufficient.
