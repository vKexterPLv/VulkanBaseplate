# Examples

Nine runnable examples in `example/`, each built from the menu in
`example/build.bat`.

## Layout

Every example follows the same three-file + `assets/` layout:

```
<ExampleName>/
    main.cpp       // 4 lines — calls VCK::<Example>::Run()
    App.h          // namespace + forward decls + window state
    App.cpp        // Init / DrawFrame / Shutdown / Run
    assets/        // .glsl source + .spv output
```

## Core reference (no FrameScheduler)

These exercise the raw `VulkanSync` / `VulkanCommand` path — use them to
understand the unscheduled lifecycle.

| # | Example         | Demonstrates |
|---|-----------------|--------------|
| 1 | `RGBTriangle`   | coloured triangle, live window resize |
| 2 | `MipmapExample` | texture upload + mipmap generation + sampling |
| 3 | `VMMExample`    | persistent triangle, transient animated quad, checkerboard texture, `LogStats` every 120 frames |

## VCKExpansion execution layer

All of these use `FrameScheduler`. Same triangle scene in most of them;
each one emphasises a different piece of the execution layer.

| # | Example                     | Demonstrates |
|---|-----------------------------|--------------|
| 4 | `HelloExample`              | smallest `FrameScheduler` program — `BeginFrame` / acquire / record / `QueueGraphics` / `EndFrame` / present |
| 5 | `JobGraphExample`           | CPU task graph per frame — `physics` ∥ `animate` → `build-cmds`, `audio` parallel, per-job timing logged every 60 frames |
| 6 | `SchedulerPolicyExample`    | press `1` / `2` / `3` to live-swap `Lockstep` / `Pipelined` / `AsyncMax`; title shows current policy, avg CPU frame µs logged every 120 frames |
| 7 | `SubmissionBatchingExample` | two independent cmd buffers per frame queued through `GpuSubmissionBatcher` → **one** `vkQueueSubmit` |
| 8 | `TimelineExample`           | `TimelineSemaphore::Initialize` probe + host `Signal` / `DependencyToken::WaitHost` round-trip (logs fallback when the device feature is off) |
| 9 | `DebugTimelineExample`      | `cfg.enableTimeline = true`, user `BeginCpuSpan` / `EndCpuSpan` / `NoteStall`, `scheduler.Timeline().Dump()` every 120 frames |

## Building

```
cd example
build.bat
```

Pick an example from the menu. See [[Build]](Build.md) for dependency setup.
