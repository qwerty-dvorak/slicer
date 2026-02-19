CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -pedantic -std=c99
LDFLAGS ?=
LDLIBS ?= -lxcb -ldl -pthread

# Auto-detect LTO support
LTO_FLAG := $(shell $(CC) -flto -x c -c /dev/null -o /dev/null 2>/dev/null && echo -flto)

BUILDDIR := build
TARGET   := $(BUILDDIR)/xcb-view
BENCH    := $(BUILDDIR)/bench_decode

SRC      := main.c cli.c viewer.c renderer.c image.c png_decoder.c
BENCH_SRC := bench_decode.c image.c png_decoder.c

GEN_SCRIPT := gen_procedural_pngs.py

# --------------------------------------------------------------------
# Flags for each build mode
# --------------------------------------------------------------------
# Common aggressive optimisation knobs shared by fast builds
OPT_COMMON    := -funroll-loops -ftree-vectorize

# bench      : optimised, no debug, just for timing numbers
BENCH_CFLAGS  := -O3 $(OPT_COMMON) -Wall -Wextra -pedantic -std=c99
# bench-native : maximum speed for the *current* CPU (not portable!)
NATIVE_CFLAGS := -O3 -march=native $(OPT_COMMON) -Wall -Wextra -pedantic -std=c99
NATIVE_LDFLAGS := $(LTO_FLAG)
# bench-perf : keep frame pointers so perf/FlameGraph can unwind the stack
PERF_CFLAGS   := -O3 $(OPT_COMMON) -g -fno-omit-frame-pointer -Wall -Wextra -pedantic -std=c99
# bench-perf-native : perf build with -march=native + LTO
PERF_NATIVE_CFLAGS := -O3 -march=native $(OPT_COMMON) -g -fno-omit-frame-pointer \
                       -Wall -Wextra -pedantic -std=c99
PERF_NATIVE_LDFLAGS := $(LTO_FLAG)
# bench-prof : GNU gprof instrumentation
PROF_CFLAGS   := -O2 -pg -g -fno-omit-frame-pointer -Wall -Wextra -pedantic -std=c99
PROF_LDFLAGS  := -pg
# bench-asan : AddressSanitizer + UBSan for correctness checking
ASAN_CC       := clang
ASAN_CFLAGS   := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
                 -Wall -Wextra -pedantic -std=c99
ASAN_LDFLAGS  := -fsanitize=address,undefined

# bench-pgo  : Profile-Guided Optimization (two-step build)
PGO_PROFDIR   := $(BUILDDIR)/pgo_profiles
PGO_GEN_CFLAGS  := -O2 -march=native -fprofile-generate=$(PGO_PROFDIR) \
                    -Wall -Wextra -pedantic -std=c99
PGO_GEN_LDFLAGS := -fprofile-generate=$(PGO_PROFDIR)
PGO_USE_CFLAGS  := -O3 -march=native $(OPT_COMMON) $(LTO_FLAG) \
                    -fprofile-use=$(PGO_PROFDIR) -fprofile-correction \
                    -Wno-missing-profile -Wall -Wextra -pedantic -std=c99
PGO_USE_LDFLAGS := $(LTO_FLAG) -fprofile-use=$(PGO_PROFDIR)

BENCH_LDLIBS := -ldl -pthread

.PHONY: all clean gen-samples \
        bench bench-native bench-perf bench-perf-native bench-prof bench-asan \
        bench-pgo-gen bench-pgo bench-pgo-auto

# --------------------------------------------------------------------
# Default target: the viewer
# --------------------------------------------------------------------
all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(SRC) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

# --------------------------------------------------------------------
# Benchmark targets  (all decode-only, no XCB needed)
# --------------------------------------------------------------------

# Plain optimised build – use this for raw timing numbers
bench: $(BENCH_SRC) | $(BUILDDIR)
	$(CC) $(BENCH_CFLAGS) $(LDFLAGS) -o $(BENCH) $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "built: $(BENCH)  (-O3, use for timing)"

# Maximum-speed build for the *current* CPU (not portable)
bench-native: $(BENCH_SRC) | $(BUILDDIR)
	$(CC) $(NATIVE_CFLAGS) $(NATIVE_LDFLAGS) $(LDFLAGS) -o $(BUILDDIR)/bench_decode_native $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "built: $(BUILDDIR)/bench_decode_native  (-O3 -march=native -flto)"

# perf / FlameGraph build – frame pointers preserved for stack unwinding
bench-perf: $(BENCH_SRC) | $(BUILDDIR)
	$(CC) $(PERF_CFLAGS) $(LDFLAGS) -o $(BUILDDIR)/bench_decode_perf $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "built: $(BUILDDIR)/bench_decode_perf  (perf/flamegraph ready)"

# perf build with -march=native + LTO (best of both worlds)
bench-perf-native: $(BENCH_SRC) | $(BUILDDIR)
	$(CC) $(PERF_NATIVE_CFLAGS) $(PERF_NATIVE_LDFLAGS) $(LDFLAGS) -o $(BUILDDIR)/bench_decode_perf_native $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "built: $(BUILDDIR)/bench_decode_perf_native  (perf + -march=native -flto)"

# gprof build
bench-prof: $(BENCH_SRC) | $(BUILDDIR)
	$(CC) $(PROF_CFLAGS) $(PROF_LDFLAGS) -o $(BUILDDIR)/bench_decode_prof $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "built: $(BUILDDIR)/bench_decode_prof  (gprof instrumented)"

# ASan + UBSan build – use clang; gcc requires a separately installed libasan
bench-asan: $(BENCH_SRC) | $(BUILDDIR)
	$(ASAN_CC) $(ASAN_CFLAGS) $(ASAN_LDFLAGS) -o $(BUILDDIR)/bench_decode_asan $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "built: $(BUILDDIR)/bench_decode_asan  (ASan/UBSan enabled, clang)"

# --------------------------------------------------------------------
# PGO targets  (Profile-Guided Optimization, two-step workflow)
# --------------------------------------------------------------------
#
# Usage:
#   1)  make bench-pgo-gen
#   2)  ./build/bench_decode_pgo_gen sample/test1.png 50   (or your workload)
#   3)  make bench-pgo
#
# Or use the all-in-one target (requires PGO_TRAIN_IMG):
#   make bench-pgo-auto PGO_TRAIN_IMG=sample/test1.png PGO_TRAIN_ITERS=50
#

# Step 1: build the instrumented binary that records profile counters
bench-pgo-gen: $(BENCH_SRC) | $(BUILDDIR)
	@rm -rf $(PGO_PROFDIR)
	@mkdir -p $(PGO_PROFDIR)
	$(CC) $(PGO_GEN_CFLAGS) $(PGO_GEN_LDFLAGS) $(LDFLAGS) \
	      -o $(BUILDDIR)/bench_decode_pgo_gen $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "built: $(BUILDDIR)/bench_decode_pgo_gen  (instrumented for profiling)"
	@echo ""
	@echo ">>> Now run your representative workload, e.g.:"
	@echo ">>>   ./$(BUILDDIR)/bench_decode_pgo_gen sample/test1.png 50"
	@echo ">>> Then run:  make bench-pgo"

# Step 2: rebuild using the collected profile data
bench-pgo: $(BENCH_SRC) | $(BUILDDIR)
	@if [ ! -d "$(PGO_PROFDIR)" ] || [ -z "$$(ls -A $(PGO_PROFDIR) 2>/dev/null)" ]; then \
	    echo "error: no profile data in $(PGO_PROFDIR)"; \
	    echo "       run 'make bench-pgo-gen' and exercise the binary first"; \
	    exit 1; \
	fi
	$(CC) $(PGO_USE_CFLAGS) $(PGO_USE_LDFLAGS) $(LDFLAGS) \
	      -o $(BUILDDIR)/bench_decode_pgo $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "built: $(BUILDDIR)/bench_decode_pgo  (PGO + -O3 -march=native -flto)"

# All-in-one: gen → train → use  (set PGO_TRAIN_IMG and optionally PGO_TRAIN_ITERS)
PGO_TRAIN_ITERS ?= 50
bench-pgo-auto: $(BENCH_SRC) | $(BUILDDIR)
	@if [ -z "$(PGO_TRAIN_IMG)" ]; then \
	    echo "error: set PGO_TRAIN_IMG, e.g.:"; \
	    echo "  make bench-pgo-auto PGO_TRAIN_IMG=sample/test1.png"; \
	    exit 1; \
	fi
	@echo "=== PGO step 1/3: building instrumented binary ==="
	@rm -rf $(PGO_PROFDIR)
	@mkdir -p $(PGO_PROFDIR)
	$(CC) $(PGO_GEN_CFLAGS) $(PGO_GEN_LDFLAGS) $(LDFLAGS) \
	      -o $(BUILDDIR)/bench_decode_pgo_gen $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo "=== PGO step 2/3: training with $(PGO_TRAIN_IMG) ($(PGO_TRAIN_ITERS) iters) ==="
	./$(BUILDDIR)/bench_decode_pgo_gen $(PGO_TRAIN_IMG) $(PGO_TRAIN_ITERS)
	@echo "=== PGO step 3/3: rebuilding with profile feedback ==="
	$(CC) $(PGO_USE_CFLAGS) $(PGO_USE_LDFLAGS) $(LDFLAGS) \
	      -o $(BUILDDIR)/bench_decode_pgo $(BENCH_SRC) $(BENCH_LDLIBS)
	@echo ""
	@echo "built: $(BUILDDIR)/bench_decode_pgo  (PGO + -O3 -march=native -flto)"

# --------------------------------------------------------------------
gen-samples:
	python3 $(GEN_SCRIPT) --out sample

clean:
	rm -rf $(BUILDDIR)
