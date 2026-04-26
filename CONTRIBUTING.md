# Contributing to VCK

Thanks for wanting to contribute. VCK is small on purpose — no scene graph, no
ECS, no material system — so most contributions fall into one of:

1. **Bug fix** in the core primitives, expansion layer, execution layer, or
   examples. Smallest-possible diff preferred.
2. **New example** in `example/<Name>Example/` demonstrating a Vulkan feature
   with the same `main.cpp + App.{h,cpp}` pattern as the existing 13.
3. **Docs / wiki polish** — the wiki is the 1-hour-onboarding surface; the
   `docs/` folder is where the source lives (wiki is mirrored from it).
4. **Cross-platform fix** for Windows / Linux / macOS compile or runtime issues.

## Before you open a PR

- Read [`docs/Design.md`](docs/Design.md) — the 22 design rules are the
  architectural contract. PRs that violate a rule get a rework request unless
  the rule itself is being changed in the same PR.
- Read [`VCK.h`](VCK.h) — the header block is the single source of truth for
  the API surface, class index, config, and quick-start (rule 21). If you add
  a public class or config knob, update `VCK.h`.
- Rule 20: every public class in `VCK.h` must be exercised by at least one
  example. New public API lands with its example in the same PR.
- Run `example/build.bat [A]` (Windows, MinGW) or `example/build.sh` (Linux /
  macOS) and confirm all 13 examples still compile.

## Design rules, in one line

1. Explicit over magic (Init/Shutdown pairs, no singletons).
2. Core owns resources; expansion/execution borrow raw pointers.
3. Strict lifecycle order (Context → Device → Swapchain → Command → Sync → …).
4. No hidden synchronisation — only `Shutdown()` may call `vkDeviceWaitIdle`;
   the runtime hot path never does (v0.3).
5. Memory is frame-scoped or persistent; no dangling state.
6. No hidden behaviour; user-visible picks log as `VCKLog::Notice`.
7. User owns the frame loop unless they opt into `FrameScheduler`.
8. Explicit synchronisation model; no implicit ordering.
9. Escape hatches everywhere — every wrapper exposes `Get<VkHandle>()`.
10. Zero hidden GPU state.
11. Deterministic frame behaviour under Pipelined / Lockstep; `AsyncMax` is documented nondeterminism.
12. Explicit recreation events (logged + `DebugTimeline` spans).
13. Debuggability is a core feature, not optional.
14. Fail fast, fail loud — every failure returns an explicit `bool` **and**
    logs via `VCKLog::Error("<subsystem>", ...)`. Silent `return false` is a bug.
15. Minimal core surface.
16. No engine assumptions (no scene graph, no materials, no assets).
17. The frame is the unit of truth.
18. External synchronisation — concurrent access to the same VCK instance from
    multiple threads is UB unless the caller locks. `JobGraph` is the sole
    thread-safe exception.
19. Zero cost for unused features — un-`Initialize`d modules allocate nothing,
    spawn no thread, emit no log line.
20. Every public class in `VCK.h` has at least one example under `example/`.
21. `VCK.h` is the API surface. Layer headers under `layers/*` are
    implementation detail and may move. Breaking changes to `VCK.h` bump the
    minor version (0.x) until v1.0.0.
22. VCK never owns user handles. Raw `Vk*` passed in is caller-owned; handles
    VCK returns via getters are borrows — do not destroy them.

## Branching

- `master` — stable; only merge via PR with a green CI.
- `VCK` — current integration branch for the 0.x pre-release line.
- Feature branches — `feature/<name>` or `fix/<issue>`.

## Commit style

- First line: ≤ 72 chars, imperative mood ("Add AA auto-detector", not "Added AA auto-detector").
- Body (optional): wrap at 72 cols, explain *why* more than *what*.
- No `--amend` on pushed commits; no `--no-verify`.

## Code style

- C++17. Follow the surrounding style of the file you're editing.
- Include order: local (`"../../VCK.h"`) → third-party (`<vulkan/vulkan.h>`) → std (`<vector>`).
- Prefer minimal comments; `VCK.h` header documents the API, not per-class
  headers.
- `snake_case` for local variables, `camelCase` for functions/methods,
  `PascalCase` for types, `m_Member` for class members.

## Testing

VCK currently has no unit tests. CI on Windows runs `build.bat [A]` which
compiles all 13 examples. Runtime validation is manual:

- Run an example, resize the window (including 720p ↔ 4K), confirm no
  stutter.
- Run with `cfg.debug = true` and verify the `[Context]` / `[Device]` /
  `[Swapchain]` init chatter is visible.

## License

By contributing, you agree your work is licensed under the repo's MIT license
(see [`LICENSE`](LICENSE)).
