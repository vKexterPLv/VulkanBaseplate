<div align="center">

# Build &nbsp;·&nbsp; Windows / Linux / macOS

Five-minute setup. CMake + Ninja everywhere. 13 examples + R14 unit tests,
one canonical build command per platform.

</div>

---

## TL;DR

```
cd example
cmake -S . -B build -G Ninja
cmake --build build -j --target examples
```

Wall-clock cold build-all on a modern 8-core machine:

| Platform                          | Time        |
|-----------------------------------|-------------|
| Linux (Ubuntu g++)                | ~30-45 s    |
| macOS (clang++ + MoltenVK)        | ~30-45 s    |
| Windows / MSVC `cl`  + Ninja `-j` | ~1-2 min    |
| Windows / MinGW `g++` + Ninja `-j`| ~2-3 min    |

The `build.bat` / `build.sh` wrappers are convenience menus over the
canonical CMake commands above — same result, same speed.

---

## Common requirements

- **CMake 3.20+** on `PATH`.
- **Ninja** on `PATH` (`apt install ninja-build`, `brew install ninja`,
  `choco install ninja`, or scoop).
- **glslangValidator** on `PATH` (ships with the Vulkan SDK; `apt install
  glslang-tools`, `brew install glslang`).
- **Vulkan SDK / loader** — `VULKAN_SDK` env var on Windows; distro
  packages on Linux (`libvulkan-dev`); Homebrew on macOS (`vulkan-loader`
  + `vulkan-headers` + `molten-vk`).
- **GLFW** — Linux/macOS take it from the system package (`libglfw3-dev`
  / `brew install glfw`). On Windows CMake fetches GLFW 3.4 source via
  `FetchContent` and builds it as part of the configure step, so there is
  no manual download.
- **Vulkan-Headers** — CMake fetches the canonical `vulkan-sdk-1.4.321.0`
  tag via `FetchContent`. This is what unblocks the `vk_video/*.h`
  references in `vulkan_core.h` on hosts whose system Vulkan headers are
  older than the SDK we link against.

VMA ships in-repo at `vendor/vma/vk_mem_alloc.h`. There is no
`example/deps/` directory anymore — CMake assembles every dependency
itself.

---

## Windows

CMake + Ninja picks whichever C++ compiler is on `PATH`. **No
`--toolchain` flag** — Ninja autodetects:

- Run from a *Developer Command Prompt for VS* (or *Developer PowerShell*)
  → `cl.exe` + `lib.exe` + `link.exe` are on `PATH` → Ninja uses MSVC.
- Otherwise prepend MSYS2's `C:\msys64\mingw64\bin` to `PATH` → Ninja uses
  MinGW-w64 `g++`.

```cmd
cd example

:: Interactive menu (numbers 1-13, A=all, T=tests, 0=exit):
build.bat

:: Non-interactive forms:
build.bat A          :: build all 13 examples
build.bat T          :: build + run the R14 unit-test harness
build.bat 1          :: build RGBTriangle only

:: Or skip the wrapper entirely - same result:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target examples
ctest --test-dir build --output-on-failure
```

On success each executable lands at `example\<Example>\<Example>.exe`,
right next to its `assets/` so the working directory works out of the box.
Run from `cmd.exe` — the binaries are console-subsystem, so `VCKLog` output
lands in the terminal you launched them from (and simultaneously in the VS
Output window when a debugger is attached).

---

## Linux / macOS

```
cd example

# Interactive menu:
./build.sh

# Non-interactive forms:
./build.sh A         # build all 13 examples
./build.sh T         # build + run the R14 unit-test harness

# Or skip the wrapper entirely:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target examples
ctest --test-dir build --output-on-failure
```

Distros / Homebrew install:

```
# Ubuntu / Debian
sudo apt install cmake ninja-build g++ libvulkan-dev libglfw3-dev \
                 vulkan-tools glslang-tools pkg-config

# macOS
brew install cmake ninja vulkan-headers vulkan-loader glfw glslang \
             molten-vk pkg-config
export VK_ICD_FILENAMES="$(brew --prefix molten-vk)/share/vulkan/icd.d/MoltenVK_icd.json"
```

The script uses `pkg-config` under the hood — no manual paths. macOS
needs MoltenVK for an ICD; install via Homebrew and point
`VK_ICD_FILENAMES` at `MoltenVK_icd.json` if the loader doesn't pick it up
automatically.

---

## Lib-once compile model

`example/CMakeLists.txt` declares one `add_library(vck STATIC ...)` target
that compiles every layer source exactly once. Each example is a separate
`add_executable()` linking against `vck` via
`target_link_libraries(<example> PRIVATE vck)`.

|                              | Old per-example model       | New (PR #7)                 |
|------------------------------|-----------------------------|-----------------------------|
| Build-all TU count           | 13 × 14 = **182** TUs       | 13 lib + 13 × 2 = **39** TUs|
| Linux / macOS wall-clock     | ~10-12 min                  | **~30-40 s**                |
| Windows MinGW wall-clock     | ~10-12 min                  | **~2-3 min**                |
| Windows MSVC wall-clock      | n/a                         | **~1-2 min**                |
| Repeat build-all (no change) | full recompile              | **0 s** (Ninja null-build)  |

The library bundles:

- All `layers/core/*.cpp` (`VulkanContext`, `VulkanDevice`, `VulkanSwapchain`,
  `VulkanCommand`, `VulkanPipeline`, `VulkanBuffer`, `VulkanImage`,
  `VulkanSync`, `VmaImpl`, `VCKCrossplatform`).
- `VCKExpansion.cpp` and `VCKExecution.cpp` from `layers/expansion/` and
  `layers/execution/`.
- `VulkanMemoryManager.cpp` from `layers/vmm/` — the static-archive linker
  pulls only the symbols each example uses, so non-VMM examples cost zero
  extra link time.

Shaders compile via `add_custom_command()` calling `glslangValidator`;
each example depends on its own `*.spv` outputs so editing
`example/<X>/assets/<X>.frag` triggers exactly one recompile + relink.

---

## R14 unit-test harness

```
cd example
build.bat T          :: Windows
./build.sh T         :: Linux / macOS
ctest --test-dir build --output-on-failure   :: any platform, any time
```

Builds the `vck_tests` target (single binary at `tests/vck_tests` or
`tests\vck_tests.exe`) running the header-only assertion harness shipped
in `tests/`. Failed assertions exit non-zero so CI flips red on regression.

The harness:

- Has **zero third-party deps** (rule 16) — header-only `tests/vck_test.h`
  with `TEST(group, case)` and `ASSERT_TRUE/FALSE/EQ/NE/GE/CONTAINS`.
- Hooks `VCKLog::SetSink(...)` to capture every `Emit(level, tag, body)`
  call into a vector before any dedup or Info-suppression filtering, so
  tests can assert that every failure routes through `VCKLog::Error`
  exactly once (rule 14).
- Tests the **logging contract** without needing a real Vulkan device —
  validates `VK_CHECK` on literal `VkResult` values, legacy `LogVk` tag
  classification, dedup behaviour, and sink lifecycle.

CI runs the harness on every platform after the build-all step succeeds
(`ctest --test-dir build --output-on-failure`).

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

No `AllocConsole` dance: the CMake build produces console-subsystem exes,
so `stdout` is already wired to the launching terminal.
