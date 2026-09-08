#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./release.sh

Builds, installs, validates, and packages the normal and Avalon variants.

Environment variables:
  K230_BURNING_TARGET_OS       linux, macos, or windows (default: host OS)
  K230_BURNING_BUILD_DIR       build and artifact directory (default: ./build)
  QT_CMAKE                     path to qt-cmake
  CMAKE_GENERATOR              default: Ninja when available, otherwise Makefiles

macOS release variables:
  MACOS_SIGN_IDENTITY          required Developer ID Application identity
  MACOS_ARCHITECTURES          architecture list (default: current machine)
  MACOS_DEPLOYMENT_TARGET      optional deployment target
  MACOS_NOTARY_PROFILE         optional notarytool keychain profile
  MACOS_ALLOW_UNSIGNED=1       CI validation only; marks artifacts as unsigned
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi
if [[ $# -ne 0 ]]; then
  echo "Unexpected argument: $1" >&2
  usage >&2
  exit 1
fi

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
BUILD_ROOT=${K230_BURNING_BUILD_DIR:-"$REPO_ROOT/build"}

# Detect OS
UNAME=$(uname | tr '[:upper:]' '[:lower:]')
if [[ "$UNAME" == "darwin" ]]; then
  HOST_OS="macos"
elif [[ "$UNAME" == "linux" ]]; then
  HOST_OS="linux"
elif [[ "$UNAME" == *"mingw"* || "$UNAME" == *"msys"* || "$UNAME" == *"cygwin"* ]]; then
  HOST_OS="windows"
else
  echo "Unsupported OS: $UNAME"
  exit 1
fi
OS=${K230_BURNING_TARGET_OS:-$HOST_OS}
case "$OS" in
  linux|macos|windows) ;;
  *)
    echo "K230_BURNING_TARGET_OS must be linux, macos, or windows." >&2
    exit 1
    ;;
esac

if [[ "$OS" == "linux" ]] && ! command -v linuxdeployqt >/dev/null 2>&1; then
  echo "linuxdeployqt is required to bundle the Linux Qt libraries." >&2
  exit 1
fi
if [[ "$OS" == "linux" ]]; then
  # Allow AppImage-based deployment tools to run on systems where FUSE is not
  # available (for example, an unprivileged container).
  export APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-1}
fi

QT_CMAKE=${QT_CMAKE:-qt-cmake}
if ! command -v "$QT_CMAKE" >/dev/null 2>&1; then
  echo "Qt's qt-cmake was not found; add it to PATH or set QT_CMAKE." >&2
  exit 1
fi

BUILD_GENERATOR=${CMAKE_GENERATOR:-}
if [[ -z "$BUILD_GENERATOR" ]]; then
  if command -v ninja >/dev/null 2>&1; then
    BUILD_GENERATOR="Ninja"
  else
    BUILD_GENERATOR="Unix Makefiles"
  fi
fi

MACOS_SIGN_IDENTITY=${MACOS_SIGN_IDENTITY:-}
MACOS_NOTARY_PROFILE=${MACOS_NOTARY_PROFILE:-}
if [[ "$OS" == "macos" ]]; then
  if [[ -z "$MACOS_SIGN_IDENTITY" && "${MACOS_ALLOW_UNSIGNED:-0}" != "1" ]]; then
    echo "MACOS_SIGN_IDENTITY is required for a macOS release." >&2
    echo "For CI-only validation, explicitly set MACOS_ALLOW_UNSIGNED=1." >&2
    exit 1
  fi
  if [[ -n "$MACOS_SIGN_IDENTITY" ]] && ! command -v security >/dev/null 2>&1; then
    echo "Apple's security command is required for a macOS release." >&2
    exit 1
  fi
  if [[ -n "$MACOS_SIGN_IDENTITY" ]] &&
     ! security find-identity -v -p codesigning | grep -F -- "$MACOS_SIGN_IDENTITY" >/dev/null; then
    echo "Signing identity was not found: $MACOS_SIGN_IDENTITY" >&2
    security find-identity -v -p codesigning >&2
    exit 1
  fi
fi

# Get Git revision string
REVISION=$(git -C "$REPO_ROOT" describe --long --tags --dirty --always || echo "unknown")
ARTIFACT_REVISION=${REVISION//\//-}
echo "Git revision: $REVISION"

# Keep the root directory itself intact so it may be a mounted CI output
# directory. Each variant is cleaned independently below.
mkdir -p "$BUILD_ROOT"

export CI=1

build_variant() {
    local VARIANT=$1
    local BUILD_FOR_AVALON_NANO3
    local TARGET_SUFFIX
    local VARIANT_BUILD_DIR
    local VARIANT_INSTALL_DIR
    local ARTIFACTS_NAME
    local ARTIFACT_PATH
    local CHECKSUM_PATH
    local INSTALLED_EXECUTABLE
    local LDD_OUTPUT
    local EXPECTED_INSTALL_HELPER
    local INSTALL_HOOK_FOUND=0
    local -a CMAKE_EXTRA_ARGS=()

    if [[ "$VARIANT" == "avalon3" ]]; then
        BUILD_FOR_AVALON_NANO3="ON"
        TARGET_SUFFIX="avalon"
    else
        BUILD_FOR_AVALON_NANO3="OFF"
        TARGET_SUFFIX="normal"
    fi

    echo "=== Building variant: $VARIANT ==="

    VARIANT_BUILD_DIR="${BUILD_ROOT}/${VARIANT}"
    cmake -E remove_directory "$VARIANT_BUILD_DIR"
    if [[ "$OS" == "linux" ]]; then
        # Match the AppDir/usr layout used by the GitHub Actions build. The
        # Linux install hook runs linuxdeployqt against the AppDir and patches
        # the installed executable to load the bundled libraries.
        VARIANT_INSTALL_DIR="${VARIANT_BUILD_DIR}/dist/usr"
    else
        VARIANT_INSTALL_DIR="${VARIANT_BUILD_DIR}/install"
    fi

    ARTIFACTS_NAME="K230BurningTool_${OS}_${TARGET_SUFFIX}_${ARTIFACT_REVISION}"
    if [[ "$OS" == "macos" && -z "$MACOS_SIGN_IDENTITY" ]]; then
        ARTIFACTS_NAME+="_unsigned"
    fi
    case "$OS" in
        windows) ARTIFACT_PATH="${BUILD_ROOT}/${ARTIFACTS_NAME}.zip" ;;
        macos) ARTIFACT_PATH="${BUILD_ROOT}/${ARTIFACTS_NAME}.dmg" ;;
        linux) ARTIFACT_PATH="${BUILD_ROOT}/${ARTIFACTS_NAME}.tar.gz" ;;
    esac
    if [[ -e "$ARTIFACT_PATH" ]]; then
        cmake -E remove "$ARTIFACT_PATH"
    fi
    if [[ -e "${ARTIFACT_PATH}.sha256" ]]; then
        cmake -E remove "${ARTIFACT_PATH}.sha256"
    fi

    if [[ "$OS" == "macos" ]]; then
        CMAKE_EXTRA_ARGS+=(
            "-DK230_BURNING_MACOS_SIGN_IDENTITY=$MACOS_SIGN_IDENTITY"
            "-DK230_BURNING_MACOS_NOTARY_PROFILE=$MACOS_NOTARY_PROFILE"
            "-DK230_BURNING_MACOS_DMG_PATH=$ARTIFACT_PATH"
            "-DCMAKE_OSX_ARCHITECTURES=${MACOS_ARCHITECTURES:-$(uname -m)}"
        )
        if [[ -n "${MACOS_DEPLOYMENT_TARGET:-}" ]]; then
            CMAKE_EXTRA_ARGS+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET")
        fi
    fi

    # Configure
    "$QT_CMAKE" "$REPO_ROOT" -G "$BUILD_GENERATOR" \
        -B "$VARIANT_BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_FOR_AVALON_NANO3="$BUILD_FOR_AVALON_NANO3" \
        -DCMAKE_INSTALL_PREFIX="$VARIANT_INSTALL_DIR" \
        "${CMAKE_EXTRA_ARGS[@]}"

    case "$OS" in
        windows) EXPECTED_INSTALL_HELPER="win-install.cmake" ;;
        macos) EXPECTED_INSTALL_HELPER="mac-install.cmake" ;;
        linux) EXPECTED_INSTALL_HELPER="linux-install.cmake" ;;
    esac
    while IFS= read -r INSTALL_SCRIPT; do
        if grep -F -- "$EXPECTED_INSTALL_HELPER" "$INSTALL_SCRIPT" >/dev/null; then
            INSTALL_HOOK_FOUND=1
            break
        fi
    done < <(find "$VARIANT_BUILD_DIR" -name cmake_install.cmake -type f)
    if [[ "$INSTALL_HOOK_FOUND" != "1" ]]; then
        echo "CMake did not configure the $OS install helper ($EXPECTED_INSTALL_HELPER)." >&2
        echo "Check K230_BURNING_TARGET_OS and the selected Qt toolchain." >&2
        exit 1
    fi

    # Build
    cmake --build "$VARIANT_BUILD_DIR" --parallel

    # Install
    cmake --install "$VARIANT_BUILD_DIR"

    if [[ "$OS" == "linux" ]]; then
        INSTALLED_EXECUTABLE="${VARIANT_INSTALL_DIR}/bin/K230BurningTool"
        if [[ ! -x "$INSTALLED_EXECUTABLE" ]]; then
            echo "Installed executable not found: $INSTALLED_EXECUTABLE" >&2
            exit 1
        fi
        LDD_OUTPUT=$(ldd "$INSTALLED_EXECUTABLE")
        if grep -q 'not found' <<<"$LDD_OUTPUT"; then
            echo "Installed executable has unresolved libraries:" >&2
            printf '%s\n' "$LDD_OUTPUT" >&2
            exit 1
        fi
    fi

    # Package
    case "$OS" in
        windows)
            (cd "$VARIANT_INSTALL_DIR" && zip -r "$ARTIFACT_PATH" .)
            ;;
        macos)
            if [[ ! -f "$ARTIFACT_PATH" ]]; then
                echo "macOS install step did not create $ARTIFACT_PATH" >&2
                exit 1
            fi
            ;;
        linux)
            tar -C "$VARIANT_INSTALL_DIR" -czf "$ARTIFACT_PATH" .
            ;;
        *)
            echo "Unsupported OS: $OS"
            exit 1
            ;;
    esac

    CHECKSUM_PATH="${ARTIFACT_PATH}.sha256"
    (cd "${ARTIFACT_PATH%/*}" && cmake -E sha256sum "${ARTIFACT_PATH##*/}") > "$CHECKSUM_PATH"

    echo "Artifact created: $ARTIFACT_PATH"
    echo "Checksum created: $CHECKSUM_PATH"
}

# Build both variants
build_variant normal
build_variant avalon3

echo "All builds complete."
