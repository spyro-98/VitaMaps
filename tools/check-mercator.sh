#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
binary="$(mktemp "${TMPDIR:-/tmp}/vitamaps-mercator.XXXXXX")"
trap 'rm -f "$binary"' EXIT

"${CXX:-c++}" -std=c++17 -O3 -Wall -Wextra -Wpedantic \
  -I"$project_root/src" \
  "$project_root/tools/mercator_smoke.cpp" \
  "$project_root/src/map/mercator.cpp" \
  "$project_root/src/map/map_camera.cpp" \
  "$project_root/src/map/pin_geometry.cpp" \
  -o "$binary"
"$binary"
