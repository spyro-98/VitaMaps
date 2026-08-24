#!/usr/bin/env bash
set -euo pipefail

: "${VITASDK:?Set VITASDK to the absolute VitaSDK path}"

project_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$project_root"

git submodule update --init --recursive
if [[ ! -f external/vita-https/build/deps/curl-mbedtls/lib/libcurl.a ]]; then
  external/vita-https/tools/build-curl-mbedtls.sh
fi

cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel "${VITAMAPS_BUILD_JOBS:-4}"

echo "VitaMaps debug package: $project_root/build-debug/VitaMaps.vpk"
