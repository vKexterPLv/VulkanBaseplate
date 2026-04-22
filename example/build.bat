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
echo  [1]  RGBTriangle    - coloured triangle, live resize
echo  [2]  MipmapExample  - mip chain generation and sampling
echo  [3]  VMMExample     - VulkanMemoryManager all three layers
echo  [0]  Exit
echo.
set /p CHOICE=" Select: "

if "%CHOICE%"=="1" goto BUILD_TRIANGLE
if "%CHOICE%"=="2" goto BUILD_MIPMAP
if "%CHOICE%"=="3" goto BUILD_VMM
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

:END
echo.
endlocal
