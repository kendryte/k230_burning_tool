#!/bin/bash
set -euo pipefail

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

# Get Git revision string
REVISION=$(git describe --long --tag --dirty --always || echo "unknown")
echo "Git revision: $REVISION"

# Clean build directory
rm -rf build
mkdir -p build

export CI=1

build_variant() {
    local VARIANT=$1
    local BUILD_FOR_AVALON_NANO3
    local TARGET_SUFFIX

    if [[ "$VARIANT" == "avalon3" ]]; then
        BUILD_FOR_AVALON_NANO3="ON"
        TARGET_SUFFIX="avalon"
    else
        BUILD_FOR_AVALON_NANO3="OFF"
        TARGET_SUFFIX="normal"
    fi

    echo "=== Building variant: $VARIANT ==="

    VARIANT_BUILD_DIR="./build/${VARIANT}"
    VARIANT_INSTALL_DIR="${VARIANT_BUILD_DIR}/install"
    mkdir -p $VARIANT_INSTALL_DIR

    # Configure
    qt-cmake . -G Ninja \
        -B "$VARIANT_BUILD_DIR" \
        -DBUILD_FOR_AVALON_NANO3="$BUILD_FOR_AVALON_NANO3" \
        -DCMAKE_INSTALL_PREFIX="$VARIANT_INSTALL_DIR"

    # Build
    cmake --build "$VARIANT_BUILD_DIR" --parallel

    # Install
    cmake --install "$VARIANT_BUILD_DIR"

    # Package
    ARTIFACTS_NAME="K230BurningTool_${OS}_${TARGET_SUFFIX}_${REVISION}"
    (
        cd "$VARIANT_INSTALL_DIR" || exit 1

        case "$OS" in
            windows)
                zip -r "../../${ARTIFACTS_NAME}.zip" ./*
                ;;
            macos)
                mv ../gui/K230BurningTool.dmg "../../${ARTIFACTS_NAME}.dmg"
                ;;
            linux)
                tar -czf "../../${ARTIFACTS_NAME}.tar.gz" ./*
                ;;
            *)
                echo "Unsupported OS: $OS"
                exit 1
                ;;
        esac
    )

    echo "Artifact created: build/${ARTIFACTS_NAME}"
}

# Build both variants
build_variant normal
build_variant avalon3

echo "All builds complete."
