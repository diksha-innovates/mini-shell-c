# Thin convenience wrapper around CMake; the real build logic lives in
# CMakeLists.txt and scripts/build_and_test.sh.

BUILD_DIR ?= build

.PHONY: all build test asan ubsan clean check

all: build

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

asan:
	cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
	cmake --build build-asan
	ctest --test-dir build-asan --output-on-failure

ubsan:
	cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=Debug -DENABLE_UBSAN=ON
	cmake --build build-ubsan
	ctest --test-dir build-ubsan --output-on-failure

check:
	./scripts/build_and_test.sh

clean:
	rm -rf build build-asan build-ubsan
