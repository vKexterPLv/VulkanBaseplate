#!/usr/bin/env bash
# =============================================================================
#  VCK example builder (Linux + macOS)
# =============================================================================
#  Mirror of build.bat for POSIX targets.  Same [1]-[13] / [A] / [T] / [0] menu.
#
#  PR #7: lib-once compile model
#  -----------------------------
#  Stage 1: build the VCK static library once into  build/libvck.a
#           (all 12 VKB sources + VulkanMemoryManager.cpp).
#  Stage 2: per example, compile only main.cpp + App.cpp (2 TUs) and link
#           against build/libvck.a.  Cuts ~143 redundant TUs out of build-all
#           (13 examples * 11 redundant VKB compiles).
#  Stage 3: optional [T] target builds tests/test_main.cpp + tests/*.cpp and
#           links against build/libvck.a (R14 unit-test harness, see tests/).
#
#  Requirements:
#    - glslangValidator on PATH              (ships with the Vulkan SDK)
#    - g++ or clang++ on PATH                (C++17)
#    - ar on PATH                            (archive helper, ships with binutils)
#    - Vulkan loader + dev headers           (libvulkan, vulkan/vulkan.h)
#    - GLFW 3.3+ with dev headers            (libglfw, GLFW/glfw3.h)
#    - pkg-config on PATH                    (used to discover the above)
#
#  Linux (Debian/Ubuntu):
#    sudo apt install libvulkan-dev libglfw3-dev vulkan-tools glslang-tools \
#                     pkg-config g++
#
#  macOS (Homebrew):
#    brew install vulkan-headers vulkan-loader glfw glslang molten-vk pkg-config
#    # MoltenVK provides Vulkan-over-Metal.  You may need to point the loader
#    # at it before running the compiled examples:
#    #   export VK_ICD_FILENAMES="$(brew --prefix molten-vk)/share/vulkan/icd.d/MoltenVK_icd.json"
#
#  Dependency layout (same as Windows where applicable):
#    example/
#      build/                                 (gitignored, compiler scratch)
#      deps/
#        vk_mem_alloc.h                       (single-header VMA)
# =============================================================================

set -uo pipefail

# ── ANSI colours -------------------------------------------------------------
C_RESET=$'\e[0m'; C_BOLD=$'\e[1m'; C_DIM=$'\e[2m'
C_RED=$'\e[91m'; C_GRN=$'\e[92m'; C_YEL=$'\e[93m'
C_BLU=$'\e[94m'; C_MAG=$'\e[95m'; C_CYN=$'\e[96m'; C_WHT=$'\e[97m'

err() { echo; echo "${C_RED}  ERROR:${C_RESET} $*"; echo; }

banner() {
    echo
    echo " ${C_CYN}+---------------------------------------------------------+${C_RESET}"
    echo " ${C_CYN}|${C_RESET}   ${C_BOLD}${C_WHT}V C K${C_RESET}   ${C_DIM}Vulkan Core Kit  -  example builder${C_RESET}    ${C_CYN}|${C_RESET}"
    echo " ${C_CYN}+---------------------------------------------------------+${C_RESET}"
    echo
}

step() { echo; echo "${C_MAG}>${C_RESET} ${C_BOLD}$*${C_RESET}"; }

# ── Platform detection -------------------------------------------------------
uname_s="$(uname -s)"
case "$uname_s" in
    Linux*)  VCK_OS=linux;   VCK_MACRO=-DVCK_PLATFORM_LINUX=1 ;;
    Darwin*) VCK_OS=macos;   VCK_MACRO=-DVCK_PLATFORM_MACOS=1 ;;
    *)       banner; err "unsupported OS '$uname_s' (only Linux + macOS)"; exit 1 ;;
esac

# ── Precondition checks ------------------------------------------------------
need_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        banner; err "'$1' not on PATH.  $2"; exit 1
    fi
}

need_tool glslangValidator "Install the Vulkan SDK / glslang-tools."
need_tool pkg-config        "Install pkg-config."
need_tool ar                "Install binutils (ar is needed to build libvck.a)."

CXX="${CXX:-}"
if [ -z "$CXX" ]; then
    if   command -v g++     >/dev/null 2>&1; then CXX=g++
    elif command -v clang++ >/dev/null 2>&1; then CXX=clang++
    else banner; err "neither g++ nor clang++ on PATH."; exit 1; fi
fi

if [ ! -f "deps/vk_mem_alloc.h" ]; then
    banner; err "deps/vk_mem_alloc.h is missing."; exit 1
fi

# Probe Vulkan + GLFW via pkg-config.
if ! pkg-config --exists vulkan glfw3; then
    banner
    err "pkg-config can't find 'vulkan' and/or 'glfw3'."
    if [ "$VCK_OS" = "linux" ]; then
        echo "       sudo apt install libvulkan-dev libglfw3-dev pkg-config"
    else
        echo "       brew install vulkan-headers vulkan-loader glfw pkg-config"
    fi
    exit 1
fi

PKG_CFLAGS="$(pkg-config --cflags vulkan glfw3)"
PKG_LIBS="$(pkg-config --libs   vulkan glfw3)"

# ── Shared flags -------------------------------------------------------------
# -DGLFW_INCLUDE_VULKAN mirrors the Windows build: any translation unit that
# includes <GLFW/glfw3.h> before "VCK.h" picks up glfwCreateWindowSurface.
DEFINES="-DGLFW_INCLUDE_VULKAN $VCK_MACRO"

# macOS: Cocoa/IOKit/QuartzCore are required by GLFW; MoltenVK ships as dylib
# so we just rely on pkg-config's -lvulkan.
INCLUDES="-I../vendor/vma -I../vendor/glfw/include -Ideps -I.. -I../layers/core -I../layers/expansion -I../layers/execution -I../layers/vmm -I../vendor/vulkan_headers $PKG_CFLAGS $DEFINES"
LIBS="$PKG_LIBS -lpthread -ldl"

if [ "$VCK_OS" = "macos" ]; then
    LIBS="$PKG_LIBS -framework Cocoa -framework IOKit -framework QuartzCore"
fi

# Compile flags shared by lib + per-example builds.  -w matches build.bat's
# silent-on-warnings behaviour (VMA / GLFW / vulkan_core.h are noisy on POSIX
# toolchains and the warnings are not actionable for end users).  Real bugs
# surface as errors via -Werror=return-type regardless.
CXXFLAGS="-std=c++17 -O2 -w -Werror=return-type"

# ── VKB sources (compiled once into libvck.a) ───────────────────────────────
# Order does not matter: archived into a static library, linker pulls by-need.
# VulkanMemoryManager.cpp moves from per-example optional to lib-included so
# the archive is monolithic and examples need not split on "uses VMM" anymore.
VKB_SRCS=(
    "../layers/core/VmaImpl.cpp"
    "../layers/core/VulkanBuffer.cpp"
    "../layers/core/VulkanCommand.cpp"
    "../layers/core/VulkanContext.cpp"
    "../layers/core/VulkanDevice.cpp"
    "../layers/core/VulkanImage.cpp"
    "../layers/core/VulkanPipeline.cpp"
    "../layers/core/VulkanSwapchain.cpp"
    "../layers/core/VulkanSync.cpp"
    "../layers/core/VCKCrossplatform.cpp"
    "../layers/expansion/VCKExpansion.cpp"
    "../layers/execution/VCKExecution.cpp"
    "../layers/vmm/VulkanMemoryManager.cpp"
)

BUILD_DIR="build"
LIB="$BUILD_DIR/libvck.a"

# ── Build primitives ---------------------------------------------------------

# Hash the lib's input list to know when to rebuild.  Tracks the compiler, the
# include / define string, the source file list, and each source's mtime.
# Stored in build/libvck.stamp.  When the stamp matches, the lib is up-to-date
# and we skip the entire stage-1 compile.  Cuts repeat invocations of
# `./build.sh A` from "compile 13 sources twice" to "link 26 small TUs".
lib_stamp_of() {
    {
        echo "$CXX"
        echo "$CXXFLAGS"
        echo "$INCLUDES"
        for s in "${VKB_SRCS[@]}"; do
            printf '%s ' "$s"
            stat -c '%Y' "$s" 2>/dev/null || stat -f '%m' "$s" 2>/dev/null || echo 0
        done
    } | sha1sum 2>/dev/null | awk '{print $1}' || \
    {
        echo "$CXX"
        echo "$CXXFLAGS"
        echo "$INCLUDES"
        for s in "${VKB_SRCS[@]}"; do
            printf '%s ' "$s"
            stat -f '%m' "$s" 2>/dev/null || echo 0
        done
    } | shasum | awk '{print $1}'
}

build_lib() {
    mkdir -p "$BUILD_DIR"
    local want="$(lib_stamp_of)"
    local have=""
    [ -f "$BUILD_DIR/libvck.stamp" ] && have="$(cat "$BUILD_DIR/libvck.stamp")"
    if [ -f "$LIB" ] && [ "$want" = "$have" ]; then
        echo "  ${C_DIM}libvck.a   up-to-date${C_RESET}"
        return 0
    fi
    step "[lib] $LIB ($(basename "$CXX"))"
    rm -f "$LIB"
    local objs=()
    local src
    local jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
    # Fan out compilation across cores.  Each TU is independent; -c writes a
    # .o into build/.
    local pids=() statuses=() i=0
    for src in "${VKB_SRCS[@]}"; do
        local stem; stem="$(basename "$src" .cpp)"
        local obj="$BUILD_DIR/$stem.o"
        objs+=("$obj")
        echo "  ${C_DIM}$CXX -c $stem${C_RESET}"
        (
            # shellcheck disable=SC2086
            $CXX -c "$src" -o "$obj" $CXXFLAGS $INCLUDES
        ) &
        pids+=("$!")
        # Cap concurrency at $jobs.
        if [ "${#pids[@]}" -ge "$jobs" ]; then
            wait "${pids[0]}" || { err "C++ compile failed in libvck.a stage"; return 1; }
            pids=("${pids[@]:1}")
        fi
    done
    local pid
    for pid in "${pids[@]}"; do
        wait "$pid" || { err "C++ compile failed in libvck.a stage"; return 1; }
    done
    echo "  ${C_DIM}ar  rcs    libvck.a${C_RESET}"
    ar rcs "$LIB" "${objs[@]}" || { err "ar failed for libvck.a"; return 1; }
    echo "$want" > "$BUILD_DIR/libvck.stamp"
    echo "  ${C_GRN}  OK${C_RESET}   $LIB"
}

compile_shaders() {
    # $1 = example dir      $2 = shader stem
    local ex="$1" stem="$2"
    mkdir -p "$ex/assets"
    echo "  ${C_DIM}glslang $stem.vert${C_RESET}"
    glslangValidator -V "$ex/assets/$stem.vert" -o "$ex/assets/$stem.vert.spv" >/dev/null || {
        err "shader compile failed: $stem.vert"; return 1; }
    echo "  ${C_DIM}glslang $stem.frag${C_RESET}"
    glslangValidator -V "$ex/assets/$stem.frag" -o "$ex/assets/$stem.frag.spv" >/dev/null || {
        err "shader compile failed: $stem.frag"; return 1; }
}

compile_cpp() {
    # $1 = example dir.  Compiles only main.cpp + App.cpp and links against
    # the prebuilt libvck.a.  No more conditional VMM compile - VMM is in
    # the library.
    local ex="$1"
    local out="$ex/$ex"
    echo "  ${C_DIM}$CXX     $ex (main+App, link libvck.a)${C_RESET}"
    # shellcheck disable=SC2086
    $CXX "$ex/main.cpp" "$ex/App.cpp" \
         -o "$out" $CXXFLAGS $INCLUDES "$LIB" $LIBS || {
        err "C++ compile failed: $ex"; return 1; }
    echo "  ${C_GRN}  OK${C_RESET}   $out"
}

ok_run() {
    local ex="$1"
    echo
    echo "  ${C_CYN}run:${C_RESET}   cd $ex  &&  ./$ex"
    echo
}

# Per-example configuration (name, shader stem).
build_one() {
    local id="$1" ex stem=""
    case "$id" in
        1)  ex=RGBTriangle                stem=triangle                 ;;
        2)  ex=MipmapExample              stem=mip                      ;;
        3)  ex=VMMExample                 stem=vmm                      ;;
        4)  ex=SecondaryCmdExample        stem=secondary                ;;
        5)  ex=DebugTimelineExample       stem=DebugTimelineExample     ;;
        6)  ex=DebugShowcaseExample       stem=                         ;;
        7)  ex=AAShowcaseExample          stem=aa                       ;;
        8)  ex=JobGraphExample            stem=JobGraphExample          ;;
        9)  ex=SubmissionBatchingExample  stem=SubmissionBatchingExample;;
        10) ex=TimelineExample            stem=TimelineExample          ;;
        11) ex=SchedulerPolicyExample     stem=SchedulerPolicyExample   ;;
        12) ex=HelloExample               stem=hello                    ;;
        13) ex=EasyCubeExample            stem=easycube                 ;;
        *) err "unknown example id '$id'"; return 1 ;;
    esac

    if [ -n "$stem" ]; then
        compile_shaders "$ex" "$stem" || return 1
    fi
    compile_cpp     "$ex" || return 1
}

build_all() {
    local i names=(
        "RGBTriangle" "MipmapExample" "VMMExample" "SecondaryCmdExample"
        "DebugTimelineExample" "DebugShowcaseExample" "AAShowcaseExample"
        "JobGraphExample" "SubmissionBatchingExample" "TimelineExample"
        "SchedulerPolicyExample" "HelloExample" "EasyCubeExample"
    )
    build_lib || return 1
    for i in 1 2 3 4 5 6 7 8 9 10 11 12 13; do
        step "[${i}/13] ${names[$((i-1))]}"
        build_one "$i" || return 1
    done
    echo
    echo "${C_GRN}  all 13 examples built.${C_RESET}"
    echo
}

# Build the R14 unit-test harness.  Sources live under tests/ and link against
# the prebuilt libvck.a so test-mode compile is the same lib + 1-2 small TUs.
build_tests() {
    if [ ! -d "../tests" ]; then
        err "../tests/ directory not found - R14 harness not in this checkout."
        return 1
    fi
    build_lib || return 1
    step "[T] R14 unit tests"
    local out="../tests/vck_tests"
    local srcs
    srcs=( ../tests/*.cpp )
    if [ "${#srcs[@]}" -eq 0 ]; then
        err "no .cpp files under ../tests/"; return 1
    fi
    echo "  ${C_DIM}$CXX     vck_tests (${#srcs[@]} TUs, link libvck.a)${C_RESET}"
    # shellcheck disable=SC2086
    $CXX "${srcs[@]}" -o "$out" $CXXFLAGS $INCLUDES -I../tests "$LIB" $LIBS || {
        err "test compile failed"; return 1; }
    echo "  ${C_GRN}  OK${C_RESET}   $out"
    echo
    echo "  ${C_CYN}run:${C_RESET}   $out"
    echo
}

# ── Menu --------------------------------------------------------------------
banner
echo " ${C_BOLD}${C_WHT}Raw core${C_RESET}                ${C_DIM}(VulkanSync / VulkanCommand path, you write everything)${C_RESET}"
echo "   ${C_YEL}[1]${C_RESET}  ${C_WHT}RGBTriangle${C_RESET}                 coloured triangle, live resize"
echo "   ${C_YEL}[2]${C_RESET}  ${C_WHT}MipmapExample${C_RESET}               mip chain generation and sampling"
echo "   ${C_YEL}[3]${C_RESET}  ${C_WHT}VMMExample${C_RESET}                  VMM all three layers"
echo "   ${C_YEL}[4]${C_RESET}  ${C_WHT}SecondaryCmdExample${C_RESET}         secondary command buffers + scheduler-aware resize (v0.3)"
echo
echo " ${C_BOLD}${C_WHT}Debug + tooling${C_RESET}         ${C_DIM}(opt-in instrumentation, still hand-records)${C_RESET}"
echo "   ${C_YEL}[5]${C_RESET}  ${C_WHT}DebugTimelineExample${C_RESET}        span recorder + Dump every 120 frames"
echo "   ${C_YEL}[6]${C_RESET}  ${C_WHT}DebugShowcaseExample${C_RESET}        VCKLog levels / dedup / VK_CHECK / debug toggle"
echo
echo " ${C_BOLD}${C_WHT}Expansion${C_RESET}               ${C_DIM}(AA / framebuffer / sampler wiring)${C_RESET}"
echo "   ${C_YEL}[7]${C_RESET}  ${C_WHT}AAShowcaseExample${C_RESET}           AATechnique decision matrix + auto-pick + triangle"
echo
echo " ${C_BOLD}${C_WHT}Execution layer${C_RESET}         ${C_DIM}(JobGraph / batching / timeline / policy)${C_RESET}"
echo "   ${C_YEL}[8]${C_RESET}  ${C_WHT}JobGraphExample${C_RESET}             CPU task graph with dependencies"
echo "   ${C_YEL}[9]${C_RESET}  ${C_WHT}SubmissionBatchingExample${C_RESET}   2 cmd buffers, 1 vkQueueSubmit"
echo "   ${C_YEL}[10]${C_RESET} ${C_WHT}TimelineExample${C_RESET}             TimelineSemaphore + DependencyToken"
echo "   ${C_YEL}[11]${C_RESET} ${C_WHT}SchedulerPolicyExample${C_RESET}      Lockstep / Pipelined / AsyncMax at runtime"
echo
echo " ${C_BOLD}${C_WHT}Mostly VCK${C_RESET}              ${C_DIM}(ergonomic API does the work)${C_RESET}"
echo "   ${C_YEL}[12]${C_RESET} ${C_WHT}HelloExample${C_RESET}                minimal FrameScheduler + triangle"
echo "   ${C_YEL}[13]${C_RESET} ${C_WHT}EasyCubeExample${C_RESET}             Primitives::Cube + VertexLayout + PushConstants + VCKMath"
echo
echo "   ${C_CYN}[A]${C_RESET}  ${C_WHT}Build all${C_RESET}                   in order, stops on first failure"
echo "   ${C_CYN}[T]${C_RESET}  ${C_WHT}R14 unit tests${C_RESET}              build + show run command (PR #7)"
echo "   ${C_CYN}[0]${C_RESET}  ${C_WHT}Exit${C_RESET}"
echo

CHOICE="${1:-}"
if [ -z "$CHOICE" ]; then
    printf "   ${C_BOLD}${C_CYN}select> ${C_RESET}"
    read -r CHOICE
fi

case "$(echo "$CHOICE" | tr '[:upper:]' '[:lower:]')" in
    0)  exit 0 ;;
    a)  build_all || exit 1; exit 0 ;;
    t)  build_tests || exit 1; exit 0 ;;
    1|2|3|4|5|6|7|8|9|10|11|12|13)
        build_lib || exit 1
        build_one "$CHOICE" || exit 1
        # name lookup again for the run hint
        case "$CHOICE" in
            1)  ok_run RGBTriangle                ;;
            2)  ok_run MipmapExample              ;;
            3)  ok_run VMMExample                 ;;
            4)  ok_run SecondaryCmdExample        ;;
            5)  ok_run DebugTimelineExample       ;;
            6)  ok_run DebugShowcaseExample       ;;
            7)  ok_run AAShowcaseExample          ;;
            8)  ok_run JobGraphExample            ;;
            9)  ok_run SubmissionBatchingExample  ;;
            10) ok_run TimelineExample            ;;
            11) ok_run SchedulerPolicyExample     ;;
            12) ok_run HelloExample               ;;
            13) ok_run EasyCubeExample            ;;
        esac
        ;;
    *)  err "unknown selection '$CHOICE'"; exit 1 ;;
esac
