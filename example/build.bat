@echo off
setlocal enabledelayedexpansion

:: =============================================================================
::  build.bat  -  VCK example builder
::  Run from the example\ folder.
::
::  Expected deps layout:
::    example\
::      build.bat
::      deps\
::        vk_mem_alloc.h
::        libglfw3.a
::        glfw\
::          include\
::            GLFW\
::              glfw3.h
::              glfw3native.h
::
::  GLFW: download "Windows pre-compiled binaries" from https://www.glfw.org
::  Copy include\GLFW\ into example\deps\glfw\include\GLFW\
::  Copy lib-mingw-w64\libglfw3.a into example\deps\libglfw3.a
::
::  Requirements on PATH:
::    glslangValidator  (Vulkan SDK\Bin)
::    g++               (MinGW-w64)
::
::  VULKAN_SDK env var must point to your Vulkan SDK root.
:: =============================================================================

:: ── Checks -------------------------------------------------------------------
if "%VULKAN_SDK%"=="" (
    echo ERROR: VULKAN_SDK is not set.
    exit /b 1
)
where glslangValidator >nul 2>&1
if errorlevel 1 ( echo ERROR: glslangValidator not on PATH. & exit /b 1 )
where g++ >nul 2>&1
if errorlevel 1 ( echo ERROR: g++ not on PATH. & exit /b 1 )
if not exist "deps\vk_mem_alloc.h" (
    echo ERROR: deps\vk_mem_alloc.h not found.
    exit /b 1
)
if not exist "deps\libglfw3.a" (
    echo ERROR: deps\libglfw3.a not found.
    exit /b 1
)
if not exist "deps\glfw\include\GLFW\glfw3.h" (
    echo ERROR: deps\glfw\include\GLFW\glfw3.h not found.
    echo        Download GLFW Windows pre-compiled from https://www.glfw.org
    echo        Copy include\GLFW\ to example\deps\glfw\include\GLFW\
    exit /b 1
)

:: ── Shared flags -------------------------------------------------------------
::
::  Include search order:
::    1. deps\                   vk_mem_alloc.h
::    2. deps\glfw\include       GLFW/glfw3.h, GLFW/glfw3native.h
::    3. ..                      VCK.h and all Vulkan*.h (project root)
::    4. VULKAN_SDK\Include      vulkan/vulkan.h and full vk_video/ tree
::
::  Note: we do NOT add the bundled vulkan\ folder in the project root to
::  the include path — it is incomplete (missing vk_video/).
::  The SDK headers are always complete.
::
set INCLUDES=-Ideps -Ideps\glfw\include -I.. -I"%VULKAN_SDK%\Include"

::  Lib search + link:
::    deps\libglfw3.a   GLFW static (MinGW)
::    VULKAN_SDK\Lib    vulkan-1.lib
set LIBS=-Ldeps -L"%VULKAN_SDK%\Lib" -lvulkan-1 -lglfw3 -lgdi32 -luser32 -lshell32

::  VCK core .cpp files (shared by all examples)
set VKB=..\VmaImpl.cpp ..\VulkanBuffer.cpp ..\VulkanCommand.cpp ..\VulkanContext.cpp ..\VulkanDevice.cpp ..\VulkanHelpers.cpp ..\VulkanImage.cpp ..\VulkanPipeline.cpp ..\VulkanSwapchain.cpp ..\VulkanSync.cpp ..\VCKExpansion.cpp

:: ── Menu ---------------------------------------------------------------------
echo.
echo  VCK - example builder
echo  -----------------------------------
echo  [1]  RGBTriangle                - coloured triangle, live resize
echo  [2]  MipmapExample              - mip chain generation and sampling
echo  [3]  VMMExample                 - VulkanMemoryManager all three layers
echo  ---  VCKExpansion execution layer  ---
echo  [4]  HelloExample               - minimal FrameScheduler + triangle
echo  [5]  JobGraphExample            - CPU task graph with dependencies
echo  [6]  SchedulerPolicyExample     - switch Lockstep / Pipelined / AsyncMax at runtime
echo  [7]  SubmissionBatchingExample  - 2 cmd buffers, 1 vkQueueSubmit
echo  [8]  TimelineExample            - TimelineSemaphore + DependencyToken smoke test
echo  [9]  DebugTimelineExample       - span recorder + Dump every 120 frames
echo  [A]  All  (builds 1..9 in sequence)
echo  [0]  Exit
echo.
set /p CHOICE=" Select: "

if /i "%CHOICE%"=="A" goto BUILD_ALL
if "%CHOICE%"=="1" goto BUILD_TRIANGLE
if "%CHOICE%"=="2" goto BUILD_MIPMAP
if "%CHOICE%"=="3" goto BUILD_VMM
if "%CHOICE%"=="4" goto BUILD_HELLO
if "%CHOICE%"=="5" goto BUILD_JOBGRAPH
if "%CHOICE%"=="6" goto BUILD_POLICY
if "%CHOICE%"=="7" goto BUILD_BATCHING
if "%CHOICE%"=="8" goto BUILD_TIMELINE
if "%CHOICE%"=="9" goto BUILD_DBGTIMELINE
if "%CHOICE%"=="0" exit /b 0
echo Unknown: %CHOICE%
exit /b 1

:: =============================================================================
:BUILD_TRIANGLE
set EX=RGBTriangle
set ASSETS=%EX%\assets
echo.
echo [%EX%] Compiling shaders...
if not exist "%ASSETS%" mkdir "%ASSETS%"
glslangValidator -V %ASSETS%\triangle.vert -o %ASSETS%\triangle.vert.spv
if errorlevel 1 ( echo FAILED: triangle.vert & exit /b 1 )
glslangValidator -V %ASSETS%\triangle.frag -o %ASSETS%\triangle.frag.spv
if errorlevel 1 ( echo FAILED: triangle.frag & exit /b 1 )
echo [%EX%] Compiling C++...
g++ %EX%\main.cpp %EX%\App.cpp %VKB% -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS%
if errorlevel 1 ( echo FAILED: C++ compilation & exit /b 1 )
echo.
echo  OK  %EX%\%EX%.exe
echo  Run: cd %EX% ^&^& %EX%.exe
goto END

:: =============================================================================
:BUILD_MIPMAP
set EX=MipmapExample
set ASSETS=%EX%\assets
echo.
echo [%EX%] Compiling shaders...
if not exist "%ASSETS%" mkdir "%ASSETS%"
glslangValidator -V %ASSETS%\mip.vert -o %ASSETS%\mip.vert.spv
if errorlevel 1 ( echo FAILED: mip.vert & exit /b 1 )
glslangValidator -V %ASSETS%\mip.frag -o %ASSETS%\mip.frag.spv
if errorlevel 1 ( echo FAILED: mip.frag & exit /b 1 )
echo [%EX%] Compiling C++...
g++ %EX%\main.cpp %EX%\App.cpp %VKB% -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS%
if errorlevel 1 ( echo FAILED: C++ compilation & exit /b 1 )
echo.
echo  OK  %EX%\%EX%.exe
echo  Run: cd %EX% ^&^& %EX%.exe
goto END

:: =============================================================================
:BUILD_VMM
set EX=VMMExample
set ASSETS=%EX%\assets
echo.
echo [%EX%] Compiling shaders...
if not exist "%ASSETS%" mkdir "%ASSETS%"
glslangValidator -V %ASSETS%\vmm.vert -o %ASSETS%\vmm.vert.spv
if errorlevel 1 ( echo FAILED: vmm.vert & exit /b 1 )
glslangValidator -V %ASSETS%\vmm.frag -o %ASSETS%\vmm.frag.spv
if errorlevel 1 ( echo FAILED: vmm.frag & exit /b 1 )
echo [%EX%] Compiling C++...
g++ %EX%\main.cpp %EX%\App.cpp %VKB% ..\VMM\VulkanMemoryManager.cpp -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS%
if errorlevel 1 ( echo FAILED: C++ compilation & exit /b 1 )
echo.
echo  OK  %EX%\%EX%.exe
echo  Run: cd %EX% ^&^& %EX%.exe
goto END

:: =============================================================================
:: VCKExpansion execution-layer examples.
::
:: All six share the same Vertex layout + shader shape, so we reuse a small
:: macro-ish pattern: each label sets EX and the assets file stem, compiles
:: the .vert/.frag, then compiles C++ against the shared VKB list.
:: =============================================================================

:BUILD_HELLO
set EX=HelloExample
set STEM=hello
goto COMPILE_EX_WITH_STEM

:BUILD_JOBGRAPH
set EX=JobGraphExample
set STEM=JobGraphExample
goto COMPILE_EX_WITH_STEM

:BUILD_POLICY
set EX=SchedulerPolicyExample
set STEM=SchedulerPolicyExample
goto COMPILE_EX_WITH_STEM

:BUILD_BATCHING
set EX=SubmissionBatchingExample
set STEM=SubmissionBatchingExample
goto COMPILE_EX_WITH_STEM

:BUILD_TIMELINE
set EX=TimelineExample
set STEM=TimelineExample
goto COMPILE_EX_WITH_STEM

:BUILD_DBGTIMELINE
set EX=DebugTimelineExample
set STEM=DebugTimelineExample
goto COMPILE_EX_WITH_STEM

:COMPILE_EX_WITH_STEM
set ASSETS=%EX%\assets
echo.
echo [%EX%] Compiling shaders...
if not exist "%ASSETS%" mkdir "%ASSETS%"
glslangValidator -V %ASSETS%\%STEM%.vert -o %ASSETS%\%STEM%.vert.spv
if errorlevel 1 ( echo FAILED: %STEM%.vert & exit /b 1 )
glslangValidator -V %ASSETS%\%STEM%.frag -o %ASSETS%\%STEM%.frag.spv
if errorlevel 1 ( echo FAILED: %STEM%.frag & exit /b 1 )
echo [%EX%] Compiling C++...
g++ %EX%\main.cpp %EX%\App.cpp %VKB% -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS%
if errorlevel 1 ( echo FAILED: C++ compilation & exit /b 1 )
echo.
echo  OK  %EX%\%EX%.exe
echo  Run: cd %EX% ^&^& %EX%.exe
goto END

:: =============================================================================
:BUILD_ALL
call :DO_BUILD_TRIANGLE || exit /b 1
call :DO_BUILD_MIPMAP   || exit /b 1
call :DO_BUILD_VMM      || exit /b 1
:: No alignment padding here — in cmd.exe `set VAR=value <spaces>& ...`
:: folds the trailing spaces into the variable's value, which then shows
:: up as `HelloExample              \assets\hello.vert` and breaks the
:: glslangValidator call.  One `set` per line keeps the values clean.
set EX=HelloExample
set STEM=hello
call :DO_COMPILE || exit /b 1
set EX=JobGraphExample
set STEM=JobGraphExample
call :DO_COMPILE || exit /b 1
set EX=SchedulerPolicyExample
set STEM=SchedulerPolicyExample
call :DO_COMPILE || exit /b 1
set EX=SubmissionBatchingExample
set STEM=SubmissionBatchingExample
call :DO_COMPILE || exit /b 1
set EX=TimelineExample
set STEM=TimelineExample
call :DO_COMPILE || exit /b 1
set EX=DebugTimelineExample
set STEM=DebugTimelineExample
call :DO_COMPILE || exit /b 1
echo.
echo  OK  all examples built.
goto END

:DO_BUILD_TRIANGLE
set EX=RGBTriangle
set ASSETS=%EX%\assets
echo.
echo [%EX%] Compiling shaders...
if not exist "%ASSETS%" mkdir "%ASSETS%"
glslangValidator -V %ASSETS%\triangle.vert -o %ASSETS%\triangle.vert.spv || exit /b 1
glslangValidator -V %ASSETS%\triangle.frag -o %ASSETS%\triangle.frag.spv || exit /b 1
echo [%EX%] Compiling C++...
g++ %EX%\main.cpp %EX%\App.cpp %VKB% -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS% || exit /b 1
echo  OK  %EX%\%EX%.exe
exit /b 0

:DO_BUILD_MIPMAP
set EX=MipmapExample
set ASSETS=%EX%\assets
echo.
echo [%EX%] Compiling shaders...
if not exist "%ASSETS%" mkdir "%ASSETS%"
glslangValidator -V %ASSETS%\mip.vert -o %ASSETS%\mip.vert.spv || exit /b 1
glslangValidator -V %ASSETS%\mip.frag -o %ASSETS%\mip.frag.spv || exit /b 1
echo [%EX%] Compiling C++...
g++ %EX%\main.cpp %EX%\App.cpp %VKB% -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS% || exit /b 1
echo  OK  %EX%\%EX%.exe
exit /b 0

:DO_BUILD_VMM
set EX=VMMExample
set ASSETS=%EX%\assets
echo.
echo [%EX%] Compiling shaders...
if not exist "%ASSETS%" mkdir "%ASSETS%"
glslangValidator -V %ASSETS%\vmm.vert -o %ASSETS%\vmm.vert.spv || exit /b 1
glslangValidator -V %ASSETS%\vmm.frag -o %ASSETS%\vmm.frag.spv || exit /b 1
echo [%EX%] Compiling C++...
g++ %EX%\main.cpp %EX%\App.cpp %VKB% ..\VMM\VulkanMemoryManager.cpp -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS% || exit /b 1
echo  OK  %EX%\%EX%.exe
exit /b 0

:DO_COMPILE
set ASSETS=%EX%\assets
echo.
echo [%EX%] Compiling shaders...
if not exist "%ASSETS%" mkdir "%ASSETS%"
glslangValidator -V %ASSETS%\%STEM%.vert -o %ASSETS%\%STEM%.vert.spv || exit /b 1
glslangValidator -V %ASSETS%\%STEM%.frag -o %ASSETS%\%STEM%.frag.spv || exit /b 1
echo [%EX%] Compiling C++...
g++ %EX%\main.cpp %EX%\App.cpp %VKB% -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS% || exit /b 1
echo  OK  %EX%\%EX%.exe
exit /b 0

:END
echo.
endlocal
