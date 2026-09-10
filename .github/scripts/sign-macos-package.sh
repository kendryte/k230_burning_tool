#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 5 ]]; then
    echo "Usage: $0 ARCH VARIANT REVISION INPUT_DIR WORK_DIR" >&2
    exit 2
fi
arch=$1
variant=$2
revision=$3
input=$4
work=$5
case "$arch" in x86_64|arm64) ;; *) exit 2 ;; esac
case "$variant" in normal|avalon) ;; *) exit 2 ;; esac
case "$revision" in ''|*[!a-zA-Z0-9._+-]*) exit 2 ;; esac
: "${MACOS_SIGN_IDENTITY:?Signing identity required}"
: "${MACOS_KEYCHAIN:?Signing keychain required}"
: "${MACOS_NOTARY_PROFILE:?Notarization profile required}"
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

package="K230BurningTool_macos_${arch}_${variant}_${revision}"
(cd "$input" && shasum -a 256 -c "${package}_unsigned.zip.sha256")
staging="$work/$arch/$variant"
mkdir -p "$staging" "$work/output"
ditto -x -k "$input/${package}_unsigned.zip" "$staging"
prefix="$staging/install"
app="$prefix/bin/K230BurningTool.app"
lipo "$app/Contents/MacOS/K230BurningTool" -verify_arch "$arch"

# Qt was deployed by the native build runner; do not modify its dependencies here.
dmg="$staging/$package.dmg"
cmake "-DINSTALL_PREFIX=$prefix" -DEXECUTABLE_NAME=K230BurningTool \
    "-DCMAKE_CACHEFILE_DIR=$staging" -DSKIP_DEPLOYMENT=ON \
    "-DSIGN_IDENTITY=$MACOS_SIGN_IDENTITY" "-DKEYCHAIN_PATH=$MACOS_KEYCHAIN" \
    "-DNOTARY_PROFILE=$MACOS_NOTARY_PROFILE" "-DDMG_PATH=$dmg" \
    -P "$repo/gui/mac-install.cmake"
codesign --verify --deep --strict "$app"
codesign --verify --strict "$dmg"
test -f "$dmg"
mv "$dmg" "$work/output/$package.dmg"
(cd "$work/output" && shasum -a 256 "$package.dmg" > "$package.dmg.sha256")
