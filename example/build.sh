#!/usr/bin/env bash
# =============================================================================
#  VCK example builder (Linux / macOS)
# =============================================================================
#  Run from the repo's example/ folder.  This script is a thin wrapper around
#  CMake + Ninja - the heavy lifting (lib-once compile model, GLFW + Vulkan
#  headers, GLSL -> SPIR-V) lives in example/CMakeLists.txt.
#
#    1. Configure once     -> cmake -S . -B build -G Ninja
#    2. Build by menu pick -> cmake --build build -j --target <Name>
#
#  Toolchain: ninja picks the C++ compiler from CXX env var or PATH.
#  Set CXX=clang++ to force clang on Linux; macOS defaults to clang++.
#
#  Requirements:
#    - cmake 3.20+ + ninja
#    - Vulkan loader + headers (apt: libvulkan-dev, brew: vulkan-loader)
#    - glfw3 (apt: libglfw3-dev, brew: glfw)
#    - glslangValidator (apt: glslang-tools, brew: glslang)
#    - pkg-config
#
#  Menu / non-interactive forms:
#    ./build.sh            - interactive menu
#    ./build.sh A          - build all 13 examples
#    ./build.sh T          - build + run the R14 unit-test harness
#    ./build.sh 1..13      - build a single example by number
#    ./build.sh 0          - exit
# =============================================================================

set -e

C_RESET='\033[0m'
C_BOLD='\033[1m'
C_DIM='\033[2m'
C_RED='\033[91m'
C_YEL='\033[93m'
C_CYN='\033[96m'
C_WHT='\033[97m'

err() { printf "${C_RED}ERROR:${C_RESET} %s\n" "$1" >&2; exit 1; }
banner() {
    printf "\n${C_BOLD}${C_CYN}VCK${C_RESET} ${C_DIM}- example builder ($(uname -s) / cmake + ninja)${C_RESET}\n\n"
}

command -v cmake >/dev/null 2>&1 || err "cmake not found on PATH."
command -v ninja >/dev/null 2>&1 || err "ninja not found on PATH (apt: ninja-build, brew: ninja)."
command -v glslangValidator >/dev/null 2>&1 || err "glslangValidator not found (apt: glslang-tools, brew: glslang)."

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${HERE}/build"
TESTS_BIN="${HERE}/../tests/vck_tests"

configure_once() {
    if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        printf "${C_DIM}[cmake] configuring (one-time)...${C_RESET}\n"
        cmake -S "${HERE}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
    fi
}

CHOICE="${1:-}"
if [ -z "${CHOICE}" ]; then
    banner
    printf " ${C_BOLD}${C_WHT}Raw core${C_RESET}                ${C_DIM}(pure Vulkan, hand-recorded commands)${C_RESET}\n"
    printf "   ${C_YEL}[1]${C_RESET}  ${C_WHT}RGBTriangle${C_RESET}                 hello triangle, manual everything\n"
    printf "   ${C_YEL}[2]${C_RESET}  ${C_WHT}MipmapExample${C_RESET}               mip chain generation and sampling\n\n"
    printf " ${C_BOLD}${C_WHT}Plus VMM${C_RESET}                ${C_DIM}(persistent / transient / frame-buffered)${C_RESET}\n"
    printf "   ${C_YEL}[3]${C_RESET}  ${C_WHT}VMMExample${C_RESET}                  VMM all three layers\n"
    printf "   ${C_YEL}[4]${C_RESET}  ${C_WHT}SecondaryCmdExample${C_RESET}         secondary command buffers + scheduler-aware resize (v0.3)\n\n"
    printf " ${C_BOLD}${C_WHT}Plus debug instrumentation${C_RESET}\n"
    printf "   ${C_YEL}[5]${C_RESET}  ${C_WHT}DebugTimelineExample${C_RESET}        span recorder + Dump every 120 frames\n"
    printf "   ${C_YEL}[6]${C_RESET}  ${C_WHT}DebugShowcaseExample${C_RESET}        VCKLog levels / dedup / VK_CHECK / debug toggle\n\n"
    printf " ${C_BOLD}${C_WHT}Plus AA${C_RESET}                  ${C_DIM}(MSAA / FXAA expansion)${C_RESET}\n"
    printf "   ${C_YEL}[7]${C_RESET}  ${C_WHT}AAShowcaseExample${C_RESET}           MSAA + FXAA + post pipeline\n\n"
    printf " ${C_BOLD}${C_WHT}Execution layer${C_RESET}         ${C_DIM}(JobGraph / batching / timeline / policy)${C_RESET}\n"
    printf "   ${C_YEL}[8]${C_RESET}  ${C_WHT}JobGraphExample${C_RESET}             CPU task graph with dependencies\n"
    printf "   ${C_YEL}[9]${C_RESET}  ${C_WHT}SubmissionBatchingExample${C_RESET}   2 cmd buffers, 1 vkQueueSubmit\n"
    printf "   ${C_YEL}[10]${C_RESET} ${C_WHT}TimelineExample${C_RESET}             DependencyToken / GPU wait chains\n"
    printf "   ${C_YEL}[11]${C_RESET} ${C_WHT}SchedulerPolicyExample${C_RESET}      Pipelined / AsyncMax policies\n\n"
    printf " ${C_BOLD}${C_WHT}Mostly VCK${C_RESET}              ${C_DIM}(ergonomic API does the work)${C_RESET}\n"
    printf "   ${C_YEL}[12]${C_RESET} ${C_WHT}HelloExample${C_RESET}                minimal FrameScheduler + triangle\n"
    printf "   ${C_YEL}[13]${C_RESET} ${C_WHT}EasyCubeExample${C_RESET}             Primitives::Cube + VertexLayout + PushConstants + VCKMath\n\n"
    printf "   ${C_YEL}[A]${C_RESET}  ${C_BOLD}Build all${C_RESET}\n"
    printf "   ${C_YEL}[T]${C_RESET}  ${C_BOLD}R14 unit-test harness (build + run)${C_RESET}\n"
    printf "   ${C_YEL}[0]${C_RESET}  ${C_DIM}Exit${C_RESET}\n\n"
    read -rp "> " CHOICE
fi

case "${CHOICE}" in
    0)        exit 0 ;;
    A|a)      configure_once; cmake --build "${BUILD_DIR}" -j --target examples ;;
    T|t)      configure_once
              cmake --build "${BUILD_DIR}" -j --target vck_tests
              "${TESTS_BIN}" ;;
    1)        configure_once; cmake --build "${BUILD_DIR}" -j --target RGBTriangle ;;
    2)        configure_once; cmake --build "${BUILD_DIR}" -j --target MipmapExample ;;
    3)        configure_once; cmake --build "${BUILD_DIR}" -j --target VMMExample ;;
    4)        configure_once; cmake --build "${BUILD_DIR}" -j --target SecondaryCmdExample ;;
    5)        configure_once; cmake --build "${BUILD_DIR}" -j --target DebugTimelineExample ;;
    6)        configure_once; cmake --build "${BUILD_DIR}" -j --target DebugShowcaseExample ;;
    7)        configure_once; cmake --build "${BUILD_DIR}" -j --target AAShowcaseExample ;;
    8)        configure_once; cmake --build "${BUILD_DIR}" -j --target JobGraphExample ;;
    9)        configure_once; cmake --build "${BUILD_DIR}" -j --target SubmissionBatchingExample ;;
    10)       configure_once; cmake --build "${BUILD_DIR}" -j --target TimelineExample ;;
    11)       configure_once; cmake --build "${BUILD_DIR}" -j --target SchedulerPolicyExample ;;
    12)       configure_once; cmake --build "${BUILD_DIR}" -j --target HelloExample ;;
    13)       configure_once; cmake --build "${BUILD_DIR}" -j --target EasyCubeExample ;;
    *)        err "unknown selection '${CHOICE}'.  Pick 1-13, A, T, or 0." ;;
esac
