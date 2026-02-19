#!/usr/bin/env bash
# =============================================================================
# perf_profile.sh  –  industry-standard profiling toolkit for slicer/
#
# Subcommands:
#   bench      <image> [iters]   raw timing benchmark (mean/min/max/stddev)
#   flamegraph <image> [iters]   perf record + Brendan Gregg FlameGraph SVG
#   perf-stat  <image> [iters]   hardware counter report (IPC, cache misses…)
#   gprof      <image> [iters]   GNU gprof flat profile + call graph
#   callgrind  <image> [iters]   valgrind callgrind + kcachegrind-ready output
#   massif     <image> [iters]   valgrind heap profiler
#   asan       <image> [iters]   AddressSanitizer + UBSan run
#   all        <image> [iters]   run bench + perf-stat + flamegraph + gprof
#
# Requirements (install what you need):
#   bench / flamegraph / perf-stat  : linux-perf  (xbps-install: linux-tools)
#   flamegraph                      : FlameGraph   (https://github.com/brendangregg/FlameGraph)
#                                     clone to ~/FlameGraph  OR  set FLAMEGRAPH_DIR
#   gprof                           : binutils     (usually pre-installed)
#   callgrind / massif              : valgrind     (xbps-install: valgrind)
#   asan                            : clang or gcc with sanitizer support
#
# Usage examples:
#   ./perf_profile.sh bench      sample/test1.png 200
#   ./perf_profile.sh flamegraph sample/test1.png 500
#   ./perf_profile.sh all        sample/test1.png
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
RESULTS_DIR="${SCRIPT_DIR}/perf_results"

# Path to Brendan Gregg's FlameGraph scripts.
# Override with:  FLAMEGRAPH_DIR=/your/path ./perf_profile.sh flamegraph ...
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-${HOME}/FlameGraph}"

# Default iteration counts per mode (can be overridden on the CLI)
DEFAULT_ITERS_BENCH=200
DEFAULT_ITERS_FLAMEGRAPH=2000
DEFAULT_ITERS_PERFSTAT=500
DEFAULT_ITERS_GPROF=500
DEFAULT_ITERS_CALLGRIND=20     # callgrind is ~50x slower – keep small
DEFAULT_ITERS_MASSIF=5
DEFAULT_ITERS_ASAN=50

# perf record frequency (samples per second)
PERF_FREQ=99

# ---------------------------------------------------------------------------
# Colours
# ---------------------------------------------------------------------------
RED=$'\033[0;31m'; YELLOW=$'\033[1;33m'; GREEN=$'\033[0;32m'
CYAN=$'\033[0;36m'; BOLD=$'\033[1m'; RESET=$'\033[0m'

info()    { echo -e "${CYAN}[info]${RESET}  $*"; }
ok()      { echo -e "${GREEN}[ok]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${RESET}  $*"; }
die()     { echo -e "${RED}[error]${RESET} $*" >&2; exit 1; }
header()  { echo -e "\n${BOLD}=== $* ===${RESET}"; }

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

require_cmd() {
    for cmd in "$@"; do
        command -v "$cmd" &>/dev/null || die "'$cmd' not found. Install it first."
    done
}

require_file() {
    [[ -f "$1" ]] || die "file not found: $1"
}

# Build a specific make target and return the binary path
build_target() {
    local target="$1"   # e.g. bench-perf
    local binary="$2"   # expected binary path

    info "building target '$target'..."
    make -C "${SCRIPT_DIR}" "$target" --no-print-directory
    require_file "$binary"
    ok "binary ready: $binary"
}

# Print a summary box around output
run_with_header() {
    local label="$1"; shift
    echo
    echo -e "${BOLD}>>> ${label}${RESET}"
    "$@"
    echo -e "${BOLD}<<< ${label}${RESET}"
}

# ---------------------------------------------------------------------------
# Subcommand: bench
# ---------------------------------------------------------------------------
cmd_bench() {
    local image="$1"
    local iters="${2:-${DEFAULT_ITERS_BENCH}}"
    local binary="${BUILD_DIR}/bench_decode"

    header "BENCHMARK  –  optimised build, ${iters} iterations"
    require_file "$image"
    build_target bench "$binary"

    mkdir -p "${RESULTS_DIR}"
    local outfile="${RESULTS_DIR}/bench_$(timestamp).txt"

    run_with_header "bench_decode" \
        "$binary" "$image" "$iters" | tee "$outfile"

    ok "results saved to ${outfile}"
}

# ---------------------------------------------------------------------------
# Subcommand: flamegraph
# ---------------------------------------------------------------------------
cmd_flamegraph() {
    local image="$1"
    local iters="${2:-${DEFAULT_ITERS_FLAMEGRAPH}}"
    local binary="${BUILD_DIR}/bench_decode_perf"

    header "FLAMEGRAPH  –  perf + FlameGraph, ${iters} iterations"
    require_file "$image"
    require_cmd perf

    # Check FlameGraph scripts
    local stackcollapse="${FLAMEGRAPH_DIR}/stackcollapse-perf.pl"
    local flamegraph_pl="${FLAMEGRAPH_DIR}/flamegraph.pl"
    if [[ ! -f "$stackcollapse" || ! -f "$flamegraph_pl" ]]; then
        warn "FlameGraph scripts not found at: ${FLAMEGRAPH_DIR}"
        warn "Clone them with:"
        warn "  git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph"
        warn "Or set FLAMEGRAPH_DIR=/path/to/FlameGraph"
        die "FlameGraph not available"
    fi

    build_target bench-perf "$binary"

    mkdir -p "${RESULTS_DIR}"
    local ts; ts=$(timestamp)
    local perf_data="${RESULTS_DIR}/perf_${ts}.data"
    local folded="${RESULTS_DIR}/perf_${ts}.folded"
    local svg="${RESULTS_DIR}/flamegraph_${ts}.svg"

    info "recording with perf (freq=${PERF_FREQ} Hz, ${iters} iters)..."
    info "note: you may need to run as root or allow perf for your user:"
    info "  echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid"

    perf record \
        --freq="${PERF_FREQ}" \
        --call-graph dwarf \
        --output="${perf_data}" \
        -- "$binary" "$image" "$iters"

    info "generating folded stacks..."
    perf script --input="${perf_data}" \
        | perl "${stackcollapse}" \
        > "$folded"

    info "rendering SVG flamegraph..."
    perl "${flamegraph_pl}" \
        --title="slicer png_decode (${iters} iters)" \
        --width=1600 \
        --colors=hot \
        "$folded" \
        > "$svg"

    ok "flamegraph saved: ${svg}"
    info "open with:  xdg-open '${svg}'"

    # Also emit a quick perf report to stdout
    echo
    info "top functions by CPU time (perf report --stdio):"
    perf report \
        --input="${perf_data}" \
        --stdio \
        --no-children \
        --sort=symbol \
        2>/dev/null | head -40 || true
}

# ---------------------------------------------------------------------------
# Subcommand: perf-stat
# ---------------------------------------------------------------------------
cmd_perf_stat() {
    local image="$1"
    local iters="${2:-${DEFAULT_ITERS_PERFSTAT}}"
    local binary="${BUILD_DIR}/bench_decode_perf"

    header "PERF STAT  –  hardware counters, ${iters} iterations"
    require_file "$image"
    require_cmd perf
    build_target bench-perf "$binary"

    mkdir -p "${RESULTS_DIR}"
    local outfile="${RESULTS_DIR}/perf_stat_$(timestamp).txt"

    # Measure: cycles, instructions, cache refs/misses, branches/mispredicts,
    #          page faults, context switches, cpu-migrations, L1/LLC misses
    run_with_header "perf stat" \
        perf stat \
            --repeat=5 \
            --event=cycles,instructions,cache-references,cache-misses,\
branch-instructions,branch-misses,page-faults,context-switches,\
L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses \
            -- "$binary" "$image" "$iters" \
        2>&1 | tee "$outfile"

    ok "perf stat results saved to: ${outfile}"
}

# ---------------------------------------------------------------------------
# Subcommand: gprof
# ---------------------------------------------------------------------------
cmd_gprof() {
    local image="$1"
    local iters="${2:-${DEFAULT_ITERS_GPROF}}"
    local binary="${BUILD_DIR}/bench_decode_prof"

    header "GPROF  –  GNU profiler, ${iters} iterations"
    require_file "$image"
    require_cmd gprof
    build_target bench-prof "$binary"

    mkdir -p "${RESULTS_DIR}"
    local ts; ts=$(timestamp)
    local gmon="${RESULTS_DIR}/gmon_${ts}.out"
    local flat_out="${RESULTS_DIR}/gprof_flat_${ts}.txt"
    local call_out="${RESULTS_DIR}/gprof_callgraph_${ts}.txt"

    # gprof writes gmon.out to CWD; run from RESULTS_DIR
    # Resolve to absolute path before cd-ing away from the project root
    image="$(realpath "$image")"

    info "running profiled binary (${iters} iters)..."
    (
        cd "${RESULTS_DIR}"
        "${binary}" "$image" "$iters" > /dev/null
        mv gmon.out "$gmon" 2>/dev/null || true
    )

    if [[ ! -f "$gmon" ]]; then
        # Some systems write gmon.out next to the binary
        if [[ -f "${BUILD_DIR}/gmon.out" ]]; then
            mv "${BUILD_DIR}/gmon.out" "$gmon"
        else
            die "gmon.out not produced – is the binary built with -pg?"
        fi
    fi

    info "generating flat profile..."
    gprof -b -p "$binary" "$gmon" > "$flat_out"

    info "generating call graph..."
    gprof -b -q "$binary" "$gmon" > "$call_out"

    echo
    info "--- flat profile (top 30 lines) ---"
    head -30 "$flat_out"
    echo
    info "--- call graph (top 60 lines) ---"
    head -60 "$call_out"

    ok "flat profile  : ${flat_out}"
    ok "call graph    : ${call_out}"
}

# ---------------------------------------------------------------------------
# Subcommand: callgrind
# ---------------------------------------------------------------------------
cmd_callgrind() {
    local image="$1"
    local iters="${2:-${DEFAULT_ITERS_CALLGRIND}}"
    local binary="${BUILD_DIR}/bench_decode_perf"

    header "CALLGRIND  –  valgrind instruction-level profiler, ${iters} iterations"
    require_file "$image"
    require_cmd valgrind
    build_target bench-perf "$binary"

    mkdir -p "${RESULTS_DIR}"
    local ts; ts=$(timestamp)
    local cg_out="${RESULTS_DIR}/callgrind_${ts}.out"

    info "running under valgrind --tool=callgrind  (this is ~50x slower – be patient)"
    valgrind \
        --tool=callgrind \
        --callgrind-out-file="$cg_out" \
        --cache-sim=yes \
        --branch-sim=yes \
        --collect-jumps=yes \
        -- "$binary" "$image" "$iters"

    ok "callgrind output: ${cg_out}"

    # Print a quick summary via callgrind_annotate
    if command -v callgrind_annotate &>/dev/null; then
        local ann_out="${RESULTS_DIR}/callgrind_annotate_${ts}.txt"
        callgrind_annotate --auto=yes "$cg_out" > "$ann_out" 2>&1 || true
        info "annotation (top 40 lines):"
        head -40 "$ann_out" || true
        ok "full annotation: ${ann_out}"
    fi

    if command -v kcachegrind &>/dev/null; then
        info "open in kcachegrind with:  kcachegrind '${cg_out}'"
    else
        info "for GUI analysis install kcachegrind:  sudo xbps-install kcachegrind"
    fi
}

# ---------------------------------------------------------------------------
# Subcommand: massif (heap profiler)
# ---------------------------------------------------------------------------
cmd_massif() {
    local image="$1"
    local iters="${2:-${DEFAULT_ITERS_MASSIF}}"
    local binary="${BUILD_DIR}/bench_decode_perf"

    header "MASSIF  –  valgrind heap profiler, ${iters} iterations"
    require_file "$image"
    require_cmd valgrind
    build_target bench-perf "$binary"

    mkdir -p "${RESULTS_DIR}"
    local ts; ts=$(timestamp)
    local ms_out="${RESULTS_DIR}/massif_${ts}.out"
    local ms_txt="${RESULTS_DIR}/massif_${ts}.txt"

    info "running under valgrind --tool=massif..."
    valgrind \
        --tool=massif \
        --massif-out-file="$ms_out" \
        --pages-as-heap=no \
        --detailed-freq=1 \
        -- "$binary" "$image" "$iters"

    if command -v ms_print &>/dev/null; then
        ms_print "$ms_out" > "$ms_txt"
        info "heap profile (top 60 lines):"
        head -60 "$ms_txt" || true
        ok "full massif text: ${ms_txt}"
    fi

    ok "massif raw output: ${ms_out}"
    if command -v massif-visualizer &>/dev/null; then
        info "open in massif-visualizer:  massif-visualizer '${ms_out}'"
    fi
}

# ---------------------------------------------------------------------------
# Subcommand: asan (AddressSanitizer + UBSan)
# ---------------------------------------------------------------------------
cmd_asan() {
    local image="$1"
    local iters="${2:-${DEFAULT_ITERS_ASAN}}"
    local binary="${BUILD_DIR}/bench_decode_asan"

    header "ASAN / UBSAN  –  memory & undefined-behaviour sanitizers, ${iters} iters"
    require_file "$image"
    build_target bench-asan "$binary"

    mkdir -p "${RESULTS_DIR}"
    local outfile="${RESULTS_DIR}/asan_$(timestamp).txt"

    info "running under ASan+UBSan (any report = bug)..."
    ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:log_path=${outfile}" \
    UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \
        "$binary" "$image" "$iters"

    # Check if ASan produced any reports
    local reports
    reports=$(ls "${outfile}".* 2>/dev/null | wc -l || echo 0)
    if [[ "$reports" -gt 0 ]]; then
        warn "ASan/UBSan issues found! Reports:"
        ls "${outfile}".*
        cat "${outfile}".* | head -80
    else
        ok "no ASan/UBSan issues detected"
    fi
}

# ---------------------------------------------------------------------------
# Subcommand: all
# ---------------------------------------------------------------------------
cmd_all() {
    local image="$1"
    local iters="${2:-}"

    header "ALL PROFILES  –  bench + perf-stat + flamegraph + gprof"
    require_file "$image"

    cmd_bench     "$image" "${iters:-${DEFAULT_ITERS_BENCH}}"
    cmd_perf_stat "$image" "${iters:-${DEFAULT_ITERS_PERFSTAT}}"
    cmd_gprof     "$image" "${iters:-${DEFAULT_ITERS_GPROF}}"
    cmd_flamegraph "$image" "${iters:-${DEFAULT_ITERS_FLAMEGRAPH}}"

    header "ALL DONE"
    ok "all results in: ${RESULTS_DIR}/"
    ls -lh "${RESULTS_DIR}/" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Timestamp helper
# ---------------------------------------------------------------------------
timestamp() { date '+%Y%m%d_%H%M%S'; }

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
usage() {
    cat <<EOF
${BOLD}perf_profile.sh${RESET} – profiling toolkit for slicer png_decoder

${BOLD}Usage:${RESET}
  $0 <subcommand> <image> [iterations]

${BOLD}Subcommands:${RESET}
  bench       <image> [iters]   Timing benchmark (mean/min/max/stddev/histogram)
  flamegraph  <image> [iters]   CPU flamegraph via perf + FlameGraph SVG
  perf-stat   <image> [iters]   Hardware counters (IPC, cache misses, branches)
  gprof       <image> [iters]   GNU gprof flat profile and call graph
  callgrind   <image> [iters]   Valgrind callgrind (instruction-level profiling)
  massif      <image> [iters]   Valgrind massif (heap allocation profiling)
  asan        <image> [iters]   AddressSanitizer + UBSan correctness check
  all         <image> [iters]   bench + perf-stat + gprof + flamegraph

${BOLD}Examples:${RESET}
  $0 bench      sample/test1.png 500
  $0 flamegraph sample/test1.png 2000
  $0 perf-stat  sample/test1.png
  $0 callgrind  sample/test1.png 10
  $0 all        sample/test1.png

${BOLD}FlameGraph setup (one-time):${RESET}
  git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph

${BOLD}Kernel perf permissions (one-time):${RESET}
  echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid
  # make permanent (survives reboot):
  echo 'kernel.perf_event_paranoid = 1' | sudo tee -a /etc/sysctl.conf

${BOLD}Results:${RESET} all output is saved to  ${RESULTS_DIR}/

EOF
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
main() {
    if [[ $# -lt 1 ]]; then
        usage
        exit 1
    fi

    local subcmd="$1"; shift

    case "$subcmd" in
        bench)
            [[ $# -ge 1 ]] || { usage; die "bench requires <image>"; }
            cmd_bench "$@"
            ;;
        flamegraph)
            [[ $# -ge 1 ]] || { usage; die "flamegraph requires <image>"; }
            cmd_flamegraph "$@"
            ;;
        perf-stat)
            [[ $# -ge 1 ]] || { usage; die "perf-stat requires <image>"; }
            cmd_perf_stat "$@"
            ;;
        gprof)
            [[ $# -ge 1 ]] || { usage; die "gprof requires <image>"; }
            cmd_gprof "$@"
            ;;
        callgrind)
            [[ $# -ge 1 ]] || { usage; die "callgrind requires <image>"; }
            cmd_callgrind "$@"
            ;;
        massif)
            [[ $# -ge 1 ]] || { usage; die "massif requires <image>"; }
            cmd_massif "$@"
            ;;
        asan)
            [[ $# -ge 1 ]] || { usage; die "asan requires <image>"; }
            cmd_asan "$@"
            ;;
        all)
            [[ $# -ge 1 ]] || { usage; die "all requires <image>"; }
            cmd_all "$@"
            ;;
        -h|--help|help)
            usage
            ;;
        *)
            usage
            die "unknown subcommand: '$subcmd'"
            ;;
    esac
}

main "$@"
