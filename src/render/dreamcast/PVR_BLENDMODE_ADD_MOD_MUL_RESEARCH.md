# Dreamcast Native PVR Renderer: ADD/MOD/MUL Blend Mode Feasibility Research

**Status:** ADD and MUL implemented and physically validated for the RGB565
screen path; MOD remains rejected.
**Date:** 2026-07-29
**Scope:** SDL blend equations, PVR hardware capabilities, destination-format contract, exact/multipass feasibility, cross-backend comparison, and recommended decision for ADD, MOD, and MUL.

---

## 0. Review-correction summary

An independent review identified material errors in the initial report. The corrections below change several conclusions:

1. **Destination format was not incorporated.** The validated screen path and initial RTT path are both RGB565 (no alpha channel). Alpha equations are therefore irrelevant for the current renderer. Screen-only and target semantics must be distinguished.

2. **The strict alpha criterion was inconsistent with BLEND.** Current PVR `BLEND` uses `SRCALPHA / INVSRCALPHA`, which would produce `outA = srcA*srcA + dstA*(1-srcA)` if alpha were stored. SDL's BLEND requires `outA = srcA + dstA*(1-srcA)`. The mapping is nevertheless correct for RGB565 destinations because no alpha is stored. ADD has the same RGB565-only qualification. MUL is different: its `DESTCOLOR / INVSRCALPHA` factors algebraically preserve destination alpha as well as matching RGB.

3. **The PVR has no `SRC_COLOR` factor.** Its factor enumeration contains `DESTCOLOR` and `INVDESTCOLOR` but not `SRCCOLOR`. SDL MOD requires destination factor `SRC_COLOR`. Missing `SRC_COLOR`, rather than shared color/alpha factors alone, is the immediate blocker for MOD.

4. **The PVR has a secondary accumulation buffer.** The header has `blend_dst_acc2` and `blend_src_acc2` bits, exposed as `dst_enable` and `src_enable` by the ordinary KOS polygon context. The secondary accumulator is a tile-local polygon-composition facility, not a modifier-volume-only feature and not a VRAM framebuffer. Whether it can implement MOD exactly is unresolved and requires a focused prototype.

5. **Multiple passes do not automatically require multiple scenes.** Consecutive polygons in the same autosort-disabled transparent list can implement per-command multipass operations while preserving painter order. Remove unsupported claims that every multipass requires scene boundaries.

6. **TA/list-capacity cost was overstated.** Do not claim "3× or more" without a concrete algorithm. Calculate header and vertex multiplication separately for each candidate.

7. **PSP analysis was incorrect.** The PSP comment says destination alpha is unchanged (`dstA is in fact un-changed`), which matches SDL MUL's requirement. The last two `sceGuBlendFunc` arguments are fixed colors used only with `GU_FIX`, not separate alpha factors. The source establishes an exact-looking RGB mapping and unchanged destination alpha, but it does not explain why the author called MUL "simplified."

8. **D3D9 qualification.** D3D9 supports separate color/alpha factors only when `enableSeparateAlphaBlend` is available. Its `SupportsBlendMode` rejects differing factors/operations otherwise.

9. **Renderer-wide capability semantics.** `SupportsBlendMode` receives a renderer and blend mode, not the eventual destination of every future draw. Phase 5 can deliberately support only RGB565 targets, making ADD coherent renderer-wide. MUL's candidate mapping is algebraically exact for RGBA too, but must still pass an alpha-bearing hardware target test before that broader claim is relied upon. Neither mode may be advertised until the focused physical-hardware gate passes.

---

## 1. Exact SDL equation table

### Source definitions

**File:** `include/SDL_blendmode.h` lines 68-72 (operations), lines 80-89 (factors), lines 45-55 (contract comments)
**File:** `src/render/SDL_render.c` lines 80-107 (composition macros), lines 97-107 (ADD/MOD/MUL defines)

### Algebraic form (per `include/SDL_blendmode.h` lines 45-55)

The SDL contract expresses blend as:

```c
dstRGB = colorOperation(srcRGB * srcColorFactor, dstRGB * dstColorFactor);
dstA   = alphaOperation(srcA   * srcAlphaFactor, dstA   * dstAlphaFactor);
```

| Mode | SrcColor | DstColor | ColorOp | SrcAlpha | DstAlpha | AlphaOp |
|------|----------|----------|---------|----------|----------|---------|
| NONE | ONE | ZERO | ADD | ONE | ZERO | ADD |
| BLEND | SRCALPHA | INVSRCALPHA | ADD | ONE | INVSRCALPHA | ADD |
| ADD | SRCALPHA | ONE | ADD | ZERO | ONE | ADD |
| MOD | ZERO | SRCCOLOR | ADD | ZERO | ONE | ADD |
| MUL | DSTCOLOR | INVSRCALPHA | ADD | ZERO | ONE | ADD |

### Expanded equations

| Mode | RGB equation | Alpha equation |
|------|--------------|----------------|
| NONE | `dst = src` | `dst = src` |
| BLEND | `dst = src*srcA + dst*(1-srcA)` | `dst = src + dst*(1-srcA)` |
| ADD | `dst = src*srcA + dst` | `dst = dst` |
| MOD | `dst = dst*srcC` | `dst = dst` |
| MUL | `dst = src*dstC + dst*(1-srcA)` | `dst = dst` |

**Key observation:** ADD, MOD, and MUL all require `dstA = dst` (destination alpha passthrough). Their RGB equations differ. For destinations without an alpha channel, the alpha equation is irrelevant.

---

## 2. PVR factor/operation/accumulator capability table

### Source definitions

**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_header.h` lines 158-166 (factor enum)
**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_header.h` lines 270-289 (mode2 structure)
**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr.h` lines 187-194 (context struct)
**File:** `/opt/toolchains/dc/kos/kernel/arch/dreamcast/hardware/pvr/pvr_prim.c` lines 24-95 (`pvr_poly_compile`), lines 427-430 (modifier-volume encoding)

### Available factors

```c
PVR_BLEND_ZERO, PVR_BLEND_ONE,
PVR_BLEND_DESTCOLOR, PVR_BLEND_INVDESTCOLOR,
PVR_BLEND_SRCALPHA, PVR_BLEND_INVSRCALPHA,
PVR_BLEND_DESTALPHA, PVR_BLEND_INVDESTALPHA
```

**Not available:** `PVR_BLEND_SRCCOLOR`. The PVR has no source-color factor. This is the immediate blocker for SDL MOD.

### Factor scope

The PVR header mode2 has a single `blend_src` and `blend_dst` pair (lines 289-290):

```c
pvr_blend_mode_t blend_dst :3; /* 28-26 */ /**< Blend mode for the background */
pvr_blend_mode_t blend_src :3; /* 31-29 */ /**< Blend mode for the foreground */
```

These factors apply to **both** color and alpha channels simultaneously. The PVR has no separate color/alpha factor configuration.

**Evidence:** `pvr_poly_compile` (`pvr_prim.c` lines 58-61):

```c
mode2 = FIELD_PREP(PVR_TA_PM2_SRCBLEND, src->blend.src)
    | FIELD_PREP(PVR_TA_PM2_DSTBLEND, src->blend.dst)
    | FIELD_PREP(PVR_TA_PM2_SRCENABLE, src->blend.src_enable)
    | FIELD_PREP(PVR_TA_PM2_DSTENABLE, src->blend.dst_enable)
    | FIELD_PREP(PVR_TA_PM2_FOG, src->gen.fog_type)
    | FIELD_PREP(PVR_TA_PM2_CLAMP, src->gen.color_clamp)
    | FIELD_PREP(PVR_TA_PM2_ALPHA, src->gen.alpha);
```

One factor pair. Applied to all channels.

### Available operations

The PVR only supports **ADD** (additive) blending. No subtract, min, max, or other operations are available. The blend equation is always `result = src*src_factor + dst*dst_factor`.

### Primary and secondary accumulation buffers

The PVR has two additional mode2 bits (lines 287-288):

```c
bool blend_dst_acc2 :1; /* 24 */ /**< Blend to the 2nd accumulation buffer */
bool blend_src_acc2 :1; /* 25 */ /**< Blend from the 2nd accumulation buffer */
```

KOS exposes those bits as `blend.dst_enable` and `blend.src_enable` on an
ordinary `pvr_poly_cxt_t`. `pvr_poly_compile` encodes them for ordinary
polygons whether or not `fmt.modifier` is enabled (`pvr_prim.c` lines 57-64).

The **Dreamcast/Dev.Box System Architecture**, section 3.4.6.1, describes two
tile-local accumulation buffers:

- polygons normally blend into the primary accumulation buffer;
- setting destination select writes a polygon result into the secondary
  accumulation buffer;
- setting source select lets a later flush polygon read the secondary result
  and combine it into the primary accumulator.

The documented uses include combining multiple polygons for translucent
trilinear filtering and bump-mapped-plus-textured effects. The secondary
accumulator is therefore a real same-tile composition intermediate. It is not
the screen or RTT framebuffer and is not inherently restricted to modifier
volumes.

`src2`, `dst2`, `src_enable2`, and `dst_enable2` are a different concept: they
are the alternate polygon parameters used **inside** a modifier volume.
`pvr_poly_compile_mod` emits an outside mode and an inside mode at
`pvr_prim.c` lines 423-433. Those fields must not be described as the
secondary accumulator's factors.

**Status:** Whether primary/secondary accumulator selection can synthesize
SDL MOD exactly is unresolved. The hardware provides an intermediate, but the
required polygon sequence, flush behavior, interaction with the transparent
list, source-color availability, and exact capacity cost need a focused
public-KOS prototype and physical-hardware validation.

---

## 3. Destination-format contract

### Current screen: 640×480 RGB565

The validated Dreamcast screen path is 640×480 RGB565. RGB565 has no alpha channel. The framebuffer stores only R, G, B (5+6+5 = 16 bits). Alpha is not stored.

**Implication:** For the screen path, the alpha equation is irrelevant. Only the RGB equation matters. A mapping is "exact" if it reproduces SDL's RGB equation correctly.

### Planned initial RTT: RGB565 (Phase 5)

KOS `pvr_scene_begin_rtt` uses a 16-bit render path. The RTT example (`pvr_rtt_sized`, `texture_render`) programs RTT with 16-bit pitch in 64-bit units (`pvr_scene.c` line 169):

```c
pvr_state.next_to_txr_rp = stride_px * 2 / 8;
```

KOS RTT examples describe and sample their result as RGB565. `texture_render.c` uses `PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED` for RTT output.

The KOS API itself specifies a 16-bit path rather than an SDL pixel-format
contract. In the current direct-video configuration it does not reprogram the
framebuffer pixel-mode register during RTT, and the local KOS examples create
and sample RGB565 targets. The recommended initial Phase 5 contract is
therefore RGB565-only; this still requires focused RTT validation when Phase 5
is implemented.

**Implication:** Under that planned Phase 5 contract, the alpha equation is
irrelevant for the initial target implementation.

### Hypothetical future alpha-bearing targets

An ARGB1555 or ARGB4444 target would store alpha. The alpha equation would then matter. A mapping that is exact for RGB565 would be incomplete for ARGB targets if it cannot reproduce SDL's alpha equation.

**Implication:** ADD requires Phase 5 to remain RGB565-only unless an exact
RGBA solution is found. MUL does not have the same algebraic restriction: its
candidate factors preserve destination alpha, but an alpha-bearing PVR target
would still require physical validation before being advertised.

### Consistent criterion

Apply the same destination-format reasoning to all modes:

- **For RGB565 destinations:** A mapping is exact if it reproduces SDL's RGB equation correctly. The alpha equation is irrelevant.
- **For ARGB destinations:** A mapping is exact if it reproduces BOTH SDL's RGB and alpha equations correctly.

This criterion is consistent with the existing BLEND implementation, which uses `SRCALPHA / INVSRCALPHA` for both color and alpha. If alpha were stored, this would produce `outA = srcA*srcA + dstA*(1-srcA)`, which differs from SDL's `outA = srcA + dstA*(1-srcA)`. But since RGB565 has no alpha, the discrepancy is invisible. The mapping is correct for the current destination.

---

## 4. ADD mode analysis

### SDL equation

```
RGB: dst = src*srcA + dst*1
A:   dst = dst
```

### PVR single-pass mapping

PVR computes: `dst = src*factor + dst*factor` (same factor for both color and alpha).

To match RGB: need `src_factor = SRCALPHA`, `dst_factor = ONE`.
To match A: need `src_factor = ZERO`, `dst_factor = ONE`.

**Shared-factor conflict:** Cannot use both `SRCALPHA` and `ZERO` for src_factor simultaneously.

### Destination-format analysis

**For RGB565 screen/target:** Alpha equation is irrelevant. Only RGB matters. The mapping `blend_src=SRCALPHA, blend_dst=ONE` reproduces SDL's RGB equation exactly:

```
PVR: dst = src*SRCALPHA + dst*ONE = src*srcA + dst
```

This matches SDL's `dst = src*srcA + dst*1`.

**For ARGB target:** Alpha equation matters. The mapping produces `outA = srcA*SRCALPHA + dstA*ONE = srcA*srcA + dstA`, which differs from SDL's `outA = dst` (alpha passthrough). The mapping is incomplete.

### Verdict

| Destination | Verdict | Reason |
|-------------|---------|--------|
| **RGB565 screen** | Exact single-pass | `SRCALPHA / ONE` matches SDL RGB equation; alpha irrelevant |
| **RGB565 target** | Exact single-pass | Same as screen |
| **ARGB target** | Incomplete | Shared factors cannot reproduce `dstA = dst` passthrough |

### Recommendation

**Prototype ADD for the current RGB565-only renderer.** The candidate changes are to accept `SDL_BLENDMODE_ADD` in `DC_PVR_SupportsBlendMode` and map it to `blend_src=SRCALPHA, blend_dst=ONE` in `DC_PVR_EmitHeader`. Do not treat the capability as validated or ready to advertise until focused physical-hardware tests confirm saturation, quantization, textured and vertex alpha, painter order, and recovery.

**Future target rule:** If Phase 5 adds alpha-bearing targets, accepting ADD as
a renderer-wide capability becomes invalid. A texture blend mode can be set
before the texture is drawn and then used against different targets, so a
check based only on the target active at setter time is not sufficient. Either
Phase 5 supports only RGB565 targets, or ADD remains rejected until a globally
exact RGBA implementation exists.

**Do not add ADD for ARGB targets.** The shared-factor conflict is fundamental.

---

## 5. MOD mode analysis

### SDL equation

```
RGB: dst = src*0 + dst*srcC = dst*srcC
A:   dst = dst
```

### PVR single-pass attempt

To match RGB: need `src_factor = ZERO`, `dst_factor = SRCCOLOR`.
To match A: need `src_factor = ZERO`, `dst_factor = ONE`.

**Two blockers:**

1. **Missing `SRCCOLOR` factor:** The PVR has no `PVR_BLEND_SRCCOLOR` factor. Its factor enumeration contains `DESTCOLOR` and `INVDESTCOLOR` but not `SRCCOLOR`. This is the immediate blocker. Even for RGB565, the PVR cannot multiply destination color by source color.

2. **Shared-factor conflict (secondary):** Cannot use both `SRCCOLOR` and `ONE` for dst_factor simultaneously. But this is secondary to the missing factor.

### Multipass attempt

**Hypothesis:** Use the tile-local secondary accumulator as an intermediate.

**Pass 1:** Compute `temp = dst*srcC` (color modulation) in secondary accumulator.
**Pass 2:** Flush `temp` back to primary accumulator as `dst`.

**Unknowns:**
1. SDL MOD needs the source color to multiply the existing destination. The
   PVR exposes no direct `SRCCOLOR` factor, so the prototype must show how the
   source color is retained or reconstructed across the accumulator sequence.
2. KOS exposes the source/destination accumulator-selection bits, but does not
   provide a documented high-level flush-polygon helper.
3. The exact same-list polygon sequence and its depth settings are not yet
   established.
4. The secondary accumulator is tile-local. Edge coverage, clipping, texture
   sampling, Gouraud color, and translucent painter order all need validation.

**Status:** **Unresolved and requires a focused prototype.** Do not declare MOD impossible on the basis that multipass is infeasible. The secondary accumulator exists, but its use for this purpose is unknown.

### Verdict

| Destination | Verdict | Reason |
|-------------|---------|--------|
| **RGB565 screen** | Reject (one-pass); unresolved (multipass) | Missing `SRCCOLOR` factor blocks one-pass; multipass feasibility unknown |
| **RGB565 target** | Reject (one-pass); unresolved (multipass) | Same |
| **ARGB target** | Reject | Same blockers, plus shared-factor alpha conflict |

### Recommendation

**Continue rejecting MOD.** The missing `SRCCOLOR` factor is a fundamental one-pass hardware limitation. Secondary-accumulator feasibility and cost are unknown. Do not prototype it unless application demand justifies a focused KOS/PVR hardware study.

---

## 6. MUL mode analysis

### SDL equation

```
RGB: dst = src*dstC + dst*(1-srcA)
A:   dst = dst
```

### PVR single-pass mapping

To match RGB, use `src_factor = DSTCOLOR` and
`dst_factor = INVSRCALPHA`.

For an alpha-bearing destination, the same component-wise factors produce:

```
outA = srcA*dstA + dstA*(1-srcA)
     = dstA*(srcA + 1-srcA)
     = dstA
```

This is exactly SDL MUL's destination-alpha passthrough. Unlike ADD and
BLEND, MUL does not require a different alpha-factor pair.

### Destination-format analysis

**For RGB565 screen/target:** Alpha equation is irrelevant. Only RGB matters. The mapping `blend_src=DSTCOLOR, blend_dst=INVSRCALPHA` reproduces SDL's RGB equation exactly:

```
PVR: dst = src*DSTCOLOR + dst*INVSRCALPHA = src*dstC + dst*(1-srcA)
```

This matches SDL's `dst = src*dstC + dst*(1-srcA)`.

**For ARGB target:** The factor algebra matches both RGB and alpha. Actual PVR
ARGB render-target storage, rounding, and factor behavior remain unvalidated,
so this is an exact candidate rather than a hardware-validated capability.

### Verdict

| Destination | Verdict | Reason |
|-------------|---------|--------|
| **RGB565 screen** | Exact single-pass | `DSTCOLOR / INVSRCALPHA` matches SDL RGB equation; alpha irrelevant |
| **RGB565 target** | Exact single-pass | Same as screen |
| **ARGB target** | Exact algebraic candidate | `srcA*dstA + dstA*(1-srcA) = dstA`; hardware target validation still required |

### Recommendation

**Prototype MUL for the current RGB565-only renderer.** The candidate changes are to accept `SDL_BLENDMODE_MUL` in `DC_PVR_SupportsBlendMode` and map it to `blend_src=DSTCOLOR, blend_dst=INVSRCALPHA` in `DC_PVR_EmitHeader`. Do not advertise it as validated until the same focused physical-hardware gate as ADD passes.

**Future target rule:** MUL does not force Phase 5 to remain RGB565-only, but
accepting an alpha-bearing target requires a separate hardware test proving
the PVR stores alpha and applies `DESTCOLOR` component-wise as expected.

---

## 7. Secondary accumulator and multipass feasibility

### What the secondary accumulator is

The PVR performs polygon composition in tile-local accumulation storage before
writing the completed tile to the screen or RTT framebuffer. It provides:

1. **Primary accumulator:** the normal tile-local composition destination.
2. **Secondary accumulator:** an alternate tile-local destination used to
   combine multiple polygon results before a later flush polygon reads the
   secondary result into the primary accumulator.

The ordinary mode2 bits select whether a polygon reads from or writes to the
secondary accumulator. KOS exposes them as `blend.src_enable` and
`blend.dst_enable`; despite the names, they encode source and destination
**accumulator selection**, not whether blending itself is enabled.

This facility is available in ordinary polygon headers. It is also usable in
the alternate inside-modifier mode, but it is not inherently a modifier-volume
feature. The modifier-volume fields `src2`/`dst2` are alternate blend factors
for pixels inside a volume, not secondary-accumulator storage.

### What it is not

The secondary accumulator is not addressable VRAM, a texture, or a persistent
full-frame render target. It is a specialized per-tile composition
intermediate. It can support documented multipolygon effects, but that does
not automatically make every arbitrary blend equation expressible.

### Can it implement MOD exactly?

**Unresolved.** The hardware bits exist (`blend_dst_acc2` and
`blend_src_acc2`, exposed as `dst_enable` and `src_enable`). Whether they can
be used to implement MOD exactly requires a focused prototype that studies:
1. How to flush from secondary to primary within one scene.
2. Whether consecutive polygons in one autosort-disabled transparent list can implement this.
3. Which depth comparison and write settings are required for the contributing
   and flush polygons.
4. TA/list-capacity cost.

**Do not declare multipass impossible.** The old report claimed multipass is infeasible due to missing intermediate storage. That claim is incorrect: the secondary accumulator exists. Whether it can implement MOD exactly is unknown.

### Multiple passes without multiple scenes

The old report claimed multipass requires multiple scenes, which would alter SDL painter order. That claim is unsupported.

**Single scene, multiple polygons:** Consecutive polygons in the same autosort-disabled transparent list preserve SDL painter order. A multipass algorithm that uses multiple polygons per SDL draw command can preserve order.

**Multiple PVR lists in one scene:** Multiple lists (opaque, translucent, modifier) can coexist in one scene. Their processing order is defined by the PVR hardware. For the current renderer, only `PVR_LIST_TR_POLY` is enabled. Adding modifier lists would require PVR reinitialization.

**Multiple scenes:** KOS can render an RTT scene and a screen scene during one
application frame, so a scene is not synonymous with a displayed frame.
Nevertheless, scene boundaries introduce target transitions and completion
ordering, and a screen-to-screen overlay cannot assume the previous scene's
tile-local depth or accumulator contents survive. They are unnecessary for a
same-tile secondary-accumulator experiment.

**RTT intermediates:** Render-to-texture can serve as an intermediate. This requires scene boundaries (one scene per RTT pass). Alters SDL ordering unless carefully designed.

### Capacity cost calculation

Do not claim "3× or more" without a concrete algorithm. Calculate separately:

No exact MOD accumulator sequence exists yet, so only conditional accounting
is valid. If a candidate requires two full geometry submissions with distinct
headers:

- standalone quad: one pass is `1 header + 6 vertices = 7` units; two passes
  are `2 headers + 12 vertices = 14` units, exactly 2×;
- standalone triangle: one pass is `1 + 3 = 4` units; two passes are
  `2 + 6 = 8` units, exactly 2×;
- a long single-state one-pass sequence can reuse one header, whereas
  alternating two multipass states may force two headers per SDL command. In
  that specific asymptotic comparison a quad approaches `14 / 6 = 2.33×`.

A required flush polygon would add its own header and vertices. Until its
shape and reuse rules are known, neither the exact multiplier nor the maximum
drawable count is established. The authoritative scene budget remains 8,192
32-byte units. Correctness feasibility and capacity acceptability are separate
decisions.

**Distinction:** Correctness feasibility (can the equation be reproduced?) is separate from performance acceptability (is the capacity cost acceptable?). Multipass may be correct but impractical for sprite-heavy workloads.

---

## 8. Renderer-wide capability implications

### `SupportsBlendMode` signature

**File:** `src/render/SDL_sysrender.h` (renderer struct), `src/render/SDL_render.c` lines 1271-1300 (`IsSupportedBlendMode`).

`SupportsBlendMode` receives a `SDL_Renderer *` and `SDL_BlendMode`, not a target format. The callback reports the renderer's capabilities, not per-target capabilities.

### Implication for ADD/MUL

If `DC_PVR_SupportsBlendMode` accepts a mode, it asserts that the renderer can
implement that mode for every destination it supports. A texture blend mode
may be set before any particular target is active and then used after target
switches. Checking only `renderer->target` when the setter runs would therefore
be insufficient.

- ADD can be accepted renderer-wide only while every destination is RGB565,
  unless a globally exact RGBA implementation is found.
- MUL's factor pair is algebraically exact for RGB and alpha. After current
  RGB565 validation, it can remain accepted if a later alpha-target increment
  independently validates the broader PVR behavior.

### Phase 5 target format lock-down

**Option A:** Phase 5 supports only RGB565 targets. ADD and MUL remain coherent renderer-wide after their RGB565 validation. Simple.

**Option B:** Phase 5 supports RGB565 and ARGB targets. ADD must be globally
rejected unless an exact RGBA path exists. MUL may remain supported only after
the alpha-target increment validates its exact factor algebra on hardware.

**Recommendation:** Option A for the initial Phase 5 increment. RGB565-only
targets match the current screen path and keep the first target contract
simple. This restriction is required by ADD; MUL alone would not require it.

### Future alpha targets

If future work adds ARGB targets:

1. ADD must be rejected or replaced by an exact RGBA implementation.
2. MUL must be tested for stored destination-alpha preservation, rounding, and
   sampling-after-rendering; its algebra predicts an exact result.
3. Existing BLEND must also be re-evaluated because its shared factors do not
   reproduce SDL's stored-alpha equation.

---

## 9. Corrected cross-backend comparison

### PSP (`src/render/psp/SDL_render_psp.c` lines 1010-1030)

```c
case SDL_BLENDMODE_ADD:
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0x00FFFFFF);
case SDL_BLENDMODE_MUL:
    /* FIXME SDL_BLENDMODE_MUL is simplified, and dstA is in fact un-changed.*/
    sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
```

**What this means:** The PSP's `sceGuBlendFunc` signature is
`(operation, source_factor, destination_factor, source_fixed_color,
destination_fixed_color)`. The final two values are fixed colors used only
when the corresponding factor is `GU_FIX`; they are not alpha factors.
This signature and parameter meaning are documented by the PSPSDK Graphics
Utility Library reference at
`https://pspdev.github.io/pspsdk/group__GU.html`.

For MUL, the selected RGB factors implement
`src*dstColor + dst*(1-srcAlpha)`, matching SDL's RGB equation. The source
comment says destination alpha is unchanged, which also matches current SDL's
alpha requirement. However, this source and the PSPSDK API documentation do
not explain why the author called the implementation "simplified."

**Acceptability:** PSP demonstrates the same useful RGB factor pair as the
PVR. It does not demonstrate separate alpha-factor support, and the unexplained
FIXME should not be turned into a stronger claim without PSP-specific hardware
evidence.

### PS2 (`src/render/ps2/SDL_render_ps2.c` lines 397-420)

```c
case SDL_BLENDMODE_MUL:
case SDL_BLENDMODE_MOD:
{
    /* We don't fully support MOD and MUL, however this is the best we can do */
    gsKit_set_primalpha(data->gsGlobal, GS_SETREG_ALPHA(A_COLOR_DEST, A_COLOR_NULL, A_ALPHA_SOURCE, A_COLOR_SOURCE, 0x80), 0);
}
```

**What this means:** The PS2 explicitly states "We don't fully support MOD and MUL". The GS blend expression selects color operands A, B, and D plus an alpha-derived multiplier C; this is not evidence of independently configurable output-alpha blending. The source comment is sufficient to classify the mapping as incomplete, but not to assign it a precise alpha failure without a separate GS hardware analysis.

**Acceptability:** The implementation's own comment warns that it does not fully support MOD/MUL. It is not a useful correctness precedent for the PVR.

### D3D9 (`src/render/direct3d/SDL_render_d3d.c` lines 397-418, 1040-1045)

```c
if (data->enableSeparateAlphaBlend) {
    IDirect3DDevice9_SetRenderState(data->device, D3DRS_SRCBLENDALPHA, ...);
    IDirect3DDevice9_SetRenderState(data->device, D3DRS_DESTBLENDALPHA, ...);
    IDirect3DDevice9_SetRenderState(data->device, D3DRS_BLENDOPALPHA, ...);
}
```

**What this means:** D3D9 supports separate color/alpha factors via `D3DRS_SEPARATEALPHABLENDENABLE`. The `enableSeparateAlphaBlend` flag is set if the hardware reports `D3DPMISCCAPS_SEPARATEALPHABLEND` (line 1682-1683).

**Capability check:** `D3D_SupportsBlendMode` (lines 397-418) rejects modes where `srcColorFactor != srcAlphaFactor` or `dstColorFactor != dstAlphaFactor` or `colorOperation != alphaOperation` when `enableSeparateAlphaBlend` is false.

**Acceptability:** D3D9's capability is hardware-dependent. Many D3D9 devices do not support separate alpha blend. The PVR lacks this hardware feature entirely.

### OpenGL (`src/render/opengl/SDL_render_gl.c` line 1654)

```c
data->glBlendFuncSeparate(GetBlendFunc(...), ..., GetBlendFunc(...), ...);
```

**What this means:** OpenGL's `glBlendFuncSeparate` provides independent color and alpha factor configuration. All SDL modes can be implemented exactly.

**Acceptability:** OpenGL's capability comes from API design. The PVR's API design shares factors.

### Lesson

- **PSP:** Exact-looking MUL RGB mapping and a source comment that destination alpha is unchanged; no separate alpha factors are supplied by this API call, and the FIXME remains unexplained.
- **PS2:** Approximate implementation. Acknowledged as incomplete. Not a useful precedent.
- **D3D9:** Hardware-dependent separate factors. Not all devices support it.
- **OpenGL:** API-level separate factors. Universal support.
- **PVR:** Shared factors. ADD is RGB565-only because its stored-alpha equation conflicts. MUL is algebraically exact for RGB and alpha because `srcA*dstA + dstA*(1-srcA) = dstA`, but alpha-target behavior remains physically unvalidated.

---

## 10. Verdict table

| Mode | RGB565 screen | RGB565 target | ARGB target | Recommendation |
|------|---------------|---------------|-------------|----------------|
| **ADD** | Physically validated (single-pass) | Exact candidate; RTT unvalidated | Incomplete | Accept for the current RGB565 screen path; retest when targets exist |
| **MOD** | Reject (one-pass); unresolved (multipass) | Reject (one-pass); unresolved (multipass) | Reject | Continue rejecting |
| **MUL** | Physically validated (single-pass) | Exact candidate; RTT unvalidated | Exact algebraic candidate; unvalidated | Accept for the current RGB565 screen path; retest when targets exist |

---

## 11. Focused implementation and physical-hardware validation

### ADD/MUL focused implementation increment (completed)

1. **Implement the candidate mappings:** Add ADD (`SRCALPHA / ONE`) and MUL (`DSTCOLOR / INVSRCALPHA`) to `DC_PVR_EmitHeader`, then accept them through `DC_PVR_SupportsBlendMode` so the focused public-API test exercises the real capability path.
2. **Keep the increment RGB565-only:** Do not add render targets or any alpha-bearing destination as part of this work. This validates the current renderer without prematurely claiming ARGB behavior.
3. **Focused test:** Create `test/dreamcast/test/pvr_blend_add_mul`:
   - Verify API accepts ADD/MUL via `SDL_SetRenderDrawBlendMode` and `SDL_SetTextureBlendMode`.
   - Draw additive sprites over opaque background, verify RGB matches software renderer.
   - Draw multiply-blended sprites, verify RGB matches software renderer.
   - Alternate NONE/BLEND/ADD/MUL draws, verify SDL painter order.
   - Textured and untextured draws, with vertex modulation.
   - Exercise saturation at low and high destination values and source alpha at 0, 1, intermediate, and 255.
   - Exercise RGB565 quantization boundaries and compare against the SDL software renderer with an explicitly defined tolerance where the two pipelines quantize at different stages.
4. **Physical hardware:** Validate on real Dreamcast at 640x480 VGA. Capture and directly inspect the framebuffer for each visually distinct test page. This gate passed on 2026-07-29 for the screen path; see the record below.

If a later phase adds an alpha-bearing target, add a separate MUL target page
that establishes destination-alpha preservation through readback or sampling.
The same page must demonstrate that ADD and the current BLEND mapping are not
silently treated as exact stored-alpha operations.

### MOD multipass prototype (optional, if application demand justifies)

1. **KOS/PVR hardware study:** Study whether `blend_dst_acc2`/`blend_src_acc2` (KOS `dst_enable`/`src_enable`) and a documented flush-polygon sequence can implement MOD exactly. Do not conflate these with modifier-volume `src2`/`dst2` factors.
2. **Focused prototype:** Implement a minimal MOD draw using the secondary accumulator.
3. **Validate:** Use physical hardware with a retained image for every distinct
   page. Measure TA/list capacity cost.
4. **Decision:** If feasible and capacity-acceptable, add MOD. Otherwise, continue rejecting.

**Do not prototype MOD without application demand.** The missing `SRCCOLOR` factor is a fundamental one-pass hardware limitation. Secondary-accumulator feasibility and cost remain unknown.

---

## 12. Remaining uncertainties

1. **Secondary accumulator MOD feasibility:** Unresolved. Requires focused KOS/PVR hardware study.
2. **Phase 5 target format lock-down:** The initial recommendation remains RGB565-only. If alpha targets are added, ADD and BLEND require new exact implementations or rejection; MUL requires hardware confirmation of its exact algebra.
3. **TA/list capacity for multipass:** If MOD multipass is prototyped, measure actual capacity impact. Do not assume 2.33× multiplier without measurement.
4. **Accumulator sequence and depth:** Establish the contributing and flush polygons, list requirements, and depth settings for any MOD prototype. Do not assume modifier volumes are required.
5. **PSP FIXME:** The inspected PSP API and renderer source do not explain why its MUL path is labeled "simplified." This does not affect the PVR mapping decision.

---

## 13. Conclusion

The initial report contained material errors regarding destination format, alpha criteria, PSP analysis, and multipass feasibility. The corrected analysis is:

- **ADD and MUL have exact single-pass RGB equations for RGB565 destinations.** ADD maps to `SRCALPHA / ONE`; MUL maps to `DSTCOLOR / INVSRCALPHA`. Their screen-path numeric behavior and draw-state interaction passed focused physical validation.
- **MUL's factor pair also preserves destination alpha algebraically.** ADD does not. MUL may therefore extend to a future ARGB target after a separate hardware target test, whereas ADD cannot be advertised for such a target without another implementation.
- **MOD is blocked by missing `SRCCOLOR` factor.** Even for RGB565, the PVR cannot multiply destination color by source color. Multipass feasibility via the secondary accumulator is unknown.
- **Multipass does not require multiple scenes.** Consecutive polygons in one autosort-disabled transparent list can implement per-command multipass while preserving painter order.
- **Console backend precedents are mixed.** PSP uses the same MUL RGB factors and reports unchanged destination alpha, although its "simplified" FIXME remains unexplained. PS2 explicitly approximates MOD/MUL. Do not copy PS2's approximation or infer unsupported PSP behavior.

**Recommendation:**

1. **Retain the physically validated ADD and MUL mappings for the current RGB565 screen path.** Revalidate them for render targets when Phase 5 adds that destination.
2. **Continue rejecting MOD** unless application demand justifies a focused multipass prototype.
3. **Phase 5 should initially lock target formats to RGB565 only.** This is required to preserve ADD and current BLEND semantics and keeps the first target increment bounded.
4. **Future alpha targets** require rejection or replacement of ADD/current BLEND and hardware validation of MUL's algebraically exact alpha result.

**Rationale:**

1. SDL's contract guarantees exact equations for all five modes. For RGB565 destinations, the alpha equation is irrelevant.
2. The PVR can reproduce ADD and MUL RGB equations exactly with single-pass mappings.
3. The missing `SRCCOLOR` factor makes MOD impossible in one pass. Multipass feasibility is unknown.
4. The validated ADD/MUL increment provides value to applications using the current RGB565-only renderer without requiring multipass work.
5. Rejecting unsupported modes preserves SDL's semantic contract: MOD remains rejected, ADD cannot accompany an ARGB destination without an exact replacement, and MUL must not claim ARGB support before target-specific hardware validation.

---

## 14. Physical-hardware validation record (2026-07-29)

`test/dreamcast/test/pvr_blend_add_mul` was built with
`-Wall -Wextra -Werror` against the explicit local native-PVR archive and run
on a real Dreamcast at 640x480 VGA through the BBA. The capture build presented
four distinct pages, waited three vertical blanks, and wrote one raw 640x480
PPM for each page before holding it for eight seconds. Serial output is retained
in `test/dreamcast/test/pvr_blend_add_mul/hardware-20260729-100523.log`; the
program printed `PASS`, returned zero, and returned control to `dc-load-ip`.

Direct inspection established:

- Page 1: all untextured ADD/MUL CPU-reference pairs for source alpha 0, 1,
  64, 128, 254, and 255 matched exactly or within one RGB565 channel step;
  saturation and guards were intact.
- Page 2: textured ADD/MUL reference and native cells matched exactly or
  within one RGB565 channel step at sampled corresponding texels; modulation,
  nearest/linear filtering, and guards were intact.
- Page 3: all state transitions rendered, the nonzero viewport was applied
  exactly once, and the overlapping ADD/MUL cluster preserved submission order.
- Page 4: single-submit ADD/MUL triangles interpolated smoothly; the textured
  ADD quad and RGB565 quantization probe appeared in their expected locations.
- No missing geometry, corruption, seam, guard leakage, freeze, reset, failed
  capture, or stale state was observed. MOD and the custom subtract mode stayed
  rejected through the public API.

Capture SHA-256 values:

- Page 1: `983bf27b2968f357a683ae4e68194edfc8ddcc85fafd9965356bd9ddc2c548c9`
- Page 2: `da6c1e8af380dea3c745bcae2a865417af2bac3b35bbcf4fb5b5069c694bcfb9`
- Page 3: `cf4109826619fe7346fdc6185a42f3dbe2a7cb7a98117c1ebc47cbc7ea3d0a2b`
- Page 4: `08515098cd3dd584ef4dbfea1ec1169a1104bc63ae3a28a170d4979229673f15`

This validates ADD and MUL for the current RGB565 screen destination. It does
not validate RGB565 RTT or any alpha-bearing target.
