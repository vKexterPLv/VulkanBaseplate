# =============================================================================
#  VCK examples - CMake build (PR #7)
# =============================================================================
#  Build all 13 examples + the R14 unit-test harness using a single CMake
#  configure step.  Lib-once compile model is automatic: target `vck` is a
#  STATIC library compiled exactly once, every example exe + the test exe
#  link against it.  Shaders compile into each example's assets/ next to the
#  source GLSL.
#
#  Configure:
#      cmake -S example -B build -G Ninja           (Linux / macOS / Windows)
#  Build:
#      cmake --build build -j                       (parallel, all targets)
#  Build one example:
#      cmake --build build -j --target RGBTriangle
#  Build + run tests:
#      cmake --build build -j --target vck_tests
#      ./build/vck_tests          (Linux / macOS)
#      build\vck_tests.exe        (Windows)
#
#  build.bat and build.sh are thin wrappers over `cmake --build`; they keep
#  the [1]-[13]/[A]/[T]/[0] interactive menu.
#
#  Vulkan SDK + glslangValidator are required.  GLFW is fetched + built from
#  source via FetchContent so the example/deps/ folder is no longer needed.
# =============================================================================

cmake_minimum_required(VERSION 3.20)

project(vck_examples
    VERSION     0.3.1
    LANGUAGES   CXX
    DESCRIPTION "VCK Vulkan Core Kit - examples + R14 test harness"
)

# ---------------------------------------------------------------------------
#  Compile settings shared by every target.
# ---------------------------------------------------------------------------
#  C++20 because example App.cpp files use designated initializers
#  ( '.foo = bar' inside aggregate { } ) for VkSomeStructInfo struct
#  literals.  GCC + clang accept these in C++17 mode as a non-standard
#  extension, but MSVC needs /std:c++20 to compile them.
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Default to Release if the user did not pick one.  Examples are user-facing
# programs and we want optimisation on by default; debug builds opt in via
# `cmake -B build -DCMAKE_BUILD_TYPE=Debug`.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Silence the long-tail of cosmetic compiler warnings VCK does not own (VMA's
# unused-parameter, Vulkan headers' anonymous structs, etc.).  This matches
# the build.bat / build.sh defaults; -Werror=return-type stays so genuine
# bugs still bite.  Rule 14 logging contracts are enforced at runtime, not
# at compile time.
if(MSVC)
    add_compile_options(/W0 /Zc:__cplusplus /permissive- /MP)
    #  NOMINMAX: Windows.h defines min(a,b) / max(a,b) as preprocessor
    #  macros that clobber std::min / std::max - VulkanSwapchain.cpp uses
    #  both heavily.  WIN32_LEAN_AND_MEAN trims the windows.h transitive
    #  include set so we don't pull in winsock / GDI / OLE for no reason.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-w -Werror=return-type)
endif()

# ---------------------------------------------------------------------------
#  Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

#  Vulkan-Headers: VCK pins the same SDK tag CI uses (vulkan-sdk-1.4.321.0).
#  Fetch FIRST and Populate-only (do NOT call FetchContent_MakeAvailable),
#  because Vulkan-Headers' own CMakeLists creates the Vulkan::Headers ALIAS
#  target and find_package(Vulkan) would later try to create the same ALIAS
#  -> "another target with the same name already exists" on second pass.
#  We only need the include directory anyway; pulling its targets in adds
#  no value and breaks the build.
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        vulkan-sdk-1.4.321.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(vulkan_headers)
if(NOT vulkan_headers_POPULATED)
    FetchContent_Populate(vulkan_headers)
endif()
set(VCK_VULKAN_HEADERS_INCLUDE ${vulkan_headers_SOURCE_DIR}/include)

find_package(Vulkan REQUIRED)

#  GLFW: prefer a system / pkg-config copy on Linux + macOS (faster, smaller
#  configure step); fetch + build from source on Windows where there is no
#  canonical system install path.
if(WIN32)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glfw)
else()
    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
        add_library(glfw ALIAS PkgConfig::GLFW)
    endif()
endif()

#  Repo layout:
#    <repo>/
#      example/CMakeLists.txt          (this file)
#      layers/{core,expansion,execution,vmm}/*.{h,cpp}
#      vendor/{vma,glfw,vulkan_headers}/...
#      tests/*.cpp
set(VCK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------------------------------------------------------------------
#  vck static library (lib-once)
# ---------------------------------------------------------------------------
add_library(vck STATIC
    ${VCK_ROOT}/layers/core/VmaImpl.cpp
    ${VCK_ROOT}/layers/core/VulkanBuffer.cpp
    ${VCK_ROOT}/layers/core/VulkanCommand.cpp
    ${VCK_ROOT}/layers/core/VulkanContext.cpp
    ${VCK_ROOT}/layers/core/VulkanDevice.cpp
    ${VCK_ROOT}/layers/core/VulkanImage.cpp
    ${VCK_ROOT}/layers/core/VulkanPipeline.cpp
    ${VCK_ROOT}/layers/core/VulkanSwapchain.cpp
    ${VCK_ROOT}/layers/core/VulkanSync.cpp
    ${VCK_ROOT}/layers/core/VCKCrossplatform.cpp
    ${VCK_ROOT}/layers/expansion/VCKExpansion.cpp
    ${VCK_ROOT}/layers/execution/VCKExecution.cpp
    ${VCK_ROOT}/layers/vmm/VulkanMemoryManager.cpp
)

#  VCK_VULKAN_HEADERS_INCLUDE is listed BEFORE Vulkan::Vulkan's include path
#  so the canonical 1.4.321.0 vulkan_core.h (with its vk_video/* references)
#  resolves against the FetchContent copy.  vulkan_headers/include also has
#  the matching vk_video/ subtree that vulkan_core.h pulls in.
target_include_directories(vck PUBLIC
    ${VCK_VULKAN_HEADERS_INCLUDE}
    ${VCK_ROOT}
    ${VCK_ROOT}/layers/core
    ${VCK_ROOT}/layers/expansion
    ${VCK_ROOT}/layers/execution
    ${VCK_ROOT}/layers/vmm
    ${VCK_ROOT}/vendor/vma
)

target_compile_definitions(vck PUBLIC GLFW_INCLUDE_VULKAN)
target_link_libraries(vck PUBLIC Vulkan::Vulkan glfw)

# ---------------------------------------------------------------------------
#  Shader compilation helper
# ---------------------------------------------------------------------------
#  glslangValidator ships with the Vulkan SDK; find it.  Fall back to the
#  one inside VULKAN_SDK\Bin which find_program does not always pick up
#  on Windows.
find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin
)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found.  Install the Vulkan SDK and ensure it is on PATH.")
endif()

# compile_shaders(target_name dir stem)
#   Adds custom commands that compile  <dir>/assets/<stem>.{vert,frag} to
#   <dir>/assets/<stem>.{vert,frag}.spv and attaches them as a dependency of
#   the named target.  No-op when stem is empty (DebugShowcaseExample has
#   no GLSL).
function(compile_shaders target_name example_dir shader_stem)
    if("${shader_stem}" STREQUAL "")
        return()
    endif()
    set(assets ${CMAKE_CURRENT_SOURCE_DIR}/${example_dir}/assets)
    set(spvs)
    foreach(stage IN ITEMS vert frag)
        set(src ${assets}/${shader_stem}.${stage})
        set(spv ${assets}/${shader_stem}.${stage}.spv)
        if(EXISTS ${src})
            add_custom_command(
                OUTPUT  ${spv}
                COMMAND ${GLSLANG_VALIDATOR} -V ${src} -o ${spv}
                DEPENDS ${src}
                COMMENT "glslang ${example_dir}/${shader_stem}.${stage} -> .spv"
                VERBATIM
            )
            list(APPEND spvs ${spv})
        endif()
    endforeach()
    if(spvs)
        add_custom_target(${target_name}_shaders DEPENDS ${spvs})
        add_dependencies(${target_name} ${target_name}_shaders)
    endif()
endfunction()

# ---------------------------------------------------------------------------
#  Example targets
# ---------------------------------------------------------------------------
#  Each example builds main.cpp + App.cpp into <example>/<Example>(.exe).
#  RUNTIME_OUTPUT_DIRECTORY puts the binary next to its assets/ folder so
#  the existing  ./assets/<stem>.<stage>.spv  load paths still resolve when
#  you cd into the example dir and run ./<Example>.
#
#  add_example(target dir stem)
#    target = CMake target name + final exe basename (PascalCase example name)
#    dir    = subdirectory under example/
#    stem   = shader filename stem in <dir>/assets (or "" when no shaders)
function(add_example target dir stem)
    add_executable(${target}
        ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/App.cpp
    )
    target_link_libraries(${target} PRIVATE vck)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir}
    )
    # Quote stem so CMake passes the empty string through (DebugShowcaseExample
    # has no GLSL); without quoting an unset/empty arg gets dropped and the
    # function call fails with "incorrect arguments".
    compile_shaders("${target}" "${dir}" "${stem}")
endfunction()

#                  target                       dir                          stem
add_example(RGBTriangle                RGBTriangle                  triangle)
add_example(MipmapExample              MipmapExample                mip)
add_example(VMMExample                 VMMExample                   vmm)
add_example(SecondaryCmdExample        SecondaryCmdExample          secondary)
add_example(DebugTimelineExample       DebugTimelineExample         DebugTimelineExample)
add_example(DebugShowcaseExample       DebugShowcaseExample         "")
add_example(AAShowcaseExample          AAShowcaseExample            aa)
add_example(JobGraphExample            JobGraphExample              JobGraphExample)
add_example(SubmissionBatchingExample  SubmissionBatchingExample    SubmissionBatchingExample)
add_example(TimelineExample            TimelineExample              TimelineExample)
add_example(SchedulerPolicyExample     SchedulerPolicyExample       SchedulerPolicyExample)
add_example(HelloExample               HelloExample                 hello)
add_example(EasyCubeExample            EasyCubeExample              easycube)

# ---------------------------------------------------------------------------
#  R14 unit-test harness  (tests/*.cpp - links against vck)
# ---------------------------------------------------------------------------
file(GLOB VCK_TEST_SOURCES CONFIGURE_DEPENDS ${VCK_ROOT}/tests/test_*.cpp)
add_executable(vck_tests ${VCK_TEST_SOURCES})
target_include_directories(vck_tests PRIVATE ${VCK_ROOT}/tests)
target_link_libraries(vck_tests PRIVATE vck)
set_target_properties(vck_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VCK_ROOT}/tests
)

# CTest integration so `ctest --test-dir build -j` runs the harness too.
enable_testing()
add_test(NAME R14 COMMAND vck_tests)

# ---------------------------------------------------------------------------
#  Convenience meta-targets
# ---------------------------------------------------------------------------
add_custom_target(examples
    DEPENDS
        RGBTriangle
        MipmapExample
        VMMExample
        SecondaryCmdExample
        DebugTimelineExample
        DebugShowcaseExample
        AAShowcaseExample
        JobGraphExample
        SubmissionBatchingExample
        TimelineExample
        SchedulerPolicyExample
        HelloExample
        EasyCubeExample
    COMMENT "Build all 13 VCK examples"
)
