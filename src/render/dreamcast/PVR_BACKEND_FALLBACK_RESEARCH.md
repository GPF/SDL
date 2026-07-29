# Dreamcast Native PVR Renderer: Cross-Backend Fallback Research

**Status:** Research complete. No renderer behavior changed.
**Date:** 2026-07-29
**Scope:** SDL renderer-core fallbacks, representative accelerated and console
backends, unsupported-feature policy, resource synchronization, and techniques
that may be useful to the native Dreamcast PVR renderer.

This document answers a forward-looking question: when the PVR cannot directly
provide an SDL renderer feature, which established SDL techniques can preserve
the feature's semantics, and which apparent shortcuts should be avoided?

The audit covers `SDL_render.c`, the software renderer, OpenGL/GLES, Direct3D
9/11/12, Metal, PSP, PS2, and Vita GXM. These are behavioral and architectural
references only. The Dreamcast backend remains independently owned and uses
public KallistiOS/PVR interfaces.

## 1. Primary conclusion

Use this order when the native PVR lacks a direct feature:

1. Let SDL's renderer core lower the operation to an already correct primitive.
2. Implement an exact CPU conversion, expansion, clipping, or staging path.
3. Implement a measured native specialization when it preserves SDL semantics.
4. Reject the feature clearly if no exact implementation exists.

Do not advertise a capability backed by a stub, and do not silently approximate
an SDL operation whose observable result differs. The PS2 and PSP renderers
contain useful platform techniques, but their approximate blend mappings and
the PS2 target callback are cautionary examples rather than precedents.

## 2. Fallbacks already supplied by SDL

The most valuable portability layer is `src/render/SDL_render.c` itself. A
backend should omit a specialized callback when the core's geometry path is
correct and adequate.

### 2.1 Rectangle and texture-copy lowering

- If `QueueFillRects` is absent, SDL expands each rectangle to four vertices
  and six indices, then calls `QueueGeometry`.
- If `QueueCopy` is absent, `SDL_RenderCopyF` creates textured quad geometry.
- If `QueueCopyEx` is absent but `QueueGeometry` exists, SDL creates the
  rotated/flipped textured quad, including the requested center and scale.
- Arbitrary `SDL_RenderGeometry` has no lower-level fallback. A backend that
  advertises accelerated geometry must implement `QueueGeometry`.

The native PVR renderer's geometry-first design therefore gets rectangle,
copy, rotation, and flip semantics without duplicating them. Its specialized
point and fill callbacks are justified only where they reduce queue traffic or
preserve the already researched point/line behavior.

### 2.2 Unsupported texture formats and YUV

When the requested texture format is not advertised by a backend, SDL can
create a closest-format native texture and retain it as `texture->native`.
Updates and locks convert pixels into that texture. Unsupported YUV formats can
similarly use SDL's software YUV texture and convert into a supported RGB
native texture.

This means the PVR renderer does not need to advertise RGB888, BGR888, or YUV
merely to accept common application input. Keeping the native list restricted
to formats actually validated on the PVR is the safer design. Direct YUV or
additional Dreamcast formats should be added only when they provide measured
value and have their own correctness tests.

### 2.3 Synchronization supplied by the core

Before an application updates, locks, destroys, or otherwise accesses a
texture whose queued commands may still reference it, SDL calls
`FlushRenderCommandsIfTextureNeeded`. That protects the CPU command queue, but
it does not by itself prove that submitted GPU work has completed.

The PVR backend must continue to own the second half of this contract: scene
completion, immutable VRAM for the active scene, and deferred destruction. Its
current replacement/deferred-VRAM path is analogous to the in-flight resource
handling in explicit APIs such as D3D12.

### 2.4 Features with no core emulation

SDL does not emulate these through another renderer callback:

- render targets;
- framebuffer readback; and
- arbitrary blend modes.

They require a real backend implementation or an explicit unsupported result.
Using the SDL software renderer internally as a hidden second renderer would
also violate the Dreamcast single-renderer architecture and should not be used
as a general fallback.

## 3. Useful patterns from other backends

### 3.1 CPU staging and pitch repair

GLES repacks a pitched update into a temporary tightly packed buffer because
GLES 1 lacks OpenGL's row-length unpack facility. D3D9, D3D11, D3D12, PS2,
Vita, and the software renderer likewise copy row by row when source and
destination pitches differ. Several keep a CPU staging allocation for locks
and upload or copy from it on unlock.

**Dreamcast use:** retain CPU staging as the authoritative texture copy and
perform explicit pitch-aware row copies. This already supports POT padding,
partial logical updates, safe streaming locks, format conversion by SDL, and
future VRAM restoration. It is a deliberate portability mechanism, not merely
temporary Phase 3 scaffolding.

### 3.2 Logical size separated from physical backing

OpenGL/GLES and PSP fall back from unsupported NPOT layouts to POT backing and
store UV scale separately. Vita similarly records allocation dimensions and a
width scale.

**Dreamcast use:** the existing logical/backing dimensions and UV scaling are
the established cross-backend solution. Future twiddled, VQ, or stride-aware
layouts should preserve this separation instead of exposing allocation shape
to SDL callers.

### 3.3 Lazy work and state caches

Metal's `SetRenderTarget` ends the old pass but delays opening the next render
pass until a clear or draw occurs. That permits a hardware load-action clear.
GL, D3D, Metal, PSP, Vita, and the PVR backend cache draw state so expensive
headers, pipelines, shaders, or API calls are emitted only on a relevant
change. PSP also delays swizzling suitable static textures until activation.

**Dreamcast use:** continue compiling a PVR polygon header only when texture,
filtering, blend, or other header identity changes. For future targets, make
the scene manager record a target transition and begin the required PVR work
at the next ordered operation when that is semantically safe. Lazy twiddling
or VQ preparation is plausible for static textures, but must be benchmarked
against SH-4 CPU cost and must never mutate storage referenced by an active
scene.

### 3.4 Finite validated blend implementations

GL/GLES and D3D validate every source factor, destination factor, and operation
before accepting a custom blend mode. D3D9 additionally rejects separate alpha
behavior on devices without that capability. Vita rejects custom modes because
it prebuilds only a finite set of fragment programs.

**Dreamcast use:** treat PVR blend headers as a finite set of validated native
programs. Keep `NONE` and `BLEND`; add another mode only after an exact mapping
or a correct multipass algorithm passes order, color, alpha, capacity, Flycast,
and physical-hardware tests. If PVR cannot express separate color and alpha
equations exactly, reject the mode.

PSP and PS2 approximate some standard modes; PS2 explicitly notes that `MOD`
and `MUL` are not fully supported. Copying this behavior would turn a known
platform limit into silent rendering error.

### 3.5 Resource loss, restoration, and VRAM pressure

D3D9 preserves non-target texture contents in system-memory staging across a
device reset, recreates the GPU allocations, marks them dirty, and reuploads
them. Render-target contents are not preserved; SDL emits a reset event so the
application can recreate them. D3D11/12 similarly rebuild device resources
and notify the application.

PSP maintains an LRU list of render targets. Under VRAM pressure it spills the
least recently used target to system RAM and promotes it later, translating
swizzled versus renderable layouts as necessary.

**Dreamcast use:** CPU texture staging gives the backend enough information to
recreate ordinary VRAM allocations if a future lifecycle transition requires
it. A PSP-style LRU is an interesting future option for render targets or
large texture sets, but it is high complexity on a 16 MiB system and 8 MiB
VRAM machine. It requires explicit ownership, allocation-failure tests,
content-validity rules, and scene-completion synchronization. It should not be
part of the first render-target implementation.

### 3.6 In-flight lifetime management

D3D12 issues queued GPU work before freeing a texture because destruction may
occur while commands are in flight. Metal ends/commits encoders before resource
transitions or readback. Vita explicitly finishes GXM before exposing pixels
of a render-target texture.

**Dreamcast use:** preserve the current rule that submitted texture VRAM is
immutable through scene completion. Deferred texture destruction and
copy-on-update during an active scene are appropriate native equivalents. A
future allocator or target system must use the same fence boundary rather than
freeing, relocating, or overwriting referenced VRAM early.

### 3.7 Readback through a canonical temporary format

Metal and Vita read into a backend-native temporary buffer, synchronize the
GPU, correct orientation when needed, and call `SDL_ConvertPixels` into the
caller's requested format. The software renderer also delegates final format
conversion to `SDL_ConvertPixels`.

**Dreamcast use:** if `RenderReadPixels` is implemented, read a defined PVR
framebuffer or target representation only after the necessary completion wait,
normalize row orientation, and use `SDL_ConvertPixels`. Do not create one
readback implementation per requested SDL format. Validate screen and target
readback separately; Vita's screen-only implementation shows that partial
capability should return unsupported for the missing case.

### 3.8 Render-target transitions are lifecycle operations

Metal ends the prior encoder and defers the next pass; D3D binds a real target
surface and invalidates cached viewport/clip/draw state. Mature implementations
also distinguish target contents that survive a reset from those that do not.

**Dreamcast use:** a future target callback must hand the transition to the
single renderer scene manager. It must define synchronization, target texture
layout, sampling-after-rendering, viewport restoration, clip restoration,
clear ordering, and screen presentation. Merely storing `renderer->target` is
not an implementation.

The PS2 renderer assigns `SetRenderTarget` to a callback that returns success
without changing a target. That is precisely the kind of advertised stub the
native PVR plan forbids.

## 4. Recommended decision matrix

| Missing capability | Preferred PVR response | Priority |
|---|---|---|
| Copy, rotation, flip | Keep SDL geometry lowering | Already active |
| Unsupported RGB input format | SDL native-texture conversion | Already active |
| Unsupported YUV input | SDL software YUV conversion to a validated native format | On demand |
| Source pitch unsupported by transfer API | Row-copy into CPU staging | Already active |
| NPOT texture allocation | POT backing plus logical UV scale | Already active |
| Points/default lines | Exact native queue expansion through clipped triangles | Already active |
| Custom or non-exact blend | Reject; research exact or multipass implementation separately | Required policy |
| Render target | Real scene-manager implementation or do not advertise | Phase 5 |
| Readback | Synchronized canonical buffer plus `SDL_ConvertPixels` | Later phase |
| VRAM pressure | Fail clearly first; consider measured spill/LRU only later | Optimization |
| Twiddle/VQ conversion cost | Consider lazy conversion for suitable static textures | Optimization |
| Texture mutation during scene | Replacement VRAM plus deferred release | Already active |
| Device/PVR reinitialization | Recreate from CPU staging and define reset notification semantics | Hardening |

## 5. Techniques worth prototyping later

### 5.1 Queue-capacity-aware chunking

Several explicit backends batch work and flush when finite command or upload
storage is exhausted. The PVR capacity test now supplies physical limits for
the ordinary workload range. A later optimization can place an explicit
high-water mark on SDL queue storage or TA submission, but splitting one SDL
present into multiple PVR scenes would change visibility and ordering unless
designed carefully. Prefer bounded CPU command storage and documented failure
over an invisible semantic change.

### 5.2 Static-texture layout promotion

PSP's lazy swizzle suggests maintaining a linear CPU source and promoting a
static texture to a sampling-friendly native layout after updates settle. On
Dreamcast this could mean twiddled or VQ storage. Promotion needs format- and
size-specific benchmarks, exact UV/filter tests, and an invalidation path back
to the authoritative staging copy after any update.

### 5.3 Specialized fast paths behind the geometry contract

PSP implements specialized fill/copy callbacks, while SDL's generic lowering
keeps semantics centralized. The PVR renderer can add a specialized callback
only when profiling shows meaningful savings and the produced command-owned
data is equivalent to the geometry path. The generic path should remain the
reference oracle in focused tests.

### 5.4 Target spill only after a correct baseline

PSP's target LRU could eventually make better use of limited VRAM, especially
for inactive targets. First establish one correct target with explicit scene
transitions and sampling. Then measure allocation failure and switching costs;
only those results can justify spill, compression, or layout conversion.

## 6. Explicit non-recommendations

- Do not route a missing native feature through GLdc or private GLdc state.
- Do not instantiate a hidden software renderer to execute selected commands.
- Do not advertise target textures or readback before the callbacks perform
  the full operation.
- Do not accept a blend mode with an approximate color or alpha equation.
- Do not mutate or relocate texture VRAM referenced by an unfinished scene.
- Do not use Flycast success as a substitute for physical Dreamcast results.
- Do not add a complex VRAM LRU before ordinary allocation failure is measured
  and the basic target lifecycle is correct.

## 7. Suggested validation method for future fallbacks

For each new fallback, compare three results where applicable:

1. SDL software renderer as the semantic reference.
2. The native PVR implementation in Flycast as a diagnostic convenience.
3. Physical Dreamcast output as the authoritative result, with a captured
   hardware image for each visually distinct test page or workload.

Tests should force the actual fallback path, verify its public API result,
exercise painter order and recovery after failure, and include a negative test
showing that an unavailable path is rejected. Capacity and performance must be
measured separately from visual correctness.

## 8. Recommended next research increments

In priority order:

1. Keep Phase 2/3 work using core geometry and native-texture conversion.
2. Before Phase 5, write a focused PVR render-target lifecycle study covering
   target layout, scene boundaries, sampling hazards, and VRAM ownership.
3. Research `RenderReadPixels` independently; screen capture and target
   readback need not ship together.
4. Only if application demand appears, evaluate exact `ADD`, `MOD`, or `MUL`
   mappings or multipass costs. Continue rejecting them meanwhile.
5. After texture/target workloads expose real VRAM pressure, benchmark lazy
   twiddling/VQ and PSP-style spilling rather than adopting either by analogy.

The general lesson from the other backends is not to reproduce every native
API feature. It is to keep SDL semantics in the core where possible, use CPU
work as an exact compatibility layer when affordable, and make hardware limits
explicit everywhere else.
