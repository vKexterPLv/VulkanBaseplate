@echo off
:: =============================================================================
::  compile.bat — compile MipmapExample shaders to SPIR-V
::
::  Run from the assets/ folder.
::  Copy the output .spv files into the MipmapExample root (same folder as
::  the .exe) so LoadSpv("mip.vert.spv") resolves correctly.
:: =============================================================================

where glslangValidator >nul 2>&1
if errorlevel 1 (
    echo ERROR: glslangValidator not found. Add the Vulkan SDK bin to PATH.
    exit /b 1
)

echo Compiling mip.vert ...
glslangValidator -V mip.vert -o mip.vert.spv
if errorlevel 1 ( echo FAILED: mip.vert & exit /b 1 )

echo Compiling mip.frag ...
glslangValidator -V mip.frag -o mip.frag.spv
if errorlevel 1 ( echo FAILED: mip.frag & exit /b 1 )

echo Done. Copy mip.vert.spv and mip.frag.spv next to MipmapExample.exe
