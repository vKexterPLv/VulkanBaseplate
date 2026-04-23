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

```
example/
  build.bat
  deps/
    vk_mem_alloc.h                 (from the VMA repo)
    libglfw3.a                     (from GLFW lib-mingw-w64/)
    glfw/
      include/
        GLFW/
          glfw3.h
          glfw3native.h
```

`build.bat` will refuse to run and print a diagnostic if any of these is
missing.

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
`build.bat` menu (`[1]-[9] / [A] / [0]`).

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

`LogVk(msg)` writes every line to **two** sinks:
1. `OutputDebugStringA` — Visual Studio / WinDbg Output pane.
2. `stdout` + `fflush` — the console that launched the exe.

`VK_CHECK(expr)` wraps any `VkResult`-returning call and logs the failing
expression on non-success. Use it everywhere:

```cpp
VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
```

No `AllocConsole` dance: `build.bat` produces a console-subsystem exe, so
`stdout` is already wired to the launching terminal.
