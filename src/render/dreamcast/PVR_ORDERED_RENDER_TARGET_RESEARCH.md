# Dreamcast Ordered Render-Target Research

> **Status (2026-09-13):** The guarded ordered RTT prototype described here
> was promoted to the default native PVR renderer path in commit
> `916df8fbf`. The ordering contract and design conclusions remain current,
> but the old compile-time guard and hint are retired. Physical validation,
> including VRAM rollback and recovery, passed at 640x480 VGA. Wider video
> modes and additional stress characterization remain hardening work.

## 1. Executive verdict

An ordered render-target mode is technically viable and materially cheaper than
the validated full-preservation prototype, provided it is exposed as an
explicit Dreamcast-specific contract rather than silently presented as fully
portable SDL render-target behavior.

The enforceable rule is:

> After the first screen clear or screen draw command in an SDL frame, changing
> from the screen to a texture target is an error until a successful
> screen-selected `SDL_RenderPresent()` establishes the next frame.

The renderer cannot prove that an application redraws every screen pixel. It
can only classify commands by destination and enforce their order. Selecting
the screen, changing viewport/clip/draw state, or flushing an empty/state-only
queue does not begin the final screen pass. A screen clear does begin it, as
does any screen geometry command, even if clipping, alpha, or coordinates make
the result invisible.

The promoted implementation has these properties:

- it is part of the native Dreamcast PVR renderer when that renderer is
  compiled in;
- the physical screen is rendered directly, with no paired logical-screen
  textures and no final full-screen preservation copy;
- it retains the validated paired allocation scheme for individual render
  targets initially;
- it rejects an illegal screen-to-target transition before SDL changes
  `renderer->target` or its associated viewport/clip state;
- it reports a clear SDL error and leaves the screen selected and usable;
- it treats a target-selected `SDL_RenderPresent()` as an error; and
- it advertises `SDL_RENDERER_TARGETTEXTURE`, with the ordering restriction
  documented as a Dreamcast extension.

This is not a recommendation to promote the expensive compatibility prototype
or to enable restricted target behavior by default. If this project requires
the standard capability flag to imply unrestricted screen -> target -> screen
ordering, ordered mode must not advertise that flag; only full-preservation
mode can meet that stronger interpretation.

Evidence labels used below:

- **Source-verified** means observed directly in the current SDL2, native PVR,
  or KOS source.
- **Hardware-verified** means already demonstrated on physical Dreamcast and
  recorded in the existing Phase 5 evidence.
- **Design conclusion** is an inference or recommendation based on that
  evidence, not current behavior.

The local evidence set inspected for this report includes
`src/render/SDL_render.c`, the three native PVR renderer source/data files,
`test/dreamcast/test/pvr_render_target/pvr_render_target.c`, its retained Phase
5 captures and README, the render-target stress and performance tests,
`PVR_RENDER_TARGET_PRESERVATION_RESEARCH.md`, the Phase 5 `AGENTS.md` record,
and the installed KOS `pvr/texture_render/texture_render.c` example.

## 2. Existing hardware and renderer constraints

### 2.1 Established physical-hardware results

The conclusions in
`PVR_RENDER_TARGET_PRESERVATION_RESEARCH.md`, the Phase 5 `AGENTS.md` sections,
and the existing render-target tests establish the following:

- **Hardware-verified:** Direct physical-screen rendering followed by an RTT
  scene and then another physical-screen scene does not preserve the earlier
  screen scene. Arbitrary screen -> target -> screen ordering therefore needs
  a sampleable logical-screen representation.
- **Hardware-verified:** Reselecting a target in a later PVR scene similarly
  requires preservation if its earlier contents must survive.
- **Hardware-verified:** The guarded full-compatibility prototype correctly
  preserves arbitrary transitions by rendering the logical 640x480 screen into
  paired RGB565 textures and copying the final logical screen to the physical
  framebuffer.
- **Hardware-verified:** At 640x480, that screen pair reduces available texture
  VRAM by about 2 MiB (recorded availability changes from approximately
  5,943,040 bytes to 3,845,800 bytes).
- **Hardware-verified:** Reserving the memory is not itself the observed
  performance cause. The slowdown comes from extra offscreen rendering and
  scene work, preservation copies, and the final full-screen copy.
- **Hardware-verified:** The performance test records essentially unchanged
  clear-only time, but substantial cost for normal drawing: approximately
  16.678 -> 22.236 ms for 100 rectangles, 16.678 -> 33.356 ms for 200 texture
  copies, and 33.361 -> 50.039 ms for the mixed workload.

These results make unconditional logical-screen preservation a poor default
for applications that never use targets or can obey Dreamcast-friendly order.

### 2.2 Native renderer ownership

**Source-verified:** The native backend owns PVR lifecycle, scenes, lists,
targets, synchronization, and presentation. Queue callbacks retain CPU-owned
command data; `RunCommandQueue` performs PVR submission. This is the correct
layer in which to classify the destination of executed commands.

The full prototype is guarded by `DC_PVR_RENDER_TARGET_PROTOTYPE`. In ordinary
builds the renderer neither installs `SetRenderTarget` nor advertises
`SDL_RENDERER_TARGETTEXTURE`.

In the prototype:

- a texture target has `vram` and `vram_next` allocations;
- the logical screen also has a current/next pair;
- `DC_PVR_SetRenderTarget()` finishes the active scene and changes
  `current_target`;
- a target scene renders to its next allocation and seeds it from the current
  allocation when preservation is required;
- finishing the scene swaps the target pair only after success; and
- `RenderPresent` copies the preserved logical screen to a new physical-screen
  scene.

Ordered mode can remove only the logical-screen pair and its presentation copy.
It does not automatically eliminate per-target preservation requirements.

### 2.3 Pure KOS ordering evidence

**Source-verified:** The KOS example
`examples/dreamcast/pvr/texture_render/texture_render.c` renders the RTT scene
first, finishes it, then begins a physical-screen scene which samples the
result. It does not render an earlier physical-screen scene that must survive
the RTT transition. This is the low-cost order the proposed mode formalizes:

1. finish all texture-target scenes;
2. begin one physical-screen scene;
3. draw the screen composition; and
4. present it.

The example proves that this order is natural for KOS/PVR. It does not prove
that unrestricted SDL ordering can be preserved without the compatibility
path.

## 3. SDL2 SetRenderTarget and queue/flush behavior

### 3.1 Current `SDL_SetRenderTarget` sequence

**Source-verified:** The current `SDL_SetRenderTarget()` in
`src/render/SDL_render.c` performs this sequence for a real change:

1. validate capability, ownership, access, and native-texture mapping;
2. return immediately if the requested target is already selected;
3. call `FlushRenderCommands(renderer)`;
4. lock the renderer and save screen state when leaving the screen;
5. assign `renderer->target = texture`;
6. call the backend `SetRenderTarget` callback;
7. update or restore viewport, clip, scale, and logical dimensions; and
8. queue the resulting viewport and clip commands.

Two details are decisive:

- the return from `FlushRenderCommands()` is currently ignored; and
- SDL assigns `renderer->target` before calling the backend callback.

If the current Dreamcast callback rejects an ordering violation, SDL has
already changed its target pointer. That callback does not restore the pointer
or all associated state. It is therefore not a safe enforcement point.

### 3.2 Where commands become distinguishable

**Source-verified:** A target switch flushes the old destination before SDL
changes targets. Consequently, one `RunCommandQueue` invocation belongs to a
single destination. The Dreamcast backend's `current_target` identifies that
destination while it walks the command chain.

Relevant commands divide into two classes:

| Class | Commands | Ordered-mode effect |
|---|---|---|
| State only | `SETVIEWPORT`, `SETCLIPRECT`, draw-color state | Do not begin a screen pass or require a PVR scene |
| Render intent | `CLEAR`, `GEOMETRY`, `DRAW_POINTS`, `FILL_RECTS` | On the screen, begin the final screen pass |

The backend leaves copy callbacks unset, so SDL converts copy operations into
geometry before execution. Zero-count operations which SDL discards before
queueing do not begin a pass.

The classification must be conservative. A queued screen draw counts even if
it is later fully clipped, offscreen, transparent, degenerate, or visually
redundant. Trying to prove visibility would make ordering depend on increasingly
complex rasterization analysis and still would not prove complete coverage.

### 3.3 Current eager scene creation

**Source-verified:** `DC_PVR_RunCommandQueue()` currently calls its frame/scene
begin path before walking the command chain. A queue containing only viewport
or clip changes can therefore begin a PVR scene.

Ordered mode requires lazy scene creation: begin a PVR scene/list only when the
queue walker reaches `CLEAR` or drawable geometry. This is necessary for the
valid sequence target A -> screen state changes -> target B. State-only screen
selection must not create and finish a blank physical-screen scene.

### 3.4 Explicit and implicit flushes

**Source-verified:** Queue execution is not synonymous with presentation.
Commands can be flushed by:

- `SDL_SetRenderTarget`;
- `SDL_RenderPresent`;
- `SDL_RenderFlush`;
- `SDL_RenderReadPixels` when supported;
- texture update, lock, or destruction when the queued generation references
  that texture;
- non-batched rendering; and
- some SDL internal renderer/window paths.

The window-event path can temporarily switch a selected target to the screen,
update logical/viewport state, and restore the saved target. This reinforces
the need to treat screen state-only work separately from screen rendering.

An implicit flush must execute and classify queued commands, but it must not
reset ordered-frame state. Only successful physical-screen presentation is the
ordered frame boundary.

### 3.5 Clears, readback, updates, and empty queues

- **Clear:** `SDL_RenderClear()` queues a real `CLEAR` command. A screen clear
  begins the final screen pass. This remains true even though a full clear is
  known to cover the target; its ordering, not its coverage, is relevant.
- **Viewport/clip changes:** These are state-only. They may be queued and
  flushed without beginning the screen pass.
- **Empty queues:** Flushing one changes neither ordered phase nor destination.
- **Texture update:** SDL may flush if the texture was referenced by queued
  commands. That flush does not present or reset the phase. The current native
  backend rejects CPU updates to target textures; ordinary source-texture
  replacement/deferred-free behavior remains independent of ordered mode.
- **Readback:** The native backend currently does not provide render readback.
  If added later, readback would flush but would not define a frame boundary.
- **Present:** `SDL_RenderPresent()` is a `void` API. SDL flushes, calls the
  backend, and invalidates normal backbuffer expectations. Its internal flush
  return is not delivered to the application.

## 4. Proposed ordered-mode state machine

### 4.1 Durable state

Use one ordered phase plus existing destination/scene state:

```text
ordered_phase:
    TARGETS_ALLOWED
    SCREEN_PASS_STARTED
    FAILED                 (existing fatal PVR failure may serve this role)

selected_destination:
    SCREEN
    TARGET(texture pointer)

active_scene_destination:
    NONE
    SCREEN
    TARGET(texture pointer)
```

`selected_destination` and active scene/list flags already have close backend
equivalents. `ordered_phase` is the new persistent fact. A separate durable
“queued screen draw” flag is unnecessary because every real target change
flushes first and `RunCommandQueue` performs classification.

### 4.2 Transitions

| Current condition | Event | Result |
|---|---|---|
| Renderer created | Initial state | Screen selected, `TARGETS_ALLOWED`, no scene |
| Any phase/destination | State-only command | No phase change; no scene begin |
| Target selected | Target clear/draw | Remain `TARGETS_ALLOWED`; lazily begin target scene |
| Screen selected, `TARGETS_ALLOWED` | Screen clear/draw executes | Set `SCREEN_PASS_STARTED`; lazily begin physical-screen scene |
| `TARGETS_ALLOWED` | Allowed target switch after checked flush | Finish old scene if necessary, select new destination |
| `SCREEN_PASS_STARTED`, screen selected | Select non-NULL target | Reject; keep screen selected and phase unchanged |
| Screen selected | Successful `SDL_RenderPresent` | Finish/present screen; reset to `TARGETS_ALLOWED` |
| Any phase | `SDL_RenderFlush` or implicit flush | Execute/classify only; no reset |
| Target selected | `SDL_RenderPresent` | Fail; finish target safely, do not present or reset |
| Any phase | Fatal PVR/scene failure | Enter/retain failed renderer state |
| Any phase | Renderer destruction | Finish/abort safely, synchronize, free; no artificial present |

The phase changes when a screen render-intent command is executed, not merely
when the screen is selected. This handles both batched and non-batched modes
and survives arbitrary implicit flushes.

### 4.3 What counts as the screen pass

The first executed screen `CLEAR`, `GEOMETRY`, `DRAW_POINTS`, or `FILL_RECTS`
begins the screen pass. Repeated selection of `NULL`, viewport changes, clip
changes, and draw-color changes do not.

The backend must not describe this as detecting a “complete screen redraw.” It
does not know whether all pixels were covered. The application is responsible
for composing whatever complete screen it needs after its target work. The
backend enforces only that no later target selection can invalidate already
submitted physical-screen work.

### 4.4 Flush without present

`SDL_RenderFlush()` is deliberately not a reset. For example:

```text
screen draw -> SDL_RenderFlush -> select target
```

must still fail. Treating a flush as a frame boundary would recreate the exact
content-loss bug ordered mode is intended to prevent.

### 4.5 Present and reset

Reset to `TARGETS_ALLOWED` only after the backend successfully completes a
screen-selected presentation. A failed presentation must retain the phase or
the renderer's fatal failure state.

A second `SDL_RenderPresent()` without intervening drawing is not an ordering
violation. SDL already documents the backbuffer as invalid after present, so
the visual result of presenting without rebuilding it must not be treated as
preserved content.

## 5. Enforcement point and error recovery

### 5.1 Rejected alternatives

#### Existing backend `SetRenderTarget` callback

This is too late. SDL has already assigned `renderer->target`; rejection leaves
SDL and backend destination state divergent. Restoring only the pointer would
also be insufficient because screen target state backup/restore and subsequent
viewport/clip queueing surround the callback.

#### `RunCommandQueue`

This is the correct place to classify executed commands, but not to reject a
future target switch. A failure there may occur during an implicit flush, and
the public target call may later mutate state unless its flush result is
checked. It also cannot retroactively make an already successful
`SDL_SetRenderTarget()` fail cleanly.

#### Beginning the target PVR scene

This is later still. SDL has accepted the switch and may have queued target
state or commands. Reporting failure then creates delayed, hard-to-discover
behavior and risks scene-state recovery.

#### Rejecting before flushing

Checking only the durable phase before flush misses a queued but not yet
executed screen draw. Scanning the queue independently would duplicate command
classification and would still need to handle a failing flush.

### 5.2 Recommended two-stage transition

**Design conclusion:** Add a Dreamcast-guarded internal pre-transition hook,
conceptually `PrepareRenderTarget`, and invoke it only after a checked flush but
before SDL mutates `renderer->target` or screen/target state.

The ordered path should be:

1. validate SDL-level target arguments as today;
2. return success for the same target as today;
3. call `FlushRenderCommands(renderer)` and propagate failure;
4. call the Dreamcast pre-transition hook;
5. if it rejects, return `-1` with no SDL target mutation;
6. perform SDL's existing target assignment and state backup/restore; and
7. run a no-fail backend commit callback for the prepared destination.

The pre-transition hook performs all failure-prone backend work:

- validate target format, dimensions, stride, allocations, and renderer
  ownership;
- check `SCREEN_PASS_STARTED` before a screen -> target transition;
- finish the old active scene for allowed transitions; and
- record the prepared destination.

The later backend callback should only assert/commit the prepared destination,
invalidate cached headers, and clear the prepared marker. It must not discover
a normal ordering or allocation error after SDL mutation.

This can be isolated in shared SDL code with the Dreamcast renderer build-time
guard and a nullable internal callback. Other renderers and platforms retain
their existing path. The checked-flush behavior should likewise be scoped to
this prepared transition unless a separate cross-platform SDL change is
intentionally undertaken.

### 5.3 Failure behavior

For an illegal transition, return `-1` and set a specific error such as:

```text
Dreamcast ordered render targets must be rendered before the screen pass;
present the screen before selecting a texture target
```

After failure:

- `SDL_GetRenderTarget(renderer)` remains `NULL`;
- backend `current_target` remains the screen;
- the physical-screen scene and queued/executed screen work remain valid;
- the application may continue drawing to the screen and present; and
- the next successful screen present resets the ordered phase.

Flushing pending screen work before rejecting is safe and expected:
`SDL_SetRenderTarget` already defines a flush boundary. The important property
is that the destination transition itself has not occurred.

### 5.4 Target-selected present

Ordered mode should reject `SDL_RenderPresent()` while a texture target is
selected. It should finish the target scene safely, set an SDL error, log a
clear Dreamcast error because the public function has no return value, perform
no physical presentation, and not reset the phase.

The recoverable application sequence is:

```text
SDL_SetRenderTarget(renderer, NULL);
draw_screen();
SDL_RenderPresent(renderer);
```

This is less discoverable than `SDL_SetRenderTarget` failure and must be
prominent in the mode documentation and tests.

## 6. SDL API compatibility and capability-advertising analysis

### 6.1 What SDL promises

**Source-verified:** `SDL_RENDERER_TARGETTEXTURE` is documented as the renderer
supporting rendering to texture. `SDL_SetRenderTarget()` is fallible and
returns a negative value with an SDL error. The public header does not state
that every possible interleaving must succeed.

However, the normal cross-platform programming model permits screen -> target
-> screen transitions. Nothing in the standard SDL documentation tells an
application that an earlier screen pass prevents a later target selection.
Therefore an unannounced ordered restriction would be surprising and
nonportable even if each rejection uses the API's error return.

Existing applications also cannot be assumed to handle the error well. The
SDL render-target test itself contains target switches whose return values are
not consistently checked. Many applications treat a capability check as the
main compatibility gate and assume later valid-target switches will work.

### 6.2 Honest advertisement

There are two defensible interpretations:

1. **Dreamcast extension interpretation:** An explicitly opted-in application
   has accepted the ordered contract. The renderer genuinely renders to
   textures, and individual transitions are fallible. Its renderer instance may
   advertise `SDL_RENDERER_TARGETTEXTURE`, provided documentation requires
   checking every `SDL_SetRenderTarget()` result.
2. **Strict portable-semantics interpretation:** Advertising the standard flag
   must imply arbitrary standard ordering. Under that policy ordered mode may
   not advertise the flag; only the full-preservation mode may do so.

The first interpretation is recommended only for an explicit Dreamcast mode.
Ordinary/default renderer information must continue to omit the flag. The
project should not describe ordered mode as transparently compatible with all
SDL target applications.

### 6.3 Opt-in mechanism

Use a renderer-creation-time Dreamcast hint, conceptually:

```text
SDL_HINT_DC_PVR_RENDER_TARGET_MODE = disabled | ordered | preserve
```

For the narrow next increment, only `disabled` and `ordered` need exist;
`preserve` may remain the existing separately guarded research prototype.

- `disabled` remains the default and exposes current ordinary behavior.
- `ordered` installs target callbacks, enables instance capability reporting,
  and enforces the state machine.
- a future explicit `preserve` value may select the validated costly path if
  that path is retained.

The hint must be read before renderer creation and fixed for the renderer's
lifetime. This matches existing Dreamcast architecture and avoids reallocating
screen state mid-frame. A separate renderer driver/name would duplicate driver
selection and lifecycle for what is fundamentally one PVR backend policy.

Dynamic instance capability creates a discovery limitation: static
`SDL_GetRenderDriverInfo()` cannot simultaneously report target support only
when a later hint is enabled. Ordered-mode applications should select the
Dreamcast PVR renderer explicitly, set the hint first, create it, then query the
created renderer's information. Any implementation must also test how
`SDL_CreateRenderer(..., SDL_RENDERER_TARGETTEXTURE)` filters static driver
flags before relying on that creation flag for automatic selection.

### 6.4 Unacceptable behavior

Silently finishing a physical-screen scene, running RTT, and continuing with a
new screen scene would discard earlier screen contents. That is silent visual
corruption and is unacceptable.

A warning-only mode is not sufficient: applications frequently ignore logs,
the rendered result can be plausible but wrong, and the lost content cannot be
reconstructed afterward.

## 7. Target preservation requirements

### 7.1 Screen preservation

Ordered mode needs no logical-screen texture pair. Screen commands render
directly into the physical framebuffer after all RTT work. Presentation
finishes that screen scene and synchronizes normally.

This removes:

- the unconditional approximately 2 MiB 640x480 screen pair;
- screen-current -> screen-next preservation work;
- offscreen logical-screen drawing; and
- the final logical-screen -> physical-screen copy.

Screen-only ordered-mode performance should consequently be very close to the
ordinary renderer. That is a hardware validation requirement, not an assumption
to record as a result before measurement.

### 7.2 Per-target preservation

Individual targets initially still need the validated current/next pair. The
ordered screen rule does not prevent:

- target A -> target B -> target A in one frame;
- partial drawing after target reselection;
- drawing to the same target across frames;
- target state being finished by an implicit flush and continued later; or
- an internal state-only screen excursion followed by restoration of the
  selected target.

A PVR scene does not automatically preserve a prior rendering of that target.
The current seed/copy and swap scheme is therefore still the compatibility
mechanism for target contents.

### 7.3 Possible later single-allocation target policy

A single allocation could be safe when every target activation begins with a
full target clear and the application never needs earlier contents. A leading
clear is one of the few operations whose full-target coverage can be known.

Requiring every target reselection to begin with a clear would add another
substantial, nonstandard SDL restriction. It would also need delayed scene
creation so the renderer can observe the first render command before choosing
preservation behavior. That optimization is not appropriate for the first
ordered prototype.

The narrow design should therefore spend VRAM only for target textures the
application creates, while retaining two allocations per target. It should not
spend a fixed screen pair for every renderer.

## 8. Policy comparison

| Policy | Correctness | Cost | API risk | Verdict |
|---|---|---|---|---|
| A. Strict ordered | Rejects target selection after screen pass begins | Direct screen; target pairs only | Nonportable restriction, manageable with explicit opt-in | **Recommended prototype** |
| B. Warning only | Earlier screen content can be lost | Low | Silent/ignored visual corruption | **Reject** |
| C. Full compatibility | Preserves arbitrary transitions | ~2 MiB fixed screen VRAM plus extra scenes/copies | Standard behavior | Valid explicit costly mode, not default |
| D. Automatic promotion | Cannot preserve already-rendered physical-screen contents | Potentially high and unpredictable | First illegal frame must fail or corrupt | **Reject mid-frame** |
| E. No SDL targets | Fully honest ordinary renderer | Lowest | Applications need custom KOS path | Keep as default/fallback |

### 8.1 Strict ordered mode

This is defensible because the restriction is deterministic, cheaply tracked,
and can fail before target mutation. It preserves direct framebuffer rendering
and gives Dreamcast-aware applications a standard texture-target drawing path.

### 8.2 Warning-only mode

This mode is too dangerous. A log cannot repair lost screen contents and does
not force the application to branch. It should not exist.

### 8.3 Full compatibility mode

The full prototype remains the only validated design for unrestricted SDL
ordering. It should remain guarded and explicitly selected if retained. Its
cost must not be imposed merely because target code is compiled in.

### 8.4 Automatic fallback or promotion

Mid-frame promotion is impossible after physical-screen commands have rendered:
there is no authoritative sampleable logical-screen texture from which to seed
compatibility mode. Reading the physical framebuffer back into a texture would
introduce a new costly synchronization/readback design and is not established
as a correct fallback.

Promotion on the next frame still means the triggering frame must fail or lose
content, changes VRAM allocation policy after creation, and creates
history-dependent performance. It may be useful as an explicit diagnostic
suggestion (“retry with preserve mode”), but not as automatic recovery.

### 8.5 No SDL target support

This remains the correct default. Applications that cannot accept ordered
failures or full-preservation cost can continue using custom Dreamcast/KOS RTT
APIs while ordinary SDL capability remains disabled.

## 9. Edge cases and lifecycle matrix

| Case | Ordered result | Required handling |
|---|---|---|
| Target A -> target B -> screen | Allowed | Finish/swap A, finish/swap B, then direct screen scene |
| Target A -> screen, no screen draw -> target B | Allowed | State-only screen commands must not begin a scene or screen phase |
| Screen clear -> target | Reject | Clear begins `SCREEN_PASS_STARTED`; leave screen selected |
| Screen geometry -> target | Reject | Flush executes geometry and records phase before pre-transition check |
| Screen viewport/clip/color only -> target | Allowed | Execute state only; lazy scene remains absent |
| Screen draw -> `SDL_RenderFlush` -> target | Reject | Flush is not a frame reset |
| Target-selected `SDL_RenderPresent` | Error | Finish target safely; no physical present, no phase reset; set and log error |
| Destroy selected target | Allowed cleanup | SDL first switches to screen; flush/finish target, then free/defer safely |
| Update selected/rendered target | Rejected by current backend | Target CPU updates remain unsupported; no ordered-state change |
| Update queued source texture | Existing implicit flush | Classify commands, retain ordered phase, preserve deferred VRAM rules |
| Target creation under VRAM pressure | Fail creation | Free partial allocations; do not change destination or ordered phase |
| Select same target again | Success/no-op | SDL currently returns before flush; no scene or phase change |
| `SetRenderTarget(NULL)` already on screen | Success/no-op | No flush or phase change |
| Screen -> target before any screen render command | Allowed | Checked state-only flush, then prepared transition |
| Multiple screen presents | Allowed | Each successful present resets; no-draw backbuffer result follows SDL invalidation rules |
| `SDL_RenderFlush` without present | Allowed, no reset | Screen phase persists if a screen draw executed |
| Renderer destroyed with target frame incomplete | Cleanup only | Finish/abort scene, wait as required, free; do not fabricate a screen present |
| Renderer destroyed with screen frame incomplete | Cleanup only | Existing safe scene/list shutdown; no ordered reset needed |
| Empty command queue during switch | Allowed according to phase | No scene begin; phase remains authoritative |
| Fully clipped/degenerate screen draw | Conservatively starts screen pass | Do not make ordering depend on visibility analysis |
| Internal window-event target -> screen -> target excursion | Allowed only if state-only | Lazy scene creation and per-target preservation handle restoration |
| Readback if implemented later | Flush, no reset | Read current destination; retain phase |
| Fatal PVR wait/scene error | Renderer failure | Do not permit a new target transition based on stale state |

Two cases deserve emphasis.

First, destruction of the currently selected target already routes through
`SDL_SetRenderTarget(renderer, NULL)`. That switch is from target to screen and
is valid in ordered mode; it must finish the target before its allocations are
released.

Second, repeated selection of the same target currently returns success before
the flush. Ordered mode should retain this behavior. It is not a new scene or a
reselection requiring preservation.

## 10. Recommended next increment

Implement one guarded research prototype of **strict, explicitly opted-in
ordered RTT**. Do not promote the full logical-screen path and do not change
ordinary renderer behavior.

### 10.1 Exact public behavior

1. The application sets the Dreamcast ordered-RTT hint before creating the
   native PVR renderer.
2. The created renderer instance advertises `SDL_RENDERER_TARGETTEXTURE`.
3. Texture targets remain RGB565 and use the already validated target
   allocation/preservation implementation.
4. Any number of target scenes may precede the screen pass.
5. The first executed screen clear/draw closes target selection for that frame.
6. A later non-NULL `SDL_SetRenderTarget()` returns `-1`, sets a precise error,
   and leaves both SDL and backend on the screen.
7. A successful screen-selected `SDL_RenderPresent()` starts the next ordered
   frame.
8. `SDL_RenderPresent()` with a target selected is a logged error and does not
   present or reset.
9. Applications must check every `SDL_SetRenderTarget()` return value.

### 10.2 Exact internal tracking

Add:

- an ordered-mode-enabled creation flag;
- `TARGETS_ALLOWED` / `SCREEN_PASS_STARTED` phase state;
- a prepared-target marker for the two-stage transition; and
- no new logical-screen allocations.

Retain:

- current target and active scene destination;
- existing scene/list/header-cache state;
- per-target current/next validity and swap state; and
- existing fatal PVR failure tracking.

Move PVR scene begin from the start of `RunCommandQueue` to the first clear or
drawable command. Set `SCREEN_PASS_STARTED` immediately before submitting the
first screen render-intent command.

### 10.3 Exact failure boundary

Add the guarded pre-mutation transition hook described in section 5. It runs
after a checked queue flush and before `renderer->target` assignment. The
existing post-assignment callback becomes a guaranteed commit for ordered mode.

No ordering violation may first be reported from target scene creation or
after SDL's target pointer changes.

### 10.4 Capability and guard decision

- Keep the ordinary renderer and static default information without
  `SDL_RENDERER_TARGETTEXTURE`.
- Advertise the flag only for a successfully created, explicitly opted-in
  ordered renderer instance.
- Require both compile-time Dreamcast isolation and the runtime hint.
- Keep full compatibility separately guarded; do not copy its logical-screen
  allocations or final-copy behavior into ordered mode.
- If maintainers reject constrained standard-flag advertising even with
  explicit opt-in, retain the same prototype behind a custom Dreamcast API and
  leave `SDL_RENDERER_TARGETTEXTURE` disabled. That is an API policy decision,
  not a PVR feasibility failure.

## 11. Required physical-hardware validation plan

All runtime evidence must come from a physical Dreamcast. Serial `PASS` output
alone is insufficient; capture and directly inspect every visually distinct
page through `/pc` where possible. Let the program return normally to
`dc-load-ip`.

The smallest useful matrix is:

1. **Allowed multi-target composition:** Render A, render B, select screen,
   sample both in the final screen scene, and present. Capture both target
   patterns together.
2. **State-only screen excursion:** Render A, select screen, issue/flush
   viewport and clip state only, select B, then compose the screen. Confirm no
   blank physical scene and correct A/B output.
3. **Clear rejection and recovery:** Queue a visible screen clear/marker, try
   selecting A, verify `-1`, error text, `SDL_GetRenderTarget()==NULL`, then
   continue screen drawing and present. Capture the pre- and post-rejection
   markers together.
4. **Explicit/implicit flush persistence:** Draw to screen, call
   `SDL_RenderFlush`, verify target selection still fails; present; verify the
   same target selection succeeds in the next frame. Repeat with batching
   disabled to exercise implicit per-command flushes.
5. **Target reselection preservation:** A -> B -> A with partial additions,
   then screen composition. Confirm A retains its earlier contents and receives
   the later addition. Also cover A -> screen state-only -> A.
6. **Target-selected present and lifecycle recovery:** Present while A is
   selected, verify logged/error state and no false physical present, recover
   through screen selection and a valid presentation, then destroy a selected
   target safely.
7. **Allocation rollback:** Force second-allocation failure for a target, verify
   no VRAM leak or destination/phase change, then create and render a smaller
   target successfully.
8. **Cost gate:** Compare ordinary and ordered builds on screen-only clear,
   rectangle, texture-copy, and mixed workloads. Verify ordered idle creation
   does not reserve the 2 MiB screen pair and does not reproduce the full
   prototype's extra scene/copy cost.

The test should additionally assert, without requiring separate visual pages:

- repeated selection of the same target;
- repeated `SetRenderTarget(NULL)`;
- an empty queue switch;
- multiple presents;
- flush without present;
- selected-target destruction; and
- clean return after incomplete target and screen frames.

Linker/map inspection must confirm that the opt-out build has no ordered RTT
symbols or target capability, and that any change in shared SDL renderer code
is compiled only under the Dreamcast PVR guard. Strict-warning builds remain
required.

## 12. Remaining uncertainties

- SDL maintainers may consider any ordering-restricted implementation
  incompatible with advertising the standard target flag, even when it is an
  explicit platform opt-in. If so, use a custom Dreamcast API or retain no SDL
  target support; do not weaken enforcement.
- The precise guarded internal-hook shape needs implementation review against
  all renderer creation paths. In particular, dynamic instance flags interact
  with static driver filtering when an application requests
  `SDL_RENDERER_TARGETTEXTURE` during automatic renderer selection.
- `SDL_RenderPresent()` cannot return the target-selected error to the caller.
  Setting and logging an error is the best available enforcement, but its API
  ergonomics remain weaker than `SDL_SetRenderTarget()`.
- Lazy scene creation for state-only queues is source-justified but still needs
  physical-hardware validation across target switches and window-event state
  restoration.
- Per-target paired preservation retains nontrivial VRAM and copy cost. Its
  cost should be measured independently from the removed logical-screen path.
- A future leading-clear/single-allocation target optimization is plausible but
  adds a second restriction and is not established by this research.
- Readback remains unsupported, and future implementation must define its
  interaction without treating it as presentation.
- Wider video modes, target dimensions/strides, repeated lifecycle stress, and
  TA/list capacity remain hardware-hardening work.
- Presenting with no screen draw follows SDL's invalid-backbuffer contract, but
  the exact physical result should not be used as preservation evidence.

**GO** for a guarded, explicit-opt-in strict ordered-mode prototype with
pre-mutation rejection, lazy scene creation, direct physical-screen rendering,
and per-target preservation. **NO-GO** for unconditional/default advertisement,
warning-only behavior, silent content loss, or automatic mid-frame promotion.
