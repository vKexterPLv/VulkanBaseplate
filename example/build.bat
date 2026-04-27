@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: =============================================================================
::  VCK example builder (Windows)
:: =============================================================================
::  Run from the repo's example\ folder.  This script is a thin wrapper around
::  CMake + Ninja - the heavy lifting (lib-once compile model, MSVC vs MinGW
::  detection, GLFW + Vulkan-Headers fetch, GLSL -> SPIR-V) lives in
::  example\CMakeLists.txt.  We only do two things here:
::
::    1. Configure once     -> cmake -S . -B build -G Ninja
::    2. Build by menu pick -> cmake --build build -j --target <Name>
::
::  Toolchain selection is automatic: Ninja picks whichever C++ compiler is on
::  PATH.  Run from a Developer Command Prompt (cl + lib + link on PATH) and
::  Ninja uses MSVC; otherwise MinGW-w64's g++ is picked up from MSYS2's
::  C:\msys64\mingw64\bin or from PATH.  No more --toolchain flag, no more
::  cmd-batch arg-parsing quirks.
::
::  Requirements:
::    - VULKAN_SDK env var pointing at the Vulkan SDK root
::    - cmake 3.20+ + ninja on PATH
::    - either MSVC cl on PATH (Developer Cmd Prompt) or MinGW g++ on PATH
::    - glslangValidator on PATH (ships with the Vulkan SDK)
::
::  Menu / non-interactive forms:
::    build.bat            - interactive menu
::    build.bat A          - build all 14 examples
::    build.bat T          - build + run the R14 unit-test harness
::    build.bat 1..14      - build a single example by number
::    build.bat 0          - exit
:: =============================================================================

:: Colour codes (best-effort - falls back to plain text on consoles that
:: don't honour ANSI).
for /f %%a in ('"prompt $E$ & for %%b in (1) do rem"') do set "ESC=%%a"
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

set "CHOICE=%~1"

if "%VULKAN_SDK%"=="" (
    call :BANNER
    call :ERR "VULKAN_SDK environment variable is not set."
    echo Install the LunarG Vulkan SDK and re-open cmd, or run vcvarsall.bat.
    exit /b 1
)

where cmake >nul 2>&1 || ( call :BANNER & call :ERR "cmake not found on PATH." & exit /b 1 )
where ninja >nul 2>&1 || ( call :BANNER & call :ERR "ninja not found on PATH.  Install via 'choco install ninja' or scoop." & exit /b 1 )

if "%CHOICE%"=="" goto MENU
goto DISPATCH

:MENU
call :BANNER
echo  %C_BOLD%%C_WHT%Raw core%C_RESET%                %C_DIM%(pure Vulkan, hand-recorded commands)%C_RESET%
echo    %C_YEL%[1]%C_RESET%  %C_WHT%RGBTriangle%C_RESET%                 hello triangle, manual everything
echo    %C_YEL%[2]%C_RESET%  %C_WHT%MipmapExample%C_RESET%               mip chain generation and sampling
echo.
echo  %C_BOLD%%C_WHT%Plus VMM%C_RESET%                %C_DIM%(persistent / transient / frame-buffered)%C_RESET%
echo    %C_YEL%[3]%C_RESET%  %C_WHT%VMMExample%C_RESET%                  VMM all three layers
echo    %C_YEL%[4]%C_RESET%  %C_WHT%SecondaryCmdExample%C_RESET%         secondary command buffers + scheduler-aware resize (v0.3)
echo.
echo  %C_BOLD%%C_WHT%Plus debug instrumentation%C_RESET%
echo    %C_YEL%[5]%C_RESET%  %C_WHT%DebugTimelineExample%C_RESET%        span recorder + Dump every 120 frames
echo    %C_YEL%[6]%C_RESET%  %C_WHT%DebugShowcaseExample%C_RESET%        VCKLog levels / dedup / VK_CHECK / debug toggle
echo.
echo  %C_BOLD%%C_WHT%Plus AA%C_RESET%                  %C_DIM%(MSAA / FXAA expansion)%C_RESET%
echo    %C_YEL%[7]%C_RESET%  %C_WHT%AAShowcaseExample%C_RESET%           MSAA + FXAA + post pipeline
echo.
echo  %C_BOLD%%C_WHT%Execution layer%C_RESET%         %C_DIM%(JobGraph / batching / timeline / policy)%C_RESET%
echo    %C_YEL%[8]%C_RESET%  %C_WHT%JobGraphExample%C_RESET%             CPU task graph with dependencies
echo    %C_YEL%[9]%C_RESET%  %C_WHT%SubmissionBatchingExample%C_RESET%   2 cmd buffers, 1 vkQueueSubmit
echo    %C_YEL%[10]%C_RESET% %C_WHT%TimelineExample%C_RESET%             DependencyToken / GPU wait chains
echo    %C_YEL%[11]%C_RESET% %C_WHT%SchedulerPolicyExample%C_RESET%      Pipelined / AsyncMax policies
echo.
echo  %C_BOLD%%C_WHT%Mostly VCK%C_RESET%              %C_DIM%(ergonomic API does the work)%C_RESET%
echo    %C_YEL%[12]%C_RESET% %C_WHT%HelloExample%C_RESET%                minimal FrameScheduler + triangle
echo    %C_YEL%[13]%C_RESET% %C_WHT%EasyCubeExample%C_RESET%             Primitives::Cube + VertexLayout + PushConstants + VCKMath
echo    %C_YEL%[14]%C_RESET% %C_WHT%ShaderToolingExample%C_RESET%        ShaderLoader / Watcher / SpecConstants / Stage / Interface
echo.
echo    %C_YEL%[A]%C_RESET%  %C_BOLD%Build all%C_RESET%
echo    %C_YEL%[T]%C_RESET%  %C_BOLD%R14 unit-test harness (build + run)%C_RESET%
echo    %C_YEL%[0]%C_RESET%  %C_DIM%Exit%C_RESET%
echo.
set /p "CHOICE=> "

:DISPATCH
if /i "%CHOICE%"=="0" exit /b 0

call :CONFIGURE || exit /b 1

if /i "%CHOICE%"=="A" (
    cmake --build build -j --target examples
    exit /b !ERRORLEVEL!
)
if /i "%CHOICE%"=="T" (
    cmake --build build -j --target vck_tests || exit /b !ERRORLEVEL!
    "%~dp0..\tests\vck_tests.exe"
    exit /b !ERRORLEVEL!
)

set "TARGET="
if "%CHOICE%"=="1"  set "TARGET=RGBTriangle"
if "%CHOICE%"=="2"  set "TARGET=MipmapExample"
if "%CHOICE%"=="3"  set "TARGET=VMMExample"
if "%CHOICE%"=="4"  set "TARGET=SecondaryCmdExample"
if "%CHOICE%"=="5"  set "TARGET=DebugTimelineExample"
if "%CHOICE%"=="6"  set "TARGET=DebugShowcaseExample"
if "%CHOICE%"=="7"  set "TARGET=AAShowcaseExample"
if "%CHOICE%"=="8"  set "TARGET=JobGraphExample"
if "%CHOICE%"=="9"  set "TARGET=SubmissionBatchingExample"
if "%CHOICE%"=="10" set "TARGET=TimelineExample"
if "%CHOICE%"=="11" set "TARGET=SchedulerPolicyExample"
if "%CHOICE%"=="12" set "TARGET=HelloExample"
if "%CHOICE%"=="13" set "TARGET=EasyCubeExample"
if "%CHOICE%"=="14" set "TARGET=ShaderToolingExample"

if "%TARGET%"=="" (
    call :ERR "unknown selection '%CHOICE%'.  Pick 1-14, A, T, or 0."
    exit /b 1
)

cmake --build build -j --target %TARGET%
exit /b %ERRORLEVEL%

:: ── Subroutines ----------------------------------------------------------
:CONFIGURE
:: Configure once.  CMake itself only re-runs the generator when CMakeLists
:: changed, so re-running this on every invocation is essentially free
:: (Ninja's null-build).
if not exist "build\CMakeCache.txt" (
    echo %C_DIM%[cmake] configuring (one-time)...%C_RESET%
    cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
exit /b 0

:BANNER
echo.
echo %C_BOLD%%C_CYN%VCK%C_RESET% %C_DIM%- example builder (Windows / cmake + ninja)%C_RESET%
echo.
exit /b 0

:ERR
echo %C_RED%ERROR:%C_RESET% %~1
exit /b 1
