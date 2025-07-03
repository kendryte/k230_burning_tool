#!/bin/bash
set -euo pipefail

# Usage: ./build.sh [normal|avalon3]
VARIANT=${1:-normal}
if [[ "$VARIANT" != "normal" && "$VARIANT" != "avalon3" ]]; then
  echo "Usage: $0 [normal|avalon3]"
  exit 1
fi

# Detect OS
UNAME=$(uname | tr '[:upper:]' '[:lower:]')
if [[ "$UNAME" == "darwin" ]]; then
  OS="macos"
elif [[ "$UNAME" == "linux" ]]; then
  OS="linux"
elif [[ "$UNAME" == *"mingw"* || "$UNAME" == *"msys"* || "$UNAME" == *"cygwin"* ]]; then
  OS="windows"
else
  echo "Unsupported OS: $UNAME"
  exit 1
fi

echo "Building on OS: $OS, variant: $VARIANT"

# Set env vars based on variant
if [[ "$VARIANT" == "avalon3" ]]; then
  BUILD_FOR_AVALON_NANO3="ON"
  TARGET_SUFFIX="Avalon3"
else
  BUILD_FOR_AVALON_NANO3="OFF"
  TARGET_SUFFIX="Normal"
fi

# Get Git revision string
REVISION=$(git describe --long --tag --dirty --always || echo "unknown")
echo "Git revision: $REVISION"

# Clean build directory
rm -rf build
mkdir -p build

# Run the build steps natively

export CI=1

# Run cmake config
qt-cmake . -G Ninja -B ./build -DBUILD_FOR_AVALON_NANO3=$BUILD_FOR_AVALON_NANO3

# Build
cmake --build ./build

# Install
cmake --install ./build

ls -alh ./build

echo "Build complete! Artifact: $ZIP_NAME"
