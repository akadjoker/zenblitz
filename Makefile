# zen — convenience wrapper around the CMake build.
#
# CMake is the real build: it owns the optional modules (regex, zip, net,
# http, crypto, json, utf8), the vendored C/C++ libraries (libregexp, miniz,
# tinyxml2) and their per-target defines. This file used to be a second,
# hand-maintained build over a src/ directory that stopped existing when the
# sources moved to libzen/src/ — `make` failed on a fresh clone. Rather than
# duplicate the CMake logic and let it rot again, these targets shell out.

BUILD_DIR ?= build
BUILD_TYPE ?= Release
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

.PHONY: all release debug clean test bench run

all: release

release:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR) -j$(JOBS)

# Debug turns on ASan+UBSan at -O0 (see CMakeLists.txt) — correctness, not speed.
debug:
	cmake -S . -B $(BUILD_DIR)-debug -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR)-debug -j$(JOBS)

test: release
	./tests/run_zen_tests.sh ./bin/zen

bench: release
	./bench/algo/run.sh

run: release
	./bin/zen

clean:
	rm -rf $(BUILD_DIR) $(BUILD_DIR)-debug
