<div align="center">

# Build &nbsp;·&nbsp; Windows / MinGW

Five-minute setup. One `build.bat`. Nine examples ready to run.

</div>

---

## Requirements

- **Vulkan SDK** — set `VULKAN_SDK` to the install root.
- **glslangValidator** on `PATH` (ships with the SDK).
- **g++ (MinGW-w64)** on `PATH`.
- **GLFW Windows pre-compiled** from <https://www.glfw.org>.

## Dependency layout (first time)

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

## Build + run

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

## Build all in one go

From the menu: `A`.  `build.bat` runs the nine examples in order and stops
on the first failure.

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
