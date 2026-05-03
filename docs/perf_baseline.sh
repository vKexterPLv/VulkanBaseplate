#!/usr/bin/env bash
# docs/perf_baseline.sh
#
# Measures the v0.5 anchors documented in docs/Perf.md and prints
# a markdown block ready to paste into the "Latest measurement"
# section.  Run from the repo root.  Exits 0 on success.
#
# What it measures unconditionally (no GPU required):
#   S1..S6   static surface metrics
#   A1..A2   peak RSS for the R14 ctest harness (vck_tests)
#
# What it tries to measure when a Vulkan-capable display is present:
#   A3       frame time (RGBTriangle, 1000 frames)
#   A4       VMM staging throughput (VMMExample)
#   A5       hot-reload latency (ShaderToolingExample)
#
# When no Vulkan-capable display is detected (e.g. headless CI), A3..A5
# are reported as "GPU unavailable" and the script still exits 0 - the
# static + RSS anchors are still useful as a regression signal.

set -uo pipefail
IFS=$'\n\t'

# Force a deterministic locale for any sub-tool that emits localized
# numbers, headings, or separators (notably GNU time and awk parsing
# of its output).  Anything we capture and parse must be in the C locale.
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT" || { echo "FATAL: cannot cd to repo root '$REPO_ROOT'" >&2; exit 1; }

BUILD_DIR="${BUILD_DIR:-build-perf}"
OS="$(uname -s)"

# All tmp files live here; cleaned on any exit, including signal.
TMPDIR_HARNESS="$(mktemp -d -t vck-perf.XXXXXX)"
trap 'rm -rf "$TMPDIR_HARNESS"' EXIT INT TERM

# ---------------------------------------------------------------------------
#  Helpers
# ---------------------------------------------------------------------------

# first_line CMD ARGS...  ->  prints the first line of command output, or
# nothing if the command failed or produced no output.  Avoids the
# `pipefail + head -1 + ||` trap where SIGPIPE on the producer triggers
# both the real output AND a fallback string.  We collect everything
# first, then take the first line in pure shell.
first_line() {
    local out
    out=$("$@" 2>/dev/null) || return 1
    [[ -z "$out" ]] && return 1
    printf '%s\n' "${out%%$'\n'*}"
}

# Read peak RSS (in bytes) from a command, portable Linux/macOS.
# Usage:  peak_rss CMD ARGS...   ->   prints integer bytes on stdout,
#                                     or "unavailable" if no time(1)
#                                     binary we can parse is installed.
peak_rss() {
    # Pick a time(1) binary and the parser that matches it.
    #   /usr/bin/time on Linux is GNU time          -> -v, kbytes, capital M
    #   /usr/bin/time on macOS is BSD time          -> -l, bytes, lowercase m
    #   gtime on macOS (Homebrew coreutils) is GNU  -> use the GNU parser
    local time_bin="" parser=""
    if [[ "$OS" == "Darwin" ]]; then
        if [[ -x /usr/bin/time ]]; then
            time_bin="/usr/bin/time"; parser="bsd"
        elif command -v gtime >/dev/null 2>&1; then
            time_bin="$(command -v gtime)"; parser="gnu"
        fi
    else
        [[ -x /usr/bin/time ]] && { time_bin="/usr/bin/time"; parser="gnu"; }
    fi
    [[ -n "$time_bin" ]] || { echo unavailable; return; }

    local rss_log="$TMPDIR_HARNESS/rss.$$"
    case "$parser" in
        bsd)
            "$time_bin" -l "$@" 2> "$rss_log" >/dev/null
            local bytes
            bytes=$(awk '/maximum resident set size/ { print $1 }' "$rss_log")
            if [[ -z "$bytes" ]]; then echo unavailable; else echo "$bytes"; fi
            ;;
        gnu)
            "$time_bin" -v "$@" 2> "$rss_log" >/dev/null
            local kib
            kib=$(awk -F': ' '/Maximum resident set size/ { print $2 }' "$rss_log")
            if [[ -z "$kib" ]]; then echo unavailable; else echo $(( kib * 1024 )); fi
            ;;
    esac
}

human_bytes() {
    local b=${1:-0}
    awk -v b="$b" 'BEGIN {
        if (b > 1073741824) printf "%.1f GiB", b/1073741824;
        else if (b > 1048576) printf "%.1f MiB", b/1048576;
        else if (b > 1024)    printf "%.1f KiB", b/1024;
        else                  printf "%d B", b;
    }'
}

have_display() {
    # Linux: $DISPLAY or $WAYLAND_DISPLAY set.
    # macOS: assume yes (Metal via MoltenVK).
    if [[ "$OS" == "Darwin" ]]; then return 0; fi
    [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]
}

# ---------------------------------------------------------------------------
#  Build (Release, no sanitizers)
# ---------------------------------------------------------------------------

echo "==> configuring $BUILD_DIR (Release)..."
rm -rf "$BUILD_DIR"
cmake_log="$TMPDIR_HARNESS/cmake.log"
cmake -S example -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release > "$cmake_log" 2>&1 \
    || { echo "FATAL: cmake configure failed; log:" >&2; cat "$cmake_log" >&2; exit 1; }

build_start=$(date +%s)
echo "==> building all examples + vck_tests..."
build_log="$TMPDIR_HARNESS/build.log"
cmake --build "$BUILD_DIR" -j --target examples vck_tests > "$build_log" 2>&1 \
    || { echo "FATAL: build failed; log:" >&2; cat "$build_log" >&2; exit 1; }
build_end=$(date +%s)
build_seconds=$(( build_end - build_start ))

# ---------------------------------------------------------------------------
#  Static surface metrics
# ---------------------------------------------------------------------------

# S1 - count distinct numbered class index entries in VCK.h.  The header
#      lists the index twice (a short overview block and a longer detail
#      block); dedupe on the [N] tag so each class is counted once.
S1=$(grep -oE '^//[[:space:]]*\[[0-9]+\][[:space:]]+[A-Za-z][A-Za-z0-9_<>]*' VCK.h \
        | awk '{ print $2 }' | sort -u | wc -l | tr -d ' ')

# S2 - count cfg knobs: every field in 'struct Config' that has a default.
S2=$(awk '/^struct Config$/,/^};/' layers/core/VulkanHelpers.h \
        | grep -cE '^[[:space:]]+[a-zA-Z][a-zA-Z0-9_:<>* ]+[[:space:]]+[a-zA-Z][a-zA-Z0-9_]*[[:space:]]*=' \
        || true)

# S3 - count example directories (those that contain App.cpp).
S3=$(find example -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/App.cpp' \; -print | wc -l | tr -d ' ')

# S4 - VCK.h line count.
S4=$(wc -l < VCK.h | tr -d ' ')

# S5 - static-lib size on disk.  Look in the obvious place first, then
#      fall back to a bounded find().  Read the find result line by line
#      (no `head -1 | pipefail` race).
S5_path=""
if [[ -f "$BUILD_DIR/libvck.a" ]]; then
    S5_path="$BUILD_DIR/libvck.a"
else
    while IFS= read -r p; do
        if [[ -f "$p" ]]; then S5_path="$p"; break; fi
    done < <(find "$BUILD_DIR" -maxdepth 4 \( -name 'libvck*.a' -o -name 'vck.lib' \) 2>/dev/null)
fi
if [[ -n "$S5_path" && -f "$S5_path" ]]; then
    S5_bytes=$(stat -c%s -- "$S5_path" 2>/dev/null || stat -f%z -- "$S5_path" 2>/dev/null || echo 0)
    S5_human=$(human_bytes "$S5_bytes")
else
    S5_human="(not found)"
fi

# S6 - build wall time captured above.
S6_human="${build_seconds} s"

# ---------------------------------------------------------------------------
#  A1 / A2 peak RSS (R14 harness, no GPU required)
# ---------------------------------------------------------------------------
#
# vck_tests runs the R14 ctest harness which exercises every Initialize /
# Shutdown path the kit ships.  It is a faithful proxy for the per-class
# memory cost of bringing the kit up - the RGBTriangle figure is mostly
# the same allocations plus the swapchain and the framebuffer set.

A1_path="tests/vck_tests"
if [[ ! -x "$A1_path" ]]; then
    while IFS= read -r p; do
        if [[ -x "$p" ]]; then A1_path="$p"; break; fi
    done < <(find . -name 'vck_tests' -type f -executable 2>/dev/null)
fi

if [[ -n "$A1_path" && -x "$A1_path" ]]; then
    A1_raw=$(peak_rss "$A1_path")
    if [[ "$A1_raw" == "unavailable" ]]; then
        A1_human="(install GNU time: \`apt-get install time\` on Linux, \`brew install gnu-time\` on macOS)"
        A2_human="$A1_human"
    else
        A1_human=$(human_bytes "$A1_raw")
        A2_human="$A1_human (R14 harness one-shot, no steady-state run)"
    fi
else
    A1_human="(vck_tests binary not found)"
    A2_human="(vck_tests binary not found)"
fi

# ---------------------------------------------------------------------------
#  A3 / A4 / A5 (GPU required)
# ---------------------------------------------------------------------------

if have_display; then
    A3_human="(measure: ./RGBTriangle in immediate mode for 1000 frames; record median frame time. Not yet automated; populate manually.)"
    A4_human="(measure: ./VMMExample with synthetic 256 x 1 MiB textures; record MiB/sec. Not yet automated; populate manually.)"
    A5_human="(measure: touch fragment.spv inside ShaderToolingExample, record ms to next 'reload complete' Notice. Not yet automated; populate manually.)"
else
    A3_human="GPU unavailable (no \$DISPLAY or \$WAYLAND_DISPLAY)"
    A4_human="GPU unavailable"
    A5_human="GPU unavailable"
fi

# ---------------------------------------------------------------------------
#  Output
# ---------------------------------------------------------------------------

GIT_SHA=$(git rev-parse --short HEAD 2>/dev/null || true)
[[ -n "$GIT_SHA" ]] || GIT_SHA="unknown"
DATE_NOW=$(date -u +"%Y-%m-%d")
KERNEL=$(uname -sr)
COMPILER=$(first_line "${CXX:-g++}" --version || true)
[[ -n "$COMPILER" ]] || COMPILER="(unknown)"

cat <<EOF

----- COPY EVERYTHING BELOW THIS LINE INTO docs/Perf.md -----

> Last updated: **${DATE_NOW}**, commit \`${GIT_SHA}\`

| | Anchor | Value | Notes |
|---|---|---:|---|
| A1 | Peak RSS, init        | ${A1_human} | R14 harness (vck_tests), one-shot |
| A2 | Peak RSS, steady      | ${A2_human} |  |
| A3 | Frame time            | ${A3_human} |  |
| A4 | VMM staging throughput| ${A4_human} |  |
| A5 | Hot-reload latency    | ${A5_human} |  |

| | Static metric | v0.5 |
|---|---|---:|
| S1 | Public class count (VCK.h)        | ${S1} |
| S2 | \`cfg\` knob count                  | ${S2} |
| S3 | Examples count                    | ${S3} |
| S4 | \`VCK.h\` line count                 | ${S4} |
| S5 | \`vck.a\` (static lib) size, Release | ${S5_human} |
| S6 | Build wall time, all examples      | ${S6_human} |

**Test machine:**

- Kernel: \`${KERNEL}\`
- Compiler: \`${COMPILER}\`
- Build type: \`Release\` (\`-O3 -DNDEBUG\`)
- Vulkan SDK: (fill in)
- GPU: (fill in)
- RAM: (fill in)

----- COPY EVERYTHING ABOVE THIS LINE INTO docs/Perf.md -----
EOF
