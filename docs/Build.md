<div align="center">

# Build &nbsp;·&nbsp; Windows / Linux / macOS

Five-minute setup. One build script per platform. 13 examples + R14 unit
tests, ready to run.

</div>

---

## Windows

Two toolchains, one build script. `build.bat` auto-detects Visual Studio
via `vswhere.exe` and prefers `cl` when available; falls back to MinGW
`g++` otherwise. Override with `--toolchain={auto|cl|gcc}`.

|              | `--toolchain=cl` (MSVC)                 | `--toolchain=gcc` (MinGW)            |
|--------------|------------------------------------------|---------------------------------------|
| Compiler     | `cl.exe` + `lib.exe` + `link.exe`        | `g++.exe` + `ar.exe`                  |
| Parallel TU  | `cl /MP` (cores)                         | sequential                            |
| GLFW lib     | `example\deps\glfw3.lib` (lib-vc2022)    | `example\deps\libglfw3.a` (lib-mingw-w64) |
| Static lib   | `build\vck.lib`                          | `build\libvck.a`                      |
| Wall-clock build-all (modern 8c)| ~2-3 min                  | ~3-4 min                              |
| Status       | shipped, validated by CI                 | canonical reference toolchain (v0.3)  |

### Common requirements

- **Vulkan SDK** — set `VULKAN_SDK` to the install root.
- **glslangValidator** on `PATH` (ships with the SDK).
- **GLFW Windows pre-compiled** from <https://www.glfw.org>:
  - copy `lib-mingw-w64\libglfw3.a` to `example\deps\libglfw3.a` (gcc path)
  - copy `lib-vc2022\glfw3.lib`     to `example\deps\glfw3.lib`   (cl  path)

### Toolchain-specific requirements

**`--toolchain=cl`** — Visual Studio 2019 / 2022 (any edition) **or** Build
Tools, with the *Desktop development with C++* workload installed. If you
launch `build.bat` from a regular `cmd.exe`, `vswhere.exe` finds the install
and our script calls `vcvars64.bat` in-process so `cl` resolves immediately.
If you launch from a *Developer Command Prompt for VS* or *Developer
PowerShell*, `cl` is already on `PATH` and we skip vswhere.

**`--toolchain=gcc`** — MinGW-w64 g++ + `ar` on `PATH` (MSYS2's
`/mingw64/bin` is the simplest source).

### Dependency layout

```
vendor/                          (in repo - you don't add anything)
  vma/vk_mem_alloc.h                     AMD VMA allocator
  glfw/include/GLFW/glfw3.h              GLFW C API headers
  glfw/include/GLFW/glfw3native.h
  vulkan_headers/vulkan/*.h              Vulkan SDK headers mirror

example/                         (you add one or two files here)
  build.bat
  deps/
    libglfw3.a                           MinGW pre-compiled  (gcc path)
    glfw3.lib                            MSVC pre-compiled   (cl  path)
```

`build.bat` refuses to run and prints a diagnostic if the appropriate GLFW
archive or any `vendor/` header is missing.

### Build + run

```
cd example
build.bat                    :: auto-detect toolchain (prefer cl)
build.bat --toolchain=gcc    :: force MinGW
build.bat --toolchain=cl A   :: cl + build all + non-interactive
```

Pick an example from the menu. On success the executable lands at
`example\<Example>\<Example>.exe`. Run from `cmd.exe` — the binary is
console-subsystem, so `VCKLog` output lands in the terminal you launched it
from (and simultaneously in the VS Output window when a debugger is attached).

---

## Linux / macOS

`example/build.sh` auto-detects the OS via `uname -s` and mirrors the
`build.bat` menu. Single toolchain (system `g++` on Linux, `clang++` on
macOS via `CXX` override).

### Requirements

- **Vulkan headers + loader** — distro packages or the LunarG SDK
  (`sudo apt install libvulkan-dev glslang-tools` on Debian/Ubuntu;
  `brew install vulkan-headers vulkan-loader glslang molten-vk` on macOS).
- **glslangValidator** on `PATH`.
- **GLFW** dev package — `sudo apt install libglfw3-dev` (Linux),
  `brew install glfw` (macOS).
- **g++** or **clang++** on `PATH`.
- `pkg-config` on `PATH`.

The script uses `pkg-config --cflags --libs vulkan glfw3` — no manual paths.
macOS needs MoltenVK for an ICD; install via Homebrew and point
`VK_ICD_FILENAMES` at `MoltenVK_icd.json` if the loader doesn't pick it up
automatically.

### Build + run

```
cd example
./build.sh           :: interactive menu
./build.sh A         :: build all
```

Pick an example from the menu. `[A]` builds all 13 in order. Wall-clock
build-all on a modern 8-core machine is **~30-40 s** (parallel `g++ -c`
across `nproc` cores; lib stamp caching skips rebuild on subsequent runs).

---

## Lib-once compile model

Both `build.bat` and `build.sh` compile the VCK static library **once**
into `build/libvck.a` (or `build/vck.lib` under cl), then per example
compile only `main.cpp + App.cpp` (2 TUs) and link against the prebuilt
archive.

|                              | Old model                  | New (PR #7)                 |
|------------------------------|----------------------------|-----------------------------|
| Build-all TU count           | 13 examples × 14 sources = **182** TUs | 13 lib + 13 × 2 example = **39** TUs |
| Linux / macOS wall-clock     | ~10-12 min                 | **~30-40 s**                |
| Windows MinGW wall-clock     | ~10-12 min                 | **~3-4 min**                |
| Windows MSVC `/MP` wall-clock| n/a                        | **~2-3 min**                |
| Repeat build-all (no source change) | full recompile     | **lib stamp hits → 0 s lib stage** |

The lib stamp is a fingerprint of the toolchain + `CXXFLAGS` + `INCLUDES`
+ a sentinel that bumps when the source list changes; switching toolchains
or changing `CXXFLAGS` invalidates the cache and triggers a clean rebuild.
When in doubt, delete `example/build/`.

The library bundles:

- All 12 VKB sources from `layers/core/`.
- `VCKExpansion.cpp` and `VCKExecution.cpp` from `layers/expansion/` and
  `layers/execution/`.
- `VulkanMemoryManager.cpp` from `layers/vmm/` — the `ar` archiver pulls
  by-need so non-VMM examples cost zero extra link time.

---

## R14 unit-test harness

```
cd example
build.bat T          :: Windows (auto-detect toolchain)
./build.sh T         :: Linux / macOS
```

Builds a single binary at `tests/vck_tests` (or `tests\vck_tests.exe`)
that runs the header-only assertion harness shipped in `tests/`. Failed
assertions exit non-zero so CI flips red on regression.

The harness:

- Has **zero third-party deps** (rule 16) — header-only `tests/vck_test.h`
  with `TEST(group, case)` and `ASSERT_TRUE/FALSE/EQ/NE/GE/CONTAINS`.
- Hooks `VCKLog::SetSink(...)` (added in PR #7) to capture every
  `Emit(level, tag, body)` call into a vector before any dedup or
  Info-suppression filtering, so tests can assert that every failure
  routes through `VCKLog::Error` exactly once (rule 14).
- Tests the **logging contract** without needing a real Vulkan device —
  validates `VK_CHECK` on literal `VkResult` values, legacy `LogVk` tag
  classification, dedup behaviour, and sink lifecycle.

CI runs the harness on every platform after the build-all step succeeds.

---

## Logging

`VCK::VCKLog` is the structured logger; every line goes to **two** sinks:

1. `OutputDebugStringA` on Windows (VS / WinDbg Output pane), `stderr` on Linux/macOS.
2. `stdout` + `fflush` — the console that launched the exe.

Levels:

| Level  | When visible | Use for |
|--------|--------------|---------|
| `Info`   | only if `cfg.debug = true` | init chatter, verbose diagnostics |
| `Notice` | always | one-time user-relevant pick (AA auto, MSAA clamp, present-mode fallback) |
| `Warn`   | always | recoverable surprise |
| `Error`  | always | user-facing failure |

`VCKLog` also de-duplicates identical consecutive lines — if you spam the
same `(tag, body)` every frame you'll see `(repeated N more times)` exactly
once when the next different line arrives, keeping the console readable
without losing information.

`VK_CHECK(expr)` wraps any `VkResult`-returning call and routes failures
directly to `VCKLog::Error("VK_CHECK", …)` — so Vulkan errors are always
loud, regardless of `cfg.debug`:

```cpp
VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
```

Legacy `LogVk("[Tag] body")` call sites still work — the shim parses the
leading `[Tag]` and classifies `ERROR` / `ERR` / `FAILED` as `Error`,
`WARN` / `WARNING` as `Warn`, everything else as `Info`.

`VCKLog::SetSink(fn)` lets observers (or the R14 harness) intercept every
log call without disrupting the screen output. See `tests/vck_log_capture.h`
for the canonical use.

No `AllocConsole` dance: both build scripts produce a console-subsystem exe,
so `stdout` is already wired to the launching terminal.
