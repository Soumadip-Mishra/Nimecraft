# Linux shortcuts around the CMake presets in CMakePresets.json.
# The build itself lives in CMake; this file only picks a preset and runs the result.

FILTER ?=
JOBS ?= $(shell nproc)
TEST_ARGS = $(if $(FILTER),"$(FILTER)")

# Hide Wine's "fixme" noise but keep its errors (for example a missing DLL).
export WINEDEBUG ?= fixme-all

.PHONY: all release test tsan coverage win run clean

all: release

# Configure a preset once, and again whenever CMakePresets.json changes.
# Later CMakeLists.txt edits are picked up by `cmake --build` itself.
build/%/.configured: CMakePresets.json
	cmake --preset $*
	@touch $@

release: build/release/.configured
	cmake --build --preset release -j $(JOBS)

test: build/dev/.configured
	cmake --build --preset dev -j $(JOBS)
	./build/dev/nimecraft_tests $(TEST_ARGS)

tsan: build/tsan/.configured
	cmake --build --preset tsan -j $(JOBS)
	./build/tsan/nimecraft_tests $(TEST_ARGS)

coverage: build/coverage/.configured
	cmake --build --preset coverage -j $(JOBS)
	find build/coverage -name '*.gcda' -delete
	./build/coverage/nimecraft_tests
	python3 tools/coverage_check.py build/coverage

win: build/win-cross/.configured
	cmake --build --preset win-cross -j $(JOBS)
	wine build/win-cross/nimecraft_tests.exe $(TEST_ARGS)

run: release
	./build/release/nimecraft

clean:
	rm -rf build
