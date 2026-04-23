@echo off
setlocal enabledelayedexpansion

:: Switch the console to UTF-8 so em-dashes, box-drawing chars, and other
:: non-ASCII printed by glslang / g++ / our own echo lines don't render as
:: cp850 mojibake (e.g. "ÔÇö" in place of "-").
chcp 65001 >nul 2>&1

:: =============================================================================
::  VCK example builder
:: =============================================================================
::  Run from the repo's example\ folder.  Requires:
::    - VULKAN_SDK env var pointing at your Vulkan SDK root
::    - glslangValidator on PATH  (ships with the SDK)
::    - g++ on PATH               (MinGW-w64)
::    - vendor\glfw, vendor\vma populated (source headers, commit in repo)
::    - example\deps\libglfw3.a   (Windows pre-compiled, download only)
::
::  Dependency layout:
::    vendor\                         (VCK's own source deps - in repo)
::      vma\vk_mem_alloc.h                      (AMD VMA allocator header)
::      glfw\include\GLFW\glfw3.h               (GLFW C API)
::      glfw\include\GLFW\glfw3native.h
::      vulkan_headers\vulkan\*.h               (Vulkan SDK headers mirror)
::    example\
::      deps\
::        libglfw3.a                            (Windows pre-compiled lib)
::
::  GLFW lib: download the "Windows pre-compiled binaries" from
::        https://www.glfw.org and copy
::        lib-mingw-w64\libglfw3.a → example\deps\libglfw3.a
::
::  Linux / macOS: use example\build.sh instead - it pulls GLFW/Vulkan
::        via pkg-config, no vendor\glfw or example\deps needed.
:: =============================================================================

:: ── ANSI colour setup --------------------------------------------------------
:: Capture a literal ESC byte into %ESC% so we can emit ANSI escape sequences.
:: Works on Windows 10 1511+ conhost / Terminal.  Older shells print raw codes,
:: which is ugly but not broken.
for /f %%E in ('"prompt $E & for %%a in (1) do rem"') do set "ESC=%%E"
set "C_RESET=%ESC%[0m"
set "C_BOLD=%ESC%[1m"
set "C_DIM=%ESC%[2m"
set "C_RED=%ESC%[91m"
set "C_GRN=%ESC%[92m"
set "C_YEL=%ESC%[93m"
set "C_BLU=%ESC%[94m"
set "C_MAG=%ESC%[95m"
set "C_CYN=%ESC%[96m"
set "C_WHT=%ESC%[97m"

:: ── Precondition checks ------------------------------------------------------
if "%VULKAN_SDK%"=="" (
    call :BANNER
    call :ERR "VULKAN_SDK is not set.  Install the Vulkan SDK and set VULKAN_SDK."
    exit /b 1
)
where glslangValidator >nul 2>&1
if errorlevel 1 ( call :BANNER & call :ERR "glslangValidator not on PATH." & exit /b 1 )
where g++ >nul 2>&1
if errorlevel 1 ( call :BANNER & call :ERR "g++ not on PATH  (install MinGW-w64)." & exit /b 1 )
if not exist "..\vendor\vma\vk_mem_alloc.h" (
    call :BANNER
    call :ERR "vendor\vma\vk_mem_alloc.h is missing (should be in repo)."
    exit /b 1
)
if not exist "..\vendor\glfw\include\GLFW\glfw3.h" (
    call :BANNER
    call :ERR "vendor\glfw\include\GLFW\glfw3.h is missing (should be in repo)."
    exit /b 1
)
if not exist "deps\libglfw3.a" (
    call :BANNER
    call :ERR "example\deps\libglfw3.a is missing."
    echo        Grab the GLFW Windows pre-compiled from https://www.glfw.org
    echo        and copy lib-mingw-w64\libglfw3.a into example\deps\libglfw3.a
    exit /b 1
)

:: ── Shared flags -------------------------------------------------------------
:: -DGLFW_INCLUDE_VULKAN makes every TU's first GLFW include pull vulkan.h, so
:: glfwCreateWindowSurface (gated on VK_VERSION_1_0) is always declared even if
:: user code includes <GLFW/glfw3.h> before "VCK.h" / "VCKCrossplatform.h".
set DEFINES=-DGLFW_INCLUDE_VULKAN
set INCLUDES=-I..\vendor\vma -I..\vendor\glfw\include -Ideps -I.. -I..\layers\core -I..\layers\expansion -I..\layers\execution -I..\vendor\vulkan_headers -I"%VULKAN_SDK%\Include" %DEFINES%
set LIBS=-Ldeps -L"%VULKAN_SDK%\Lib" -lvulkan-1 -lglfw3 -lgdi32 -luser32 -lshell32
set VKB=..\layers\core\VmaImpl.cpp ..\layers\core\VulkanBuffer.cpp ..\layers\core\VulkanCommand.cpp ..\layers\core\VulkanContext.cpp ..\layers\core\VulkanDevice.cpp ..\layers\core\VulkanImage.cpp ..\layers\core\VulkanPipeline.cpp ..\layers\core\VulkanSwapchain.cpp ..\layers\core\VulkanSync.cpp ..\layers\core\VCKCrossplatform.cpp ..\layers\expansion\VCKExpansion.cpp ..\layers\execution\VCKExecution.cpp

:: ── Banner + menu ------------------------------------------------------------
call :BANNER

echo  %C_BOLD%%C_WHT%Core reference%C_RESET%          %C_DIM%(core VulkanSync / VulkanCommand path)%C_RESET%
echo    %C_YEL%[1]%C_RESET%  %C_WHT%RGBTriangle%C_RESET%                 coloured triangle, live resize
echo    %C_YEL%[2]%C_RESET%  %C_WHT%MipmapExample%C_RESET%               mip chain generation and sampling
echo    %C_YEL%[3]%C_RESET%  %C_WHT%VMMExample%C_RESET%                  VMM all three layers
echo.
echo  %C_BOLD%%C_WHT%VCKExpansion execution layer%C_RESET%
echo    %C_YEL%[4]%C_RESET%  %C_WHT%HelloExample%C_RESET%                minimal FrameScheduler + triangle
echo    %C_YEL%[5]%C_RESET%  %C_WHT%JobGraphExample%C_RESET%             CPU task graph with dependencies
echo    %C_YEL%[6]%C_RESET%  %C_WHT%SchedulerPolicyExample%C_RESET%      Lockstep / Pipelined / AsyncMax at runtime
echo    %C_YEL%[7]%C_RESET%  %C_WHT%SubmissionBatchingExample%C_RESET%   2 cmd buffers, 1 vkQueueSubmit
echo    %C_YEL%[8]%C_RESET%  %C_WHT%TimelineExample%C_RESET%             TimelineSemaphore + DependencyToken
echo    %C_YEL%[9]%C_RESET%  %C_WHT%DebugTimelineExample%C_RESET%        span recorder + Dump every 120 frames
echo.
echo    %C_CYN%[A]%C_RESET%  %C_WHT%Build all%C_RESET%                   in order, stops on first failure
echo    %C_CYN%[0]%C_RESET%  %C_WHT%Exit%C_RESET%
echo.
set /p CHOICE=   %C_BOLD%%C_CYN%select^>%C_RESET% 

if /i "%CHOICE%"=="A" goto BUILD_ALL
if "%CHOICE%"=="1" goto BUILD_TRIANGLE
if "%CHOICE%"=="2" goto BUILD_MIPMAP
if "%CHOICE%"=="3" goto BUILD_VMM
if "%CHOICE%"=="4" ( set EX=HelloExample              & set STEM=hello                     & goto BUILD_ONE )
if "%CHOICE%"=="5" ( set EX=JobGraphExample           & set STEM=JobGraphExample            & goto BUILD_ONE )
if "%CHOICE%"=="6" ( set EX=SchedulerPolicyExample    & set STEM=SchedulerPolicyExample     & goto BUILD_ONE )
if "%CHOICE%"=="7" ( set EX=SubmissionBatchingExample & set STEM=SubmissionBatchingExample  & goto BUILD_ONE )
if "%CHOICE%"=="8" ( set EX=TimelineExample           & set STEM=TimelineExample            & goto BUILD_ONE )
if "%CHOICE%"=="9" ( set EX=DebugTimelineExample      & set STEM=DebugTimelineExample       & goto BUILD_ONE )
if "%CHOICE%"=="0" exit /b 0
call :ERR "unknown selection '%CHOICE%'"
exit /b 1


:: =============================================================================
::  Single-example builds
:: =============================================================================

:BUILD_TRIANGLE
set EX=RGBTriangle
set STEM=triangle
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1
call :OK_RUN
goto END

:BUILD_MIPMAP
set EX=MipmapExample
set STEM=mip
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1
call :OK_RUN
goto END

:BUILD_VMM
set EX=VMMExample
set STEM=vmm
call :COMPILE_SHADERS                 || exit /b 1
call :COMPILE_CPP_WITH_VMM            || exit /b 1
call :OK_RUN
goto END

:BUILD_ONE
:: Trim any trailing space left by the inline ( ... & ... ) assignment.
for /f "tokens=* delims= " %%A in ("!EX!")   do set "EX=%%A"
for /f "tokens=* delims= " %%A in ("!STEM!") do set "STEM=%%A"
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1
call :OK_RUN
goto END


:: =============================================================================
::  Build-all  (inlined to preserve on-screen colour output)
:: =============================================================================

:BUILD_ALL
call :STEP "[1/9] RGBTriangle"
set EX=RGBTriangle
set STEM=triangle
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1

call :STEP "[2/9] MipmapExample"
set EX=MipmapExample
set STEM=mip
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1

call :STEP "[3/9] VMMExample"
set EX=VMMExample
set STEM=vmm
call :COMPILE_SHADERS          || exit /b 1
call :COMPILE_CPP_WITH_VMM     || exit /b 1

call :STEP "[4/9] HelloExample"
set EX=HelloExample
set STEM=hello
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1

call :STEP "[5/9] JobGraphExample"
set EX=JobGraphExample
set STEM=JobGraphExample
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1

call :STEP "[6/9] SchedulerPolicyExample"
set EX=SchedulerPolicyExample
set STEM=SchedulerPolicyExample
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1

call :STEP "[7/9] SubmissionBatchingExample"
set EX=SubmissionBatchingExample
set STEM=SubmissionBatchingExample
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1

call :STEP "[8/9] TimelineExample"
set EX=TimelineExample
set STEM=TimelineExample
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1

call :STEP "[9/9] DebugTimelineExample"
set EX=DebugTimelineExample
set STEM=DebugTimelineExample
call :COMPILE_SHADERS || exit /b 1
call :COMPILE_CPP     || exit /b 1

echo.
echo %C_GRN%  all 9 examples built.%C_RESET%
echo.
goto END


:: =============================================================================
::  Helpers  (called as :LABEL)
:: =============================================================================

:BANNER
echo.
echo  %C_CYN%+---------------------------------------------------------+%C_RESET%
echo  %C_CYN%^|%C_RESET%   %C_BOLD%%C_WHT%V C K%C_RESET%   %C_DIM%Vulkan Core Kit  -  example builder%C_RESET%    %C_CYN%^|%C_RESET%
echo  %C_CYN%+---------------------------------------------------------+%C_RESET%
echo.
exit /b 0

:STEP
:: %~1 = header text, e.g. "[4/9] HelloExample"
:: Escape the literal '>' with '^>' so cmd.exe doesn't try to redirect output
:: (which produces the cosmetic 'filename, directory name or volume label
::  syntax is incorrect' error before any command runs).
echo.
echo %C_MAG%^>%C_RESET% %C_BOLD%%~1%C_RESET%
exit /b 0

:ERR
:: %~1 = error text
echo.
echo %C_RED%  ERROR:%C_RESET% %~1
echo.
exit /b 0

:COMPILE_SHADERS
set ASSETS=%EX%\assets
if not exist "%ASSETS%" mkdir "%ASSETS%"
echo   %C_DIM%glslang %STEM%.vert%C_RESET%
glslangValidator -V %ASSETS%\%STEM%.vert -o %ASSETS%\%STEM%.vert.spv >nul
if errorlevel 1 ( call :ERR "shader compile failed: %STEM%.vert" & exit /b 1 )
echo   %C_DIM%glslang %STEM%.frag%C_RESET%
glslangValidator -V %ASSETS%\%STEM%.frag -o %ASSETS%\%STEM%.frag.spv >nul
if errorlevel 1 ( call :ERR "shader compile failed: %STEM%.frag" & exit /b 1 )
exit /b 0

:COMPILE_CPP
echo   %C_DIM%g++      %EX%.exe%C_RESET%
g++ %EX%\main.cpp %EX%\App.cpp %VKB% -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS%
if errorlevel 1 ( call :ERR "C++ compile failed: %EX%" & exit /b 1 )
echo   %C_GRN%  OK%C_RESET%   %EX%\%EX%.exe
exit /b 0

:COMPILE_CPP_WITH_VMM
echo   %C_DIM%g++      %EX%.exe  (+VMM)%C_RESET%
g++ %EX%\main.cpp %EX%\App.cpp %VKB% ..\layers\vmm\VulkanMemoryManager.cpp -o %EX%\%EX%.exe -std=c++17 %INCLUDES% %LIBS%
if errorlevel 1 ( call :ERR "C++ compile failed: %EX%" & exit /b 1 )
echo   %C_GRN%  OK%C_RESET%   %EX%\%EX%.exe
exit /b 0

:OK_RUN
echo.
echo   %C_CYN%run:%C_RESET%   cd %EX%  ^&^&  %EX%.exe
echo.
exit /b 0

:END
endlocal
