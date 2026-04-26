<div align="center">

# Build &nbsp;·&nbsp; Windows / Linux / macOS

Five-minute setup. One build script per platform. Nine examples ready to run.

</div>

---

## Windows (MinGW)

### Requirements

- **Vulkan SDK** — set `VULKAN_SDK` to the install root.
- **glslangValidator** on `PATH` (ships with the SDK).
- **g++ (MinGW-w64)** on `PATH`.
- **GLFW Windows pre-compiled** from <https://www.glfw.org>.

### Dependency layout (first time)

Source-only third-party deps are committed to `vendor/` at the repo root
and are not downloads. You only need to fetch the **Windows pre-compiled
GLFW library** and drop it in `example/deps/`:

```
vendor/                          (in repo - you don't add anything)
  vma/vk_mem_alloc.h                     AMD VMA allocator
  glfw/include/GLFW/glfw3.h              GLFW C API headers
  glfw/include/GLFW/glfw3native.h
  vulkan_headers/vulkan/*.h              Vulkan SDK headers mirror

example/                         (you add one file here)
  build.bat
  deps/
    libglfw3.a                           Windows pre-compiled MinGW lib
```

Download GLFW's "Windows pre-compiled binaries" from <https://www.glfw.org>
and copy `lib-mingw-w64/libglfw3.a` to `example/deps/libglfw3.a`.

`build.bat` will refuse to run and print a diagnostic if `libglfw3.a` or
any `vendor/` header is missing.

### Build + run

```
cd example
build.bat
```

Pick an example from the menu. On success:

```
example\VMMExample\VMMExample.exe
```

Run from `cmd.exe` — the exe is console-subsystem, so `LogVk` output lands
in the terminal you launched it from (and simultaneously in the VS Output
window when a debugger is attached).

### Build all in one go

From the menu: `A`.  `build.bat` runs the nine examples in order and stops
on the first failure.

---

## Linux / macOS

`example/build.sh` auto-detects the OS via `uname -s` and mirrors the
`build.bat` menu (`[1]-[13] / [A] / [0]`).

### Requirements

- **Vulkan SDK** (or distro Vulkan dev packages — e.g.
  `sudo apt install libvulkan-dev glslang-tools` on Debian/Ubuntu).
- **glslangValidator** on `PATH`.
- **GLFW** dev package — e.g. `sudo apt install libglfw3-dev` (Linux) or
  `brew install glfw` (macOS).
- **g++** or **clang++** on `PATH`.
- `pkg-config` on `PATH`.

The script uses `pkg-config --cflags --libs vulkan glfw3` — no manual paths.
macOS needs MoltenVK for an ICD; install via the LunarG SDK or Homebrew and
point `VK_ICD_FILENAMES` at `MoltenVK_icd.json` if the loader doesn't pick
it up automatically.

### Build + run

```
cd example
./build.sh
```

Pick an example from the menu. `[A]` builds all nine in order, same as the
Windows script.

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

`VCKLog` also de-duplicates identical consecutive lines — if you spam the same
`(tag, body)` every frame you'll see `(repeated N more times)` exactly once
when the next different line arrives, keeping the console readable without
losing information.

`VK_CHECK(expr)` wraps any `VkResult`-returning call and routes failures
directly to `VCKLog::Error("VK_CHECK", …)` — so Vulkan errors are always
loud, regardless of `cfg.debug`:

```cpp
VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
```

Legacy `LogVk("[Tag] body")` call sites still work — the shim parses the
leading `[Tag]` and classifies `ERROR` / `ERR` / `FAILED` as `Error`,
`WARN` / `WARNING` as `Warn`, everything else as `Info`.

No `AllocConsole` dance: both build scripts produce a console-subsystem exe,
so `stdout` is already wired to the launching terminal.
