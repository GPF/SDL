# Native PVR sampled `.dt` texture gate

**Result: HARDWARE PASS** — physical Dreamcast, 2026-10-06, 640×480 VGA.

The normal PVR SDL3 library was installed under the writable
`build-scripts/local-install` prefix, and the fixture linked directly against
that archive. Its executable contained the PVR `.dt` validation strings and
the loader's native-renderer dispatch branch.

```text
pvr_dt renderer=Dreamcast PVR
pvr_dt valid_load=PASS format=SDL_PIXELFORMAT_RGB565 size=256x256
pvr_dt malformed_rejection=PASS
Saved hardware screenshot to /pc/pvr_dt_texture.ppm
pvr_dt result=PASS
Program returned 0
```

The valid ROM disk asset is a 256×256 RGB565 VQ texture with a full 2048-byte
codebook. It rendered centered on the 640×480 screen. The returned framebuffer
capture is [`pvr_dt_texture.ppm`](pvr_dt_texture.ppm).

The normal Dreamcast library build passed. A PVR-on/OpenGL-off library build
also passed, confirming the public loader does not depend on the GL translation
unit. A PVR-off/OpenGL-on `SDL3-static` build passed and exported only the
existing `SDL_LoadDreamcastTexture_IO` GL implementation. The full PVR-off
all-target build later failed while linking `testautomation.elf` in the SH-4
LTO toolchain; the SDL3 library target itself had built successfully.

The malformed case has the correct `DcTx` magic and version but a zero chunk
size; the loader rejected it before texture creation. The fixture also verifies
the renderer identity and presents three sampled frames before capture.

The loader accepts direct ARGB1555/RGB565/ARGB4444 layouts, VQ including reduced
codebooks, and square twiddled mip chains according to the historical PVR
payload contract. This SDL3 gate hardware-tested only full-codebook RGB565 VQ.
Strided, partial, PAL8, bump-map, YUV, and target `.dt` payloads remain
unsupported.

Two preliminary fixture runs used the wrong SDL3 link prefix and therefore
linked the global KOS addon archive. One faulted in the old `SDL_IOFromFile`
property path; the next followed the GL-only loader. The fixture now names the
local archive explicitly, and the final normal-build ELF was inspected before
the passing run. These preliminary runs are not counted as feature failures.
