#!/usr/bin/env bash
# Chitralipi installer for Termux and Linux.
#
#   curl -fsSL https://raw.githubusercontent.com/suvojit20070/chitralipi/main/scripts/install.sh | bash
#
# Installs: chitralipi command, libchitralipi.so, chitralipi.h, pkg-config file,
# CMake package, and the Python module (if Python is present).
# Settings (environment): CHITRALIPI_PREFIX (default $PREFIX in Termux, else /usr/local),
#                         CHITRALIPI_REF (branch or tag, default main).
# SPDX-License-Identifier: MIT
set -euo pipefail

REPO="https://github.com/suvojit20070/chitralipi.git"
REF="${CHITRALIPI_REF:-main}"
if [ -n "${TERMUX_VERSION:-}" ] || [ -d /data/data/com.termux ]; then
  IS_TERMUX=1
  DEST="${CHITRALIPI_PREFIX:-$PREFIX}"
else
  IS_TERMUX=0
  DEST="${CHITRALIPI_PREFIX:-/usr/local}"
fi
SUDO=""
if [ "$IS_TERMUX" = 0 ] && [ ! -w "$DEST" ] && command -v sudo >/dev/null; then SUDO="sudo"; fi

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

say "Chitralipi installer (ref: $REF, prefix: $DEST)"
if [ "$IS_TERMUX" = 1 ]; then
  say "Installing build tools with pkg"
  pkg install -y clang cmake git make pkg-config >/dev/null
else
  for tool in git cmake c++ make; do
    command -v "$tool" >/dev/null || { echo "Please install $tool first (git, cmake, a C++17 compiler, make)."; exit 1; }
  done
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/chitralipi.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

say "Downloading source"
git clone --quiet --depth 1 --branch "$REF" "$REPO" "$WORK/src"

say "Building"
cmake -S "$WORK/src" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEST" \
  -DCHITRALIPI_FETCH_STB=ON -DCHITRALIPI_BUILD_TESTS=ON >/dev/null
cmake --build "$WORK/build" --parallel "$(nproc 2>/dev/null || echo 4)" >/dev/null

say "Testing"
ctest --test-dir "$WORK/build" --output-on-failure >/dev/null

say "Installing"
$SUDO cmake --install "$WORK/build" >/dev/null

PY="$(command -v python3 || command -v python || true)"
if [ -n "$PY" ]; then
  SITE="$("$PY" -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')"
  if [ -w "$SITE" ] || [ -n "$SUDO" ]; then
    $SUDO cp "$WORK/src/bindings/python/chitralipi.py" "$SITE/"
    say "Python module installed: import chitralipi"
  fi
fi
[ "$IS_TERMUX" = 0 ] && command -v ldconfig >/dev/null && $SUDO ldconfig 2>/dev/null || true

say "Done. Try:  chitralipi --help"
