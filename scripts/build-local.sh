#!/usr/bin/env bash
# Development build + headless tests for every version you pass.
# usage: scripts/build-local.sh [--ddpdfk ddpdfk.zip] [--ddpdfk10 ddpdfk10.zip] [--dfkbl dfkbl.zip] [build-dir]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
ARGS=()
BUILD_DIR=build/cmake
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ddpdfk) ARGS+=("-DDFK_ROM_DDPDFK=$2"); shift 2;;
    --ddpdfk10) ARGS+=("-DDFK_ROM_DDPDFK10=$2"); shift 2;;
    --dfkbl) ARGS+=("-DDFK_ROM_DFKBL=$2"); shift 2;;
    *) BUILD_DIR=$1; shift;;
  esac
done
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release "${ARGS[@]}"
cmake --build "$BUILD_DIR"
(cd "$BUILD_DIR" && ctest --output-on-failure)
echo "Build and headless tests passed."
