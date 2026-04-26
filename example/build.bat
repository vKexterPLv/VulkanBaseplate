@echo off
setlocal enabledelayedexpansion

:: Switch the console to UTF-8 so em-dashes, box-drawing chars, and other
:: non-ASCII printed by glslang / cl / g++ / our own echo lines don't render
:: as cp850 mojibake (e.g. "ÔÇö" in place of "-").
chcp 65001 >nul 2>&1

:: =============================================================================
::  VCK example builder (Windows)
:: =============================================================================
::  Run from the repo's example\ folder.  Two toolchains supported:
::
::    --toolchain cl   Microsoft Visual C++  (cl.exe + lib.exe + link.exe).
::                      Faster: /MP parallelises TU compile across cores.
::                      Auto-detected via vswhere.exe when running from a
::                      regular cmd.exe; if cl is already on PATH (you ran
::                      from a Developer Command Prompt) we just use it.
::                      Requires example\deps\glfw3.lib (MSVC pre-compiled
::                      from https://www.glfw.org -> lib-vc2022\glfw3.lib).
::
::    --toolchain gcc  MinGW-w64 g++ + ar  (canonical reference toolchain
::                      through v0.3).  Requires g++ on PATH and
::                      example\deps\libglfw3.a (MinGW pre-compiled,
::                      lib-mingw-w64\libglfw3.a).
::
::    --toolchain auto default; prefer cl when available, else gcc.
::                     The legacy '--toolchain=cl' / '=gcc' / '=auto' forms are
::                     also accepted; cmd's batch arg parser truncates at the
::                     first '=' on some hosts (e.g. github-actions runner),
::                     so the space-separated form is preferred.
::
::  Common requirements:
::    - VULKAN_SDK env var pointing at your Vulkan SDK root
::    - glslangValidator on PATH  (ships with the SDK)
::    - vendor\glfw, vendor\vma populated (source headers, commit in repo)
::
::  PR #7: lib-once compile model
::  -----------------------------
::  Stage 1: build the VCK static library once into  build\libvck.a (gcc)
::           or build\vck.lib (cl).  All 12 VKB sources + VulkanMemoryManager.
::  Stage 2: per example, compile only main.cpp + App.cpp (2 TUs) and link
::           against the prebuilt library.  Cuts ~143 redundant TUs out of
::           build-all (13 examples * 11 redundant VKB compiles).
::  Stage 3: optional [T] target builds tests\*.cpp + R14 harness against
::           the same library (see tests\ directory).
::
::  Dependency layout:
::    vendor\                         (VCK's own source deps - in repo)
::      vma\vk_mem_alloc.h                      (AMD VMA allocator header)
::      glfw\include\GLFW\glfw3.h               (GLFW C API)
::      glfw\include\GLFW\glfw3native.h
::      vulkan_headers\vulkan\*.h               (Vulkan SDK headers mirror)
::    example\
::      build\                                  (gitignored, compiler scratch +
::                                               libvck.a / vck.lib + stamp)
::      deps\
::        libglfw3.a                            (MinGW pre-compiled, gcc path)
::        glfw3.lib                             (MSVC pre-compiled, cl path)
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

:: ── Argument parsing --------------------------------------------------------
:: Supports any order:
::    build.bat A --toolchain cl
::    build.bat --toolchain cl A
::    build.bat --toolchain=cl A           (= form too, when cmd preserves it)
::
:: cmd.exe is hostile to '=' in batch args.  In some invocation contexts (the
:: github-actions runner happens to be one), cmd silently splits an arg at
:: the first '=', so '--toolchain=cl' arrives as %1='--toolchain' with cl
:: lost entirely - exactly what bit PR #7's first two pushes.  We accept BOTH
:: '--toolchain VALUE' (preferred, robust) and '--toolchain=VALUE' (legacy,
:: works when cmd cooperates).
set "TOOLCHAIN=auto"
set "CHOICE="
:PARSE_ARGS
if "%~1"=="" goto PARSE_DONE
if /i "%~1"=="--toolchain" (
    if "%~2"=="" (
        call :BANNER
        call :ERR "--toolchain requires a value: cl, gcc, or auto."
        exit /b 1
    )
    set "TOOLCHAIN=%~2"
    shift
    shift
    goto PARSE_ARGS
)
if /i "%~1"=="--toolchain=cl"   ( set "TOOLCHAIN=cl"   & shift & goto PARSE_ARGS )
if /i "%~1"=="--toolchain=gcc"  ( set "TOOLCHAIN=gcc"  & shift & goto PARSE_ARGS )
if /i "%~1"=="--toolchain=auto" ( set "TOOLCHAIN=auto" & shift & goto PARSE_ARGS )
if "%CHOICE%"=="" set "CHOICE=%~1"
shift
goto PARSE_ARGS
:PARSE_DONE

:: ── Common preconditions ---------------------------------------------------
if "%VULKAN_SDK%"=="" (
    call :BANNER
    call :ERR "VULKAN_SDK is not set.  Install the Vulkan SDK and set VULKAN_SDK."
    exit /b 1
)
where glslangValidator >nul 2>&1
if errorlevel 1 ( call :BANNER & call :ERR "glslangValidator not on PATH." & exit /b 1 )
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

:: ── Toolchain resolution ---------------------------------------------------
:: 'auto' : prefer cl when reachable (already on PATH OR vswhere finds it),
::          else fall back to g++.  When the user passes --toolchain=cl
::          explicitly we must report a clear error if cl can't be set up
::          rather than silently using g++.
if /i "%TOOLCHAIN%"=="auto" (
    where cl >nul 2>&1
    if !errorlevel!==0 (
        set "TOOLCHAIN=cl"
    ) else (
        call :TRY_VCVARS
        where cl >nul 2>&1
        if !errorlevel!==0 (
            set "TOOLCHAIN=cl"
        ) else (
            where g++ >nul 2>&1
            if !errorlevel!==0 ( set "TOOLCHAIN=gcc" ) else (
                call :BANNER
                call :ERR "neither cl nor g++ is reachable; install MinGW-w64 OR run from a Developer Command Prompt."
                exit /b 1
            )
        )
    )
) else if /i "%TOOLCHAIN%"=="cl" (
    where cl >nul 2>&1
    if not !errorlevel!==0 (
        call :TRY_VCVARS
        where cl >nul 2>&1
        if not !errorlevel!==0 (
            call :BANNER
            call :ERR "--toolchain=cl requested but cl.exe not found.  Run from a Developer Command Prompt or install Visual Studio 2019/2022."
            exit /b 1
        )
    )
) else if /i "%TOOLCHAIN%"=="gcc" (
    where g++ >nul 2>&1
    if not !errorlevel!==0 (
        call :BANNER
        call :ERR "--toolchain=gcc requested but g++ not on PATH.  Install MinGW-w64."
        exit /b 1
    )
) else (
    call :BANNER
    call :ERR "unknown --toolchain='%TOOLCHAIN%'.  Use cl, gcc, or auto."
    exit /b 1
)

:: ── Per-toolchain flags + GLFW lib precondition ----------------------------
set "BUILD_DIR=build"

if /i "%TOOLCHAIN%"=="cl" (
    if not exist "deps\glfw3.lib" (
        call :BANNER
        call :ERR "example\deps\glfw3.lib is missing (cl toolchain)."
        echo        Grab the GLFW Windows pre-compiled from https://www.glfw.org
        echo        and copy lib-vc2022\glfw3.lib into example\deps\glfw3.lib
        exit /b 1
    )
    set "LIB=!BUILD_DIR!\vck.lib"
    set "DEFINES=/DGLFW_INCLUDE_VULKAN /D_CRT_SECURE_NO_WARNINGS"
    :: /W0 silences MSVC warnings (matches gcc -w / build.sh -w).  /MD picks
    :: the dynamically-linked CRT to keep .exes small and match typical
    :: Windows convention.  /Zc:__cplusplus + /permissive- match modern MSVC.
    set "CXXFLAGS=/std:c++17 /EHsc /O2 /MD /nologo /W0 /Zc:__cplusplus /permissive-"
    set "INCLUDES=/I..\vendor\vma /I..\vendor\glfw\include /Ideps /I.. /I..\layers\core /I..\layers\expansion /I..\layers\execution /I..\layers\vmm /I..\vendor\vulkan_headers /I"%VULKAN_SDK%\Include" !DEFINES!"
    set "LIBS=/LIBPATH:deps /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib glfw3.lib gdi32.lib user32.lib shell32.lib"
) else (
    if not exist "deps\libglfw3.a" (
        call :BANNER
        call :ERR "example\deps\libglfw3.a is missing (gcc toolchain)."
        echo        Grab the GLFW Windows pre-compiled from https://www.glfw.org
        echo        and copy lib-mingw-w64\libglfw3.a into example\deps\libglfw3.a
        exit /b 1
    )
    where ar  >nul 2>&1
    if errorlevel 1 ( call :BANNER & call :ERR "ar not on PATH (ships with MinGW-w64; needed for libvck.a)." & exit /b 1 )
    set "LIB=!BUILD_DIR!\libvck.a"
    set "DEFINES=-DGLFW_INCLUDE_VULKAN"
    set "CXXFLAGS=-std=c++17 -O2 -w -Werror=return-type"
    set "INCLUDES=-I..\vendor\vma -I..\vendor\glfw\include -Ideps -I.. -I..\layers\core -I..\layers\expansion -I..\layers\execution -I..\layers\vmm -I..\vendor\vulkan_headers -I"%VULKAN_SDK%\Include" !DEFINES!"
    set "LIBS=-Ldeps -L"%VULKAN_SDK%\Lib" -lvulkan-1 -lglfw3 -lgdi32 -luser32 -lshell32"
)

:: VKB sources baked into the static library.  Order does not matter.
set VKB_LIB=..\layers\core\VmaImpl.cpp ..\layers\core\VulkanBuffer.cpp ..\layers\core\VulkanCommand.cpp ..\layers\core\VulkanContext.cpp ..\layers\core\VulkanDevice.cpp ..\layers\core\VulkanImage.cpp ..\layers\core\VulkanPipeline.cpp ..\layers\core\VulkanSwapchain.cpp ..\layers\core\VulkanSync.cpp ..\layers\core\VCKCrossplatform.cpp ..\layers\expansion\VCKExpansion.cpp ..\layers\execution\VCKExecution.cpp ..\layers\vmm\VulkanMemoryManager.cpp

:: ── Banner + menu ----------------------------------------------------------
call :BANNER

echo  %C_DIM%toolchain: %TOOLCHAIN%   lib: %LIB%%C_RESET%
echo.

echo  %C_BOLD%%C_WHT%Raw core%C_RESET%                %C_DIM%(VulkanSync / VulkanCommand path, you write everything)%C_RESET%
echo    %C_YEL%[1]%C_RESET%  %C_WHT%RGBTriangle%C_RESET%                 coloured triangle, live resize
echo    %C_YEL%[2]%C_RESET%  %C_WHT%MipmapExample%C_RESET%               mip chain generation and sampling
echo    %C_YEL%[3]%C_RESET%  %C_WHT%VMMExample%C_RESET%                  VMM all three layers
echo    %C_YEL%[4]%C_RESET%  %C_WHT%SecondaryCmdExample%C_RESET%         secondary command buffers + scheduler-aware resize (v0.3)
echo.
echo  %C_BOLD%%C_WHT%Debug + tooling%C_RESET%         %C_DIM%(opt-in instrumentation, still hand-records)%C_RESET%
echo    %C_YEL%[5]%C_RESET%  %C_WHT%DebugTimelineExample%C_RESET%        span recorder + Dump every 120 frames
echo    %C_YEL%[6]%C_RESET%  %C_WHT%DebugShowcaseExample%C_RESET%        VCKLog levels / dedup / VK_CHECK / debug toggle
echo.
echo  %C_BOLD%%C_WHT%Expansion%C_RESET%               %C_DIM%(AA / framebuffer / sampler wiring)%C_RESET%
echo    %C_YEL%[7]%C_RESET%  %C_WHT%AAShowcaseExample%C_RESET%           AATechnique decision matrix + auto-pick + triangle
echo.
echo  %C_BOLD%%C_WHT%Execution layer%C_RESET%         %C_DIM%(JobGraph / batching / timeline / policy)%C_RESET%
echo    %C_YEL%[8]%C_RESET%  %C_WHT%JobGraphExample%C_RESET%             CPU task graph with dependencies
echo    %C_YEL%[9]%C_RESET%  %C_WHT%SubmissionBatchingExample%C_RESET%   2 cmd buffers, 1 vkQueueSubmit
echo   %C_YEL%[10]%C_RESET% %C_WHT%TimelineExample%C_RESET%             TimelineSemaphore + DependencyToken
echo   %C_YEL%[11]%C_RESET% %C_WHT%SchedulerPolicyExample%C_RESET%      Lockstep / Pipelined / AsyncMax at runtime
echo.
echo  %C_BOLD%%C_WHT%Mostly VCK%C_RESET%              %C_DIM%(ergonomic API does the work)%C_RESET%
echo   %C_YEL%[12]%C_RESET% %C_WHT%HelloExample%C_RESET%                minimal FrameScheduler + triangle
echo   %C_YEL%[13]%C_RESET% %C_WHT%EasyCubeExample%C_RESET%             Primitives::Cube + VertexLayout + PushConstants + VCKMath
echo.
echo    %C_CYN%[A]%C_RESET%  %C_WHT%Build all%C_RESET%                   in order, stops on first failure
echo    %C_CYN%[T]%C_RESET%  %C_WHT%R14 unit tests%C_RESET%              build + show run command (PR #7)
echo    %C_CYN%[0]%C_RESET%  %C_WHT%Exit%C_RESET%
echo.

if "!CHOICE!"=="" (
    set /p CHOICE=   %C_BOLD%%C_CYN%select^>%C_RESET% 
)

if /i "%CHOICE%"=="A" goto BUILD_ALL
if /i "%CHOICE%"=="T" goto BUILD_TESTS
if "%CHOICE%"=="1"  ( set EX=RGBTriangle               & set STEM=triangle                   & goto BUILD_ONE )
if "%CHOICE%"=="2"  ( set EX=MipmapExample             & set STEM=mip                        & goto BUILD_ONE )
if "%CHOICE%"=="3"  ( set EX=VMMExample                & set STEM=vmm                        & goto BUILD_ONE )
if "%CHOICE%"=="4"  ( set EX=SecondaryCmdExample       & set STEM=secondary                  & goto BUILD_ONE )
if "%CHOICE%"=="5"  ( set EX=DebugTimelineExample      & set STEM=DebugTimelineExample       & goto BUILD_ONE )
if "%CHOICE%"=="6"  ( set EX=DebugShowcaseExample      & set STEM=                           & goto BUILD_ONE )
if "%CHOICE%"=="7"  ( set EX=AAShowcaseExample         & set STEM=aa                         & goto BUILD_ONE )
if "%CHOICE%"=="8"  ( set EX=JobGraphExample           & set STEM=JobGraphExample            & goto BUILD_ONE )
if "%CHOICE%"=="9"  ( set EX=SubmissionBatchingExample & set STEM=SubmissionBatchingExample  & goto BUILD_ONE )
if "%CHOICE%"=="10" ( set EX=TimelineExample           & set STEM=TimelineExample            & goto BUILD_ONE )
if "%CHOICE%"=="11" ( set EX=SchedulerPolicyExample    & set STEM=SchedulerPolicyExample     & goto BUILD_ONE )
if "%CHOICE%"=="12" ( set EX=HelloExample              & set STEM=hello                      & goto BUILD_ONE )
if "%CHOICE%"=="13" ( set EX=EasyCubeExample           & set STEM=easycube                   & goto BUILD_ONE )
if "%CHOICE%"=="0" exit /b 0
call :ERR "unknown selection '%CHOICE%'"
exit /b 1


:: =============================================================================
::  Single-example build  (lib-once + per-example main+App)
:: =============================================================================

:BUILD_ONE
:: Trim any trailing space left by the inline ( ... & ... ) assignment.
for /f "tokens=* delims= " %%A in ("!EX!")   do set "EX=%%A"
for /f "tokens=* delims= " %%A in ("!STEM!") do set "STEM=%%A"
call :BUILD_LIB        || exit /b 1
if not "!STEM!"=="" (
    call :COMPILE_SHADERS || exit /b 1
)
call :LINK_EXAMPLE     || exit /b 1
call :OK_RUN
goto END


:: =============================================================================
::  Build-all  (inlined to preserve on-screen colour output)
:: =============================================================================

:BUILD_ALL
call :BUILD_LIB || exit /b 1

call :STEP "[1/13] RGBTriangle"
set EX=RGBTriangle
set STEM=triangle
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[2/13] MipmapExample"
set EX=MipmapExample
set STEM=mip
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[3/13] VMMExample"
set EX=VMMExample
set STEM=vmm
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[4/13] SecondaryCmdExample"
set EX=SecondaryCmdExample
set STEM=secondary
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[5/13] DebugTimelineExample"
set EX=DebugTimelineExample
set STEM=DebugTimelineExample
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[6/13] DebugShowcaseExample"
set EX=DebugShowcaseExample
set STEM=
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[7/13] AAShowcaseExample"
set EX=AAShowcaseExample
set STEM=aa
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[8/13] JobGraphExample"
set EX=JobGraphExample
set STEM=JobGraphExample
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[9/13] SubmissionBatchingExample"
set EX=SubmissionBatchingExample
set STEM=SubmissionBatchingExample
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[10/13] TimelineExample"
set EX=TimelineExample
set STEM=TimelineExample
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[11/13] SchedulerPolicyExample"
set EX=SchedulerPolicyExample
set STEM=SchedulerPolicyExample
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[12/13] HelloExample"
set EX=HelloExample
set STEM=hello
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

call :STEP "[13/13] EasyCubeExample"
set EX=EasyCubeExample
set STEM=easycube
call :COMPILE_SHADERS || exit /b 1
call :LINK_EXAMPLE    || exit /b 1

echo.
echo %C_GRN%  all 13 examples built.%C_RESET%   %C_DIM%(toolchain: %TOOLCHAIN%)%C_RESET%
echo.
goto END


:: =============================================================================
::  Tests  (R14 harness, PR #7)
:: =============================================================================

:BUILD_TESTS
if not exist "..\tests" (
    call :ERR "..\tests\ directory not found - R14 harness not in this checkout."
    exit /b 1
)
call :BUILD_LIB || exit /b 1
call :STEP "[T] R14 unit tests"
if /i "%TOOLCHAIN%"=="cl" (
    echo   %C_DIM%cl       vck_tests.exe (link vck.lib)%C_RESET%
    cl %CXXFLAGS% %INCLUDES% /I..\tests ..\tests\*.cpp /Fe:..\tests\vck_tests.exe /Fobuild\tests\ /link "%LIB%" %LIBS%
) else (
    echo   %C_DIM%g++      vck_tests (link libvck.a)%C_RESET%
    g++ ..\tests\*.cpp -o ..\tests\vck_tests.exe %CXXFLAGS% %INCLUDES% -I..\tests "%LIB%" %LIBS%
)
if errorlevel 1 ( call :ERR "test compile failed" & exit /b 1 )
echo   %C_GRN%  OK%C_RESET%   ..\tests\vck_tests.exe
echo.
echo   %C_CYN%run:%C_RESET%   ..\tests\vck_tests.exe
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
:: %~1 = header text, e.g. "[4/13] HelloExample"
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

:: ── vswhere.exe Visual Studio detection -----------------------------------
:: Microsoft ships vswhere.exe with VS Installer at a stable path.  When it
:: finds an install with the C++ x64 build tools, we call vcvars64.bat in
:: the current process so cl / lib / link resolve.  Silent on failure -
:: callers re-check `where cl` afterwards and surface their own error.
:TRY_VCVARS
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)
if not exist "!VSWHERE!" exit /b 0
for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSINSTALL=%%I"
if not defined VSINSTALL exit /b 0
if not exist "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" exit /b 0
echo   %C_DIM%vswhere: !VSINSTALL!%C_RESET%
echo   %C_DIM%vcvars64.bat ...%C_RESET%
call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b 0

:: ── Stage 1: build vck.lib / libvck.a once --------------------------------
:: Compiles every VKB source and archives them into the static library.
:: Skips the entire stage when the stamp matches the current toolchain +
:: flags fingerprint.  Cuts ~143 redundant TUs out of build-all.
::
:: cl path uses /MP so all VKB TUs compile in parallel inside one cl
:: invocation, which is the headline Windows speedup over MinGW.
:: gcc path is sequential (one g++ -c per source) - same correctness,
:: ~3-4x slower wall-clock.
:BUILD_LIB
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
set "STAMP_NEW=%TOOLCHAIN%|%CXXFLAGS%|%INCLUDES%|VKB-rev-1"
set "STAMP_OLD="
if exist "%BUILD_DIR%\libvck.stamp" (
    for /f "usebackq delims=" %%S in ("%BUILD_DIR%\libvck.stamp") do set "STAMP_OLD=%%S"
)
if exist "%LIB%" (
    if "%STAMP_NEW%"=="%STAMP_OLD%" (
        echo   %C_DIM%%LIB%   up-to-date%C_RESET%
        exit /b 0
    )
)
call :STEP "[lib] %LIB%   (toolchain: %TOOLCHAIN%)"
if exist "%LIB%" del /q "%LIB%"
if /i "%TOOLCHAIN%"=="cl" goto BUILD_LIB_CL
goto BUILD_LIB_GCC

:BUILD_LIB_CL
:: One cl /MP /c invocation compiles every VKB source in parallel.  /Fo with
:: a trailing path emits each .obj into build\.  /Fdbuild\vck.pdb keeps
:: debug info next to the lib (irrelevant for /O2 release builds but the
:: warning is pacified).
echo   %C_DIM%cl /MP /c %%VKB%% -^> build\*.obj%C_RESET%
cl %CXXFLAGS% /c /MP %INCLUDES% /Fo%BUILD_DIR%\ /Fdbuild\vck.pdb %VKB_LIB%
if errorlevel 1 ( call :ERR "cl /MP failed in libvck stage" & exit /b 1 )
echo   %C_DIM%lib /OUT:%LIB%%C_RESET%
lib /nologo /OUT:"%LIB%" %BUILD_DIR%\VmaImpl.obj %BUILD_DIR%\VulkanBuffer.obj %BUILD_DIR%\VulkanCommand.obj %BUILD_DIR%\VulkanContext.obj %BUILD_DIR%\VulkanDevice.obj %BUILD_DIR%\VulkanImage.obj %BUILD_DIR%\VulkanPipeline.obj %BUILD_DIR%\VulkanSwapchain.obj %BUILD_DIR%\VulkanSync.obj %BUILD_DIR%\VCKCrossplatform.obj %BUILD_DIR%\VCKExpansion.obj %BUILD_DIR%\VCKExecution.obj %BUILD_DIR%\VulkanMemoryManager.obj
if errorlevel 1 ( call :ERR "lib failed for vck.lib" & exit /b 1 )
> "%BUILD_DIR%\libvck.stamp" echo %STAMP_NEW%
echo   %C_GRN%  OK%C_RESET%   %LIB%
exit /b 0

:BUILD_LIB_GCC
set OBJS=
for %%S in (%VKB_LIB%) do (
    set "STEM_GCC=%%~nS"
    echo   %C_DIM%g++ -c   !STEM_GCC!%C_RESET%
    g++ -c "%%S" -o "%BUILD_DIR%\!STEM_GCC!.o" %CXXFLAGS% %INCLUDES%
    if errorlevel 1 ( call :ERR "C++ compile failed in libvck.a stage: !STEM_GCC!" & exit /b 1 )
    set "OBJS=!OBJS! "%BUILD_DIR%\!STEM_GCC!.o""
)
echo   %C_DIM%ar  rcs    libvck.a%C_RESET%
ar rcs "%LIB%" %OBJS%
if errorlevel 1 ( call :ERR "ar failed for libvck.a" & exit /b 1 )
> "%BUILD_DIR%\libvck.stamp" echo %STAMP_NEW%
echo   %C_GRN%  OK%C_RESET%   %LIB%
exit /b 0

:: ── Stage 2: link a single example against the static lib -----------------
:: Compiles only main.cpp + App.cpp (2 TUs) and links against the prebuilt
:: archive.  The archiver pulls only referenced object files, so non-VMM
:: examples cost zero extra link time even though the archive bundles
:: VulkanMemoryManager.cpp.
:LINK_EXAMPLE
if /i "%TOOLCHAIN%"=="cl" goto LINK_EXAMPLE_CL
goto LINK_EXAMPLE_GCC

:LINK_EXAMPLE_CL
echo   %C_DIM%cl       %EX% (main+App, link vck.lib)%C_RESET%
if not exist "%BUILD_DIR%\%EX%" mkdir "%BUILD_DIR%\%EX%"
cl %CXXFLAGS% %INCLUDES% %EX%\main.cpp %EX%\App.cpp /Fo%BUILD_DIR%\%EX%\ /Fe%EX%\%EX%.exe /link "%LIB%" %LIBS%
if errorlevel 1 ( call :ERR "cl link failed: %EX%" & exit /b 1 )
echo   %C_GRN%  OK%C_RESET%   %EX%\%EX%.exe
exit /b 0

:LINK_EXAMPLE_GCC
echo   %C_DIM%g++      %EX% (main+App, link libvck.a)%C_RESET%
g++ %EX%\main.cpp %EX%\App.cpp -o %EX%\%EX%.exe %CXXFLAGS% %INCLUDES% "%LIB%" %LIBS%
if errorlevel 1 ( call :ERR "C++ link failed: %EX%" & exit /b 1 )
echo   %C_GRN%  OK%C_RESET%   %EX%\%EX%.exe
exit /b 0

:OK_RUN
echo.
echo   %C_CYN%run:%C_RESET%   cd %EX%  ^&^&  %EX%.exe
echo.
exit /b 0

:END
endlocal
