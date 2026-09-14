# Dreamcast `.dt` native texture integration

## Verdict

Phase 6.1 is complete for twiddled 16-bit direct textures, VQ compression,
reduced VQ codebooks, and square VQ mip chains produced by KallistiOS
`pvrtex`. The existing Dreamcast-only public loader now parses and validates
the `DcTx` container once in SDL's renderer core and dispatches PVR-ready data
through a Dreamcast-only renderer callback. The GLdc-backed OpenGL renderer
retains support, and the native renderer gains direct VRAM upload and sampling.

All shared SDL declarations, structure fields, allocation dispatch, and loader
code are protected by `__DREAMCAST__`. The native implementation remains in
Dreamcast-only files. Non-Dreamcast renderer structure layout and texture
creation code are unchanged at preprocessing time.

## Container and allocation rules

The parser accepts version-zero, 32-byte-aligned `DcTx` chunks with exact PVR
power-of-two dimensions from 8 through 1024. It accepts RGB565, ARGB1555, and
ARGB4444. VQ and mipmapped inputs must be twiddled; mipmapped inputs must be
square. Strided, partial, paletted, bump-map, and YUV layouts are rejected.

Expected payload size is calculated independently from the header:

```text
base = width * height * 2
mip  = base * 4 / 3 + 6
vq   = ceil((base or mip) / 8) + codebook_entries * 8
file payload = round_up(selected_size, 32)
```

Native textures allocate exactly the stored payload size in VRAM and upload it
with `pvr_txr_load`. Reduced-codebook VQ keeps the allocation base for freeing
but supplies the PVR with `base - 2048 + codebook_bytes`, as specified by the
KOS `file_dctex` contract. Normal SDL textures continue to be explicitly
nontwiddled.

GLdc assumes a conventional 2048-byte VQ codebook at the texture base. The
Dreamcast-only OpenGL callback therefore expands a reduced codebook into a
temporary full-codebook upload, placing the stored entries at the high end so
the pvrtex index offset remains valid. For preformatted mip chains,
`SDL_SetTextureScaleMode` selects the corresponding nearest- or linear-mipmap
minification filter while retaining the requested magnification filter.

Loader-created `.dt` textures are static and immutable. This avoids exposing
ordinary linear pixel updates against twiddled or compressed storage.

## Hardware validation

`test/dreamcast/test/pvr_dt_texture` generates all assets with the active
`$(KOS_BASE)/utils/pvrtex/pvrtex`, builds native and OpenGL ELFs with
`-Wall -Wextra -Werror`, and tests six valid and six malformed cases. Twenty
load/destroy cycles pass; the native case restores the exact post-warmup VRAM
baseline after synchronized destruction.

Both final ELFs passed on physical Dreamcast hardware at 640x480 VGA on
2026-07-29. Each reported 15 checks and zero failures, wrote its unique raw
PPM, returned zero, and returned normally to dc-load-ip. Direct side-by-side
inspection shows corresponding complete panels for all three uncompressed
formats, full VQ, reduced-codebook VQ, and VQ mipmaps, including modulation,
alpha, filtering, flip, and rotation probes. Evidence and exact capture hashes
are recorded in the test README.

This increment does not change renderer capability advertising. Ordered
render-target support was subsequently promoted to the default native PVR
path in commit `916df8fbf`; this `.dt` loader remains independent of that
change. Strided NPOT renderer-created textures were added and hardware-
validated in the separate Phase 6.3 integration. Strided `.dt` payloads,
non-blocking DMA, list routing, and batching remain later work.
