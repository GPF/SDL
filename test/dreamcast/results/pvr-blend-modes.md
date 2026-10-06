# Native PVR `NONE` and `BLEND` hardware audit

**Result: PASS** — physical Dreamcast, 2026-10-06, 640×480 VGA.

The fixture ran through `tools/dc-load/kos-tool-locked.sh` with the shared
mGBADC console lock. It identified the renderer as `Dreamcast PVR`, passed
three warmed frames, and returned zero.

```text
pvr_blend renderer=Dreamcast PVR
pvr_blend setters=PASS NONE,BLEND for draw and texture
pvr_blend ADD_rejection=PASS state_preserved
pvr_blend screen_target_paths=PASS frames=3
Saved hardware screenshot to /pc/pvr_blend_modes.ppm
pvr_blend result=PASS (inspect screen/target panels)
Program returned 0
```

The left capture panel draws to the logical screen; the right panel renders to
an RGB565 target and samples it back to the screen. Each panel contains
untextured and textured rows for `NONE` and `BLEND`, each at alpha 0, 128, and
255. The textured row uses a white RGB565 source with color modulation
(255, 128, 64) over blue.

| Path | Mode | Alpha 0 | Alpha 128 | Alpha 255 |
|---|---|---|---|---|
| Untextured draw | `NONE` | (248, 0, 0) | (248, 0, 0) | (248, 0, 0) |
| Untextured draw | `BLEND` | (0, 0, 248) | (128, 0, 128) | (248, 0, 0) |
| Textured copy, modulated | `NONE` | (248, 128, 64) | (248, 128, 64) | (248, 128, 64) |
| Textured copy, modulated | `BLEND` | (0, 0, 248) | (128, 64, 160) | (248, 128, 64) |

The same values were observed in the screen and RGB565-target panels. Values
reflect RGB565 quantization. The result matches SDL3's RGB equations: `NONE`
replaces destination RGB independent of source alpha, and `BLEND` uses
`srcRGB * srcA + dstRGB * (1 - srcA)`. Destination alpha is not stored by the
renderer’s RGB565 screen or target surfaces. Color and alpha modulation were
carried in the queued vertex color for both textured and untextured geometry.

`SDL_SetRenderDrawBlendMode` and `SDL_SetTextureBlendMode` accepted `NONE` and
`BLEND`. The SDL3 PVR-specific core gate rejected `ADD` and preserved the
previous mode. No renderer or core changes were required by this audit. The
fixture passed a strict `-Wall -Wextra -Werror` compile. The capture is
[`pvr_blend_modes.ppm`](pvr_blend_modes.ppm).
