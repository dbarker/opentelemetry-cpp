#!/bin/bash

# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

set -e

CMAKE_VERSION="${CMAKE_VERSION:-3.31.6}"
CMAKE_PKG="cmake-${CMAKE_VERSION}-macos-universal"
CMAKE_TAR="${CMAKE_PKG}.tar.gz"
CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/${CMAKE_TAR}"

INSTALL_DIR="/opt/cmake"

echo "Installing CMake version: $CMAKE_VERSION"

if brew list cmake >/dev/null 2>&1; then
  brew uninstall cmake
fi

if ! command -v wget >/dev/null 2>&1; then
  brew install wget
fi

wget -q "$CMAKE_URL"
mkdir -p "$INSTALL_DIR"
tar --strip-components=1 -xzf "$CMAKE_TAR" -C "$INSTALL_DIR"

for file in "$INSTALL_DIR/bin/"*; do
  ln -sf "$file" "/usr/local/bin/$(basename "$file")"
done

rm -f "$CMAKE_TAR"

cmake --version
