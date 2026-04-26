<div align="center">

# Examples &nbsp;·&nbsp; `example/build.bat`

Thirteen runnable examples, ordered from **raw Vulkan** to **fully VCK-assisted**.

</div>

---

The menu in `example/build.bat` (and the POSIX `build.sh`) descends from the
lowest-level "you write everything" lifecycle to the highest-level "VCK does
the boring parts" ergonomics. Each tier adds one layer of assistance.

---

## Layout

Every example follows the same three-file + `assets/` layout:

```
<ExampleName>/
    main.cpp       // 4 lines — calls VCK::<Example>::Run()
    App.h          // namespace + forward decls + window state
    App.cpp        // Init / DrawFrame / Shutdown / Run
    assets/        // .glsl source + .spv output
```

## Raw core (you write everything)

Hand-written `VulkanSync` / `VulkanCommand` lifecycle — use these to
understand what the higher tiers automate.

| # | Example               | Demonstrates |
|---|-----------------------|--------------|
| 1 | `RGBTriangle`         | coloured triangle, live window resize |
| 2 | `MipmapExample`       | texture upload + mipmap generation + sampling |
| 3 | `VMMExample`          | persistent triangle, transient animated quad, checkerboard texture, `LogStats` every 120 frames |
| 4 | `SecondaryCmdExample` | `VulkanCommand::AllocateSecondary / BeginSecondary / EndSecondary / ExecuteSecondaries` — per-slot secondary re-recorded each frame, primary opens the render pass with `VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS`; uses the scheduler-aware `HandleLiveResize` (v0.3) |

## Debug + tooling (opt-in instrumentation, still hand-records)

Same hand-written lifecycle, plus the debug / log surfaces. Zero cost when
disabled (rule 19).

| # | Example                 | Demonstrates |
|---|-------------------------|--------------|
| 5 | `DebugTimelineExample`  | `cfg.enableTimeline = true`, user `BeginCpuSpan` / `EndCpuSpan` / `NoteStall`, `scheduler.Timeline().Dump()` every 120 frames |
| 6 | `DebugShowcaseExample`  | every `VCKLog` level, `cfg.debug` gating, dedup, `VK_CHECK` fail-loud path (rule 14), GPU / driver / memory / surface dump — no draw loop, pure tour |

## Expansion (AA / framebuffer / sampler wiring)

Adds expansion-layer building blocks on top of the core path.

| # | Example             | Demonstrates |
|---|---------------------|--------------|
| 7 | `AAShowcaseExample` | `DetectRecommendedAA` decision matrix printed across `forward`×`motionVectors` combinations, live swapchain auto-pick reported via `GetAATechnique()` + `GetMSAASamples()`, RGB triangle drawn with the picked AA so sample-based edges are visible |

## Execution layer (`FrameScheduler` enters)

`FrameScheduler` + `JobGraph` + `GpuSubmissionBatcher` + `TimelineSemaphore`.
This tier is where v0.2 / v0.3 features really pay off.

| #  | Example                     | Demonstrates |
|----|-----------------------------|--------------|
|  8 | `JobGraphExample`           | CPU task graph per frame — `physics` ∥ `animate` → `build-cmds`, `audio` parallel, per-job timing logged every 60 frames |
|  9 | `SubmissionBatchingExample` | two independent cmd buffers per frame queued through `GpuSubmissionBatcher` → **one** `vkQueueSubmit` |
| 10 | `TimelineExample`           | `TimelineSemaphore::Initialize` probe + host `Signal` / `DependencyToken::WaitHost` round-trip (logs fallback when the device feature is off) |
| 11 | `SchedulerPolicyExample`    | press `1` / `2` / `3` to live-swap `Lockstep` / `Pipelined` / `AsyncMax`; title shows current policy, avg CPU frame µs logged every 120 frames |

## Mostly VCK (ergonomic API does the work)

`Primitives` / `VertexLayout` / `PushConstants` / `VCKMath` collapse what
takes ~40 lines of vertex tables in the raw tier into a few statements.

| #  | Example          | Demonstrates |
|----|------------------|--------------|
| 12 | `HelloExample`   | smallest `FrameScheduler` program — `BeginFrame` / acquire / record / `QueueGraphics` / `EndFrame` / present |
| 13 | `EasyCubeExample`| `Primitives::Cube()` + `VertexLayout` + `PushConstants` + `VCKMath` (`Perspective` / `LookAt`) — spinning cube in ~50 lines |

## Building

```
cd example
build.bat
```

Pick an example from the menu. See [Build](Build) for dependency setup.

---

### Why this order?

Reading top-down, each new example adds **one layer** of VCK assistance over
the previous tier — so by the time you finish `[13]`, you've seen every
abstraction VCK ships and exactly what each one buys you. Pick the lowest tier
that meets your needs; rule 9 (escape hatches) means you can always drop back.
