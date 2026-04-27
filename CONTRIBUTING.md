# Contributing

VCK is small on purpose. No scene graph, no ECS, no material system, no asset pipeline. If you want to add one of those, this is the wrong project — go fork or write your own engine on top.

What does land here:

1. **Bug fixes** in core / expansion / execution / VMM / examples. Small, focused diffs. One bug per commit.
2. **New examples** under `example/<Name>Example/` following the `main.cpp + App.{h,cpp}` pattern the existing 13 use. Don't reinvent the layout.
3. **Docs / wiki polish.** The wiki mirrors `docs/`; edit the `docs/` source, not the wiki directly.
4. **Cross-platform fixes** for Windows / Linux / macOS. CI runs all three.

## Before opening a PR

Read these. Yes really.

- [`docs/Design.md`](docs/Design.md) — the 24 rules are the contract. Violating one gets a rework request unless you're changing the rule itself in the same PR.
- [`VCK.h`](VCK.h) — the header doc block is the API surface (R21). New public class or `cfg` knob? Update `VCK.h` in the same PR.
- R20: every public class has at least one example. New API ships with its example.
- Build it. `cmake -S example -B build -G Ninja && cmake --build build -j` plus `ctest --test-dir build`. CI does this on all 4 platforms. If your local fails, CI will fail.

## The 24 design rules in one line each

1. Explicit over magic. `Initialize` / `Shutdown` pairs, no singletons.
2. Core owns; expansion / execution borrow raw pointers.
3. Strict lifecycle order: Context → Device → Swapchain → Command → Sync → ...
4. No hidden synchronisation. Only `Shutdown()` may call `vkDeviceWaitIdle`. The hot path never does.
5. Memory is frame-scoped or persistent. No dangling state.
6. No hidden behaviour. User-visible picks log as `VCKLog::Notice`.
7. User owns the frame loop unless they opt into `FrameScheduler`.
8. Explicit synchronisation. No implicit ordering.
9. Escape hatches everywhere. Every wrapper exposes `Get<VkHandle>()`.
10. Zero hidden GPU state.
11. Deterministic frame behaviour under Pipelined / Lockstep. `AsyncMax` is documented nondeterminism.
12. Explicit recreation events (logged + `DebugTimeline` spans).
13. Debuggability is a core feature. Not optional.
14. Fail fast, fail loud. Every failure returns `bool` AND logs `VCKLog::Error("<subsystem>", ...)`. Silent `return false` is a bug.
15. Minimal core surface.
16. No engine assumptions.
17. The frame is the unit of truth.
18. External synchronisation. Concurrent access from multiple threads is UB unless the caller locks. `JobGraph` is the sole exception.
19. Zero cost for unused features. Un-`Initialize`d modules allocate nothing, spawn no thread, emit no log.
20. Every public class in `VCK.h` has at least one example.
21. `VCK.h` is the API surface. Layer headers under `layers/` may move. Breaking changes to `VCK.h` bump minor (0.x) until v1.0.
22. VCK never owns user handles. Raw `Vk*` passed in is caller-owned. Handles VCK returns via getters are borrows — don't destroy them.
23. Extension transparency. Every extension VCK enables silently is logged via `VCKLog::Notice` at init, with support status and fallback.
24. `cfg` is the contract. If a behavioural difference changes how the user writes their renderer → it lives in `cfg`. If it only changes how VCK works underneath → silent bundle.

## Branching

- `VCK` — current pre-release integration branch (default).
- `first-vck` — historical first-release snapshot. Don't touch.
- Feature branches: `devin/<timestamp>-<name>` or `feature/<name>` or `fix/<issue>`.

## Commits

- First line ≤ 72 chars, imperative mood. "Add AA auto-detector" not "Added AA auto-detector".
- Body wrapped at 72. Explain *why* more than *what*. The diff already shows what.
- No `--amend` on pushed commits. Add follow-up commits.
- No `--no-verify`. If the hook is wrong, fix the hook.

## Code style

- C++17 in core. C++20 only inside `example/` (designated initializers).
- Include order: local (`"../../VCK.h"`) → third-party (`<vulkan/vulkan.h>`) → std (`<vector>`).
- Comments are sparse and explain *why*. The `VCK.h` header documents the public API; layer headers don't repeat it.
- `snake_case` locals, `camelCase` methods, `PascalCase` types, `m_Member` for class members.
- Don't comment the diff. "Fixed X by doing Y" belongs in the commit message, not in the source.

## Testing

R14 unit harness lives in `tests/` and runs under `ctest`. It's a small home-grown thing, not GoogleTest — VCK has zero third-party test dependencies. Each test asserts that a failed `Initialize()` returns `false` AND emits exactly one `VCKLog::Error`.

Beyond R14, validation is manual:
- Run an example. Resize the window, including 720p ↔ 4K. No stutter.
- Run with `cfg.debug = true`. The init chatter from `[Context]` / `[Device]` / `[Swapchain]` is visible.

## License

By contributing, you agree your work ships under the repo's MIT licence (see [`LICENSE`](LICENSE)).
