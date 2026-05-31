#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build}"
CONFIG="${CONFIG:-Release}"
JOBS="${JOBS:-}"

if [[ -z "$JOBS" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS="2"
  fi
fi

VERSION="$(python3 - <<'PY'
import re
from pathlib import Path
text = Path('CMakeLists.txt').read_text(encoding='utf-8')
match = re.search(r'project\s*\(\s*subcli\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', text, re.I)
print(match.group(1) if match else '')
PY
)"

if [[ -z "$VERSION" ]]; then
  echo "error: could not read project version from CMakeLists.txt" >&2
  exit 1
fi

echo "=== Building subcli v$VERSION ==="
echo "Build dir: $BUILD_DIR"
echo "Config:    $CONFIG"
echo "Jobs:      $JOBS"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR" --config "$CONFIG" -j "$JOBS"
ctest --test-dir "$BUILD_DIR" --build-config "$CONFIG" --output-on-failure
cmake --build "$BUILD_DIR" --config "$CONFIG" --target package

echo "=== Package artifacts ==="
find "$BUILD_DIR" -maxdepth 1 -type f \
  \( -name 'subcli-*.tar.gz' -o -name 'subcli-*.tgz' -o -name 'subcli-*.zip' -o -name 'subcli-*.deb' \) \
  -print | sort

echo "success: local package build completed"
