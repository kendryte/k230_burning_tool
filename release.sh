#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./release.sh

Builds, installs, validates, and packages the normal variant.

Environment variables:
  K230_BURNING_TARGET_OS       linux, macos, or windows (default: host OS)
  K230_BURNING_TARGET_ARCH     optional artifact architecture label
  K230_BURNING_BUILD_DIR       build and artifact directory (default: ./build)
  K230_BURNING_REVISION        optional artifact revision override
  K230_BURNING_LINUX_DEPLOY_TOOL  linuxdeployqt (default) or linuxdeploy
  K230_IFW_TOOLS_DIR            directory containing Qt IFW binarycreator and repogen
  K230_IFW_REPOSITORY_BASE      optional HTTPS base URL for installer updates
  QT_CMAKE                     path to qt-cmake
  CMAKE_GENERATOR              default: Ninja when available, otherwise Makefiles

macOS release variables:
  MACOS_SIGN_IDENTITY          required Developer ID Application identity
  MACOS_ARCHITECTURES          architecture list (default: current machine)
  MACOS_DEPLOYMENT_TARGET      deployment target (default: 13.0)
  MACOS_NOTARY_PROFILE         optional notarytool keychain profile
  MACOS_KEYCHAIN               optional dedicated signing/notarization keychain
  MACOS_ALLOW_UNSIGNED=1       CI validation only; marks artifacts as unsigned
  MACOS_BUILD_ONLY=1           archive deployed unsigned apps for a signing runner
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
elif [[ "$UNAME" == *"mingw"* || "$UNAME" == *"msys"* || "$UNAME" == *"cygwin"* ||
        "$UNAME" == *"clang"* || "$UNAME" == *"ucrt"* ]]; then
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

LINUX_DEPLOY_TOOL=${K230_BURNING_LINUX_DEPLOY_TOOL:-linuxdeployqt}
if [[ "$OS" == "linux" ]] && ! command -v "$LINUX_DEPLOY_TOOL" >/dev/null 2>&1; then
  echo "$LINUX_DEPLOY_TOOL is required to bundle the Linux Qt libraries." >&2
  exit 1
fi
if [[ "$OS" == "linux" ]]; then
  # Allow AppImage-based deployment tools to run on systems where FUSE is not
  # available (for example, an unprivileged container).
  export APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-1}
fi

QT_CMAKE=${QT_CMAKE:-qt-cmake}
if [[ "$OS" != macos ]]; then
  : "${K230_IFW_TOOLS_DIR:?Set K230_IFW_TOOLS_DIR to the Qt IFW bin directory}"
  command -v python3 >/dev/null
  command -v zip >/dev/null
fi
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
MACOS_KEYCHAIN=${MACOS_KEYCHAIN:-}
if [[ "$OS" == "macos" ]]; then
  MACOS_ARCHITECTURES=${MACOS_ARCHITECTURES:-$(uname -m)}
  MACOS_DEPLOYMENT_TARGET=${MACOS_DEPLOYMENT_TARGET:-13.0}
  MACOS_SIGN_IDENTITY=$(printf '%s' "$MACOS_SIGN_IDENTITY" | tr -d '\r\n')
  MACOS_NOTARY_PROFILE=$(printf '%s' "$MACOS_NOTARY_PROFILE" | tr -d '\r\n')
  MACOS_KEYCHAIN=$(printf '%s' "$MACOS_KEYCHAIN" | tr -d '\r\n')
  export MACOS_SIGN_IDENTITY MACOS_NOTARY_PROFILE MACOS_KEYCHAIN
fi
if [[ "$OS" == "macos" ]]; then
  if [[ "${MACOS_BUILD_ONLY:-0}" == "1" &&
        ( -n "$MACOS_SIGN_IDENTITY" || -n "$MACOS_NOTARY_PROFILE" ) ]]; then
    echo "MACOS_BUILD_ONLY cannot be combined with signing or notarization." >&2
    exit 1
  fi
  if [[ -z "$MACOS_SIGN_IDENTITY" && "${MACOS_ALLOW_UNSIGNED:-0}" != "1" ]]; then
    echo "MACOS_SIGN_IDENTITY is required for a macOS release." >&2
    echo "For CI-only validation, explicitly set MACOS_ALLOW_UNSIGNED=1." >&2
    exit 1
  fi
  if [[ -n "$MACOS_SIGN_IDENTITY" ]] && ! command -v security >/dev/null 2>&1; then
    echo "Apple's security command is required for a macOS release." >&2
    exit 1
  fi
  if [[ -n "$MACOS_SIGN_IDENTITY" ]]; then
    if [[ -n "$MACOS_KEYCHAIN" ]]; then
      identities=$(security find-identity -v -p codesigning "$MACOS_KEYCHAIN")
    else
      identities=$(security find-identity -v -p codesigning)
    fi
    if ! grep -F -- "$MACOS_SIGN_IDENTITY" <<<"$identities" >/dev/null; then
      echo "Signing identity was not found: $MACOS_SIGN_IDENTITY" >&2
      printf '%s\n' "$identities" >&2
      exit 1
    fi
  fi
fi

# Use immutable tag/commit metadata rather than workspace dirtiness in filenames.
REVISION=$(bash "$REPO_ROOT/.github/scripts/release-revision.sh" "$REPO_ROOT")
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
    if [[ -n "${K230_BURNING_TARGET_ARCH:-}" ]]; then
        ARTIFACTS_NAME="K230BurningTool_${OS}_${K230_BURNING_TARGET_ARCH}_${TARGET_SUFFIX}_${ARTIFACT_REVISION}"
    fi
    if [[ "$OS" == "macos" ]]; then
        ARTIFACTS_NAME="K230BurningTool_macos_${MACOS_ARCHITECTURES//;/+}_${TARGET_SUFFIX}_${ARTIFACT_REVISION}"
    fi
    if [[ "$OS" == "macos" && -z "$MACOS_SIGN_IDENTITY" ]]; then
        ARTIFACTS_NAME+="_unsigned"
    fi
    case "$OS" in
        windows) ARTIFACT_PATH="${BUILD_ROOT}/${ARTIFACTS_NAME}.zip" ;;
        macos) ARTIFACT_PATH="${BUILD_ROOT}/${ARTIFACTS_NAME}.dmg" ;;
        linux) ARTIFACT_PATH="${BUILD_ROOT}/${ARTIFACTS_NAME}.zip" ;;
    esac
    if [[ "$OS" == "macos" && "${MACOS_BUILD_ONLY:-0}" == "1" ]]; then
        ARTIFACT_PATH="${BUILD_ROOT}/${ARTIFACTS_NAME}.zip"
    fi
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
            "-DK230_BURNING_MACOS_KEYCHAIN=$MACOS_KEYCHAIN"
            "-DK230_BURNING_MACOS_DMG_PATH=$ARTIFACT_PATH"
            "-DK230_BURNING_MACOS_DEPLOY_ONLY=${MACOS_BUILD_ONLY:-0}"
            "-DCMAKE_OSX_ARCHITECTURES=${MACOS_ARCHITECTURES:-$(uname -m)}"
        )
        if [[ -n "${MACOS_DEPLOYMENT_TARGET:-}" ]]; then
            CMAKE_EXTRA_ARGS+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET")
        fi
    fi
    if [[ "$OS" == "linux" ]]; then
        CMAKE_EXTRA_ARGS+=(
            "-DK230_BURNING_LINUX_DEPLOY_TOOL=$LINUX_DEPLOY_TOOL"
            "-DK230_BURNING_TARGET_ARCH=${K230_BURNING_TARGET_ARCH:-$(uname -m)}"
        )
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
    if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
        cmake --build "$VARIANT_BUILD_DIR" --parallel "$CMAKE_BUILD_PARALLEL_LEVEL"
    else
        cmake --build "$VARIANT_BUILD_DIR" --parallel
    fi

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
        windows|linux)
            (cd "$VARIANT_INSTALL_DIR" && zip -r "$ARTIFACT_PATH" .)
            ;;
        macos)
            if [[ "${MACOS_BUILD_ONLY:-0}" == "1" ]]; then
                ditto -c -k --sequesterRsrc --keepParent "$VARIANT_INSTALL_DIR" "$ARTIFACT_PATH"
            fi
            if [[ ! -f "$ARTIFACT_PATH" ]]; then
                echo "macOS install step did not create $ARTIFACT_PATH" >&2
                exit 1
            fi
            ;;
        *)
            echo "Unsupported OS: $OS"
            exit 1
            ;;
    esac

    CHECKSUM_PATH="${ARTIFACT_PATH}.sha256"
    (cd "${ARTIFACT_PATH%/*}" && cmake -E sha256sum "${ARTIFACT_PATH##*/}" | tr -d '\r') > "$CHECKSUM_PATH"

    echo "Artifact created: $ARTIFACT_PATH"
    echo "Checksum created: $CHECKSUM_PATH"

    if [[ "$OS" != macos ]]; then
        python3 "$REPO_ROOT/packaging/ifw/package.py" \
            --source "$VARIANT_INSTALL_DIR" --output "$BUILD_ROOT/ifw-artifacts" \
            --tools "$K230_IFW_TOOLS_DIR" --platform "$OS" \
            --arch "${K230_BURNING_TARGET_ARCH:-$(uname -m)}" --variant "$TARGET_SUFFIX" \
            --version "$(<"$VARIANT_BUILD_DIR/gui/autogen/version.txt")" --revision "$ARTIFACT_REVISION" \
            --repository-base "${K230_IFW_REPOSITORY_BASE:-}"
    fi
}

# Avalon is disabled; restore CI collection/signing and release verification before re-enabling.
build_variant normal
# build_variant avalon3

echo "All builds complete."
