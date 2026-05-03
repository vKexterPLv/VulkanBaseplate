<div align="center">

# Performance baseline

Anchors, not targets · re-run any time · `docs/perf_baseline.sh`

</div>

---

This page is the v0.5 baseline. Every value here is an anchor for
future work, not a target. v0.6 performance changes (async staging
ring, render-graph barrier batching, descriptor-buffer adoption) need
a starting line; this page is it.

The script that produces these numbers is `docs/perf_baseline.sh`.
Run it on a fixed test machine (ideally the same one each time), paste
the output into the **Latest measurement** section below, and date the
revision.

---

## How to read this document

There are five anchor metrics. They are picked because they are cheap
to measure, hard to game, and each one corresponds to a real renderer
question the user has to answer when picking a graphics framework.

| Anchor | What it answers | Where it lives |
|---|---|---|
| **A1 · Peak RSS, init** | "How much memory does VCK eat to draw a triangle?" | `RGBTriangle` in the first 16 frames |
| **A2 · Peak RSS, steady** | "Does it leak?" | `RGBTriangle` after 1000 frames |
| **A3 · Frame time** | "Can I hit 144 Hz at 1280×720 on this GPU?" | `RGBTriangle` median over 1000 frames, V-sync off, `framesInFlight = 2` |
| **A4 · VMM staging throughput** | "How fast can I stream textures?" | `VMMExample` total bytes uploaded / wall time, ring at max load |
| **A5 · Hot-reload latency** | "How long from `:wq` to seeing the change?" | `ShaderToolingExample` time from `.spv` `mtime` change to first redrawn frame |

A1+A2 do not require a GPU — `vck_tests` exercises Initialize/Shutdown
on the R14 paths and produces the same RSS profile in CI. A3+A4+A5
require a Vulkan-capable display and so are produced on a developer
machine, not in CI.

---

## Latest measurement

> Last updated: **2026-04-22**, commit `e79c010` (post-PF1/PF3/PF5 merge).
> Headless CI baseline — static + R14-harness anchors only. GPU anchors
> (A3 / A4 / A5) need a developer machine with a Vulkan-capable display
> and will be populated in the next reading.

| | Anchor | Value | Notes |
|---|---|---:|---|
| A1 | Peak RSS, init        | **4.1 MiB** | R14 harness (`vck_tests`), one-shot |
| A2 | Peak RSS, steady      | **4.1 MiB** | R14 harness is one-shot — same as A1 here. RGBTriangle 1000-frame number lands in next reading |
| A3 | Frame time            | _GPU required_ | 1280×720, V-sync off, framesInFlight = 2 |
| A4 | VMM staging throughput| _GPU required_ | bytes/sec through staging ring at max load |
| A5 | Hot-reload latency    | _GPU required_ | `.spv` mtime → first redraw |

| | Static metric | v0.5 |
|---|---|---:|
| S1 | Public class count (`VCK.h`)        | **30** |
| S2 | `cfg` knob count                    | **22** |
| S3 | Examples count                      | **14** |
| S4 | `VCK.h` line count                  | **1215** |
| S5 | `libvck.a` (static lib) size, Release | **1.2 MiB** |
| S6 | Build wall time, all examples + tests | **67 s** |

**Test machine** _(headless CI baseline)_:

- Kernel: `Linux 5.15.200` (Ubuntu 22.04, headless)
- Compiler: `g++ 11.4.0`
- Build type: `Release` (`-O3 -DNDEBUG`)
- GPU: _none_ (R14 ctest is the only thing that runs)
- Vulkan SDK: 1.4.321.0 (per `example/CMakeLists.txt` FetchContent pin)

---

## Historical readings

_(empty — first reading appended here once `docs/perf_baseline.sh` runs)_

---

## Methodology — what each anchor measures

### A1 / A2 · Peak RSS

`/usr/bin/time -v ./RGBTriangle --frames=16` for A1; `--frames=1000`
for A2. The harness reads `Maximum resident set size` from the GNU
time output. On macOS use `/usr/bin/time -l` and read
`maximum resident set size` (note: macOS reports bytes, Linux KiB —
the script normalizes).

A1 covers `Context::Initialize` through swapchain/framebuffer creation
and the first 16 presented frames. A2 covers steady-state with
`framesInFlight = 2` and one warm-up garbage-collection cycle.

The delta `A2 - A1` is the per-frame allocation creep, which should
be zero or near-zero. A non-trivial delta is a leak finding for
Theme A.

### A3 · Frame time

Frames per second at the resolution and present-mode listed. The
harness runs the example with `presentMode = Immediate` (V-sync off)
and `framesInFlight = 2`. Frame time is reported as the median over
1000 frames after a 100-frame warm-up. Median, not mean — to ignore
the first-paint outliers.

This is **not** a GPU benchmark. RGBTriangle is fragment-trivial;
the number is dominated by present-engine and validation overhead.
Useful as a frame-loop overhead anchor, useless as a fillrate anchor.

### A4 · VMM staging throughput

`VMMExample` allocates a fixed-size staging ring and uploads a
synthetic texture pool (256 × 1 MiB textures by default). The harness
times the upload phase and reports MiB/sec. This stresses the
staging ring and the `vkQueueWaitIdle` model that the audit's A2
sweep is going to redesign — the v0.5 baseline gives v0.6 a number
to beat.

### A5 · Hot-reload latency

`ShaderToolingExample` runs in watch mode. The harness `touch`es the
fragment `.spv` file and timestamps the next frame that emits a
"reload complete" `VCKLog::Notice`. Reported as wall-clock
milliseconds from `touch` to log emit. Bounded below by one
`framesInFlight` worth of `vkDeviceWaitIdle` (currently the
`scheduler.DrainInFlight()` call inside the example's reload path).

---

## Static surface metrics

S1–S6 do not change between machines, so the script captures them
unconditionally — they live in the **Latest measurement** table above.
Static-surface anchors are part of the baseline because shrinking
core surface is itself a goal.

| | Metric | What it answers |
|---|---|---|
| S1 | Public class count (`VCK.h`)        | "How big is the contract?" |
| S2 | `cfg` knob count                    | "How many things does the user have to read about before writing the first line?" |
| S3 | Examples count                      | R20 floor — at least one example per public class. |
| S4 | `VCK.h` line count                  | A proxy for surface complexity that's harder to game than class count. |
| S5 | `libvck.a` (static lib) size, Release | "What does adding VCK cost on disk?" |
| S6 | Build wall time, all examples + tests | "How long do contributors wait for a clean rebuild?" |

`docs/perf_baseline.sh` populates S1–S6 from the source tree directly
— no GPU required.

---

## What is intentionally not measured

- **Triangle throughput.** VCK is a kit, not a forward+ renderer.
  A triangle benchmark would be measuring the GPU, not the kit.
- **Validation-layer overhead.** Validation is on or off; comparing
  with-vs-without is documented in `docs/Design.md` (R14 & R23) and
  not part of the baseline.
- **Shader compilation throughput.** Owned by `glslangValidator`,
  outside the kit's contract.
- **Single-frame latency** in submit-to-present. The execution layer
  exposes `DebugTimeline` for that — the user runs it on their own
  scene. The baseline does not try to substitute scene-shaped
  numbers with synthetic ones.

---

## Procedure

1. Pull the `VCK` branch at the tag or sha you want to measure.
2. From the repo root, run:

```bash
bash docs/perf_baseline.sh
```

   The script prints a markdown block. Copy it into **Latest
   measurement** above (and move the previous reading into
   **Historical readings**, prepended with the date).

3. Commit the doc-only update on a `devin/<timestamp>-perf-rebase`
   branch and open a PR titled `docs(perf): rebase baseline on
   <commit>`. Keep the metric and the commit linked so any v0.6
   regression can be bisected.

The script exits with code 0 on success — including when the GPU-only
anchors cannot be collected (in which case A3/A4/A5 are reported as
"GPU unavailable" and only the static + RSS anchors land). A non-zero
exit code means a required example failed to build, or the cmake
configure step failed.
