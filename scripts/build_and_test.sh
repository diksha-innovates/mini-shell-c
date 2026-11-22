#!/usr/bin/env bash
# Configures, builds, and tests mini-shell-c from a clean clone:
#   - Debug build + CTest (tokenizer/parser unit tests)
#   - AddressSanitizer build + CTest
#   - UndefinedBehaviorSanitizer build + CTest
#   - Python integration tests (builtins, pipelines, redirection, signals,
#     job control) against the Debug binary
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found on PATH; bootstrapping a project-local copy via pip (no sudo, no global install)..."
  python3 -m venv "$ROOT_DIR/.venv"
  "$ROOT_DIR/.venv/bin/pip" install --quiet --upgrade pip
  "$ROOT_DIR/.venv/bin/pip" install --quiet cmake ninja
  export PATH="$ROOT_DIR/.venv/bin:$PATH"
fi

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
fi

build_and_test() {
  local dir="$1"
  shift
  echo "=== Configuring $dir ($*) ==="
  cmake -S . -B "$dir" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Debug "$@"
  echo "=== Building $dir ==="
  cmake --build "$dir" --
  echo "=== Testing $dir ==="
  ctest --test-dir "$dir" --output-on-failure
}

build_and_test build
build_and_test build-asan -DENABLE_ASAN=ON
build_and_test build-ubsan -DENABLE_UBSAN=ON

echo "=== Python integration tests (tests/python) against build/minishell ==="
if [ ! -d "$ROOT_DIR/.venv" ]; then
  python3 -m venv "$ROOT_DIR/.venv"
fi
"$ROOT_DIR/.venv/bin/pip" install --quiet --upgrade pip
"$ROOT_DIR/.venv/bin/pip" install --quiet -r tests/python/requirements.txt
MINISHELL_BIN="$ROOT_DIR/build/minishell" "$ROOT_DIR/.venv/bin/python" -m pytest tests/python -v

echo
echo "All builds and tests passed."
