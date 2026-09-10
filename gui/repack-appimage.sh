#!/usr/bin/env bash
set -euo pipefail

image=$(realpath "${1:?Usage: repack-appimage.sh APPIMAGE [ARCH]}")
arch=${2:-$(uname -m)}
version=v0.7.1
case "$arch" in
    x86_64)
        machine=62
        digest=d27b343d0cf038c0d986774193c704e579b8be9f863f97440ba4c326d03e3d95
        ;;
    arm64|aarch64)
        arch=aarch64
        machine=183
        digest=868d238280cf201aaad114fd12b040a96988ce210c025a0989427adcda982a63
        ;;
    *) echo "Unsupported AppImage architecture: $arch" >&2; exit 1 ;;
esac

# Both supported ELF architectures are little-endian. Reject a mismatched
# launcher before executing it or changing the existing package.
actual_machine=$(od -An -tu2 -j18 -N2 "$image" | tr -d ' ')
if [[ "$actual_machine" != "$machine" ]]; then
    echo "AppImage architecture does not match $arch" >&2
    exit 1
fi
offset=$("$image" --appimage-offset)
size=$(stat -c %s "$image")
if [[ ! "$offset" =~ ^[0-9]+$ ]] || (( offset <= 0 || offset >= size )); then
    echo "Invalid AppImage filesystem offset: $offset" >&2
    exit 1
fi
if [[ $(dd if="$image" bs=1 skip="$offset" count=4 status=none) != hsqs ]]; then
    echo "Expected a SquashFS AppImage: $image" >&2
    exit 1
fi

work=$(mktemp -d "${image}.repack.XXXXXX")
trap 'rm -rf "$work"' EXIT
runtime="$work/runtime"
curl --fail --location --retry 3 --connect-timeout 15 --max-time 180 \
    "https://github.com/VHSgunzo/uruntime/releases/download/$version/uruntime-appimage-squashfs-lite-$arch" \
    --output "$runtime"
printf '%s  %s\n' "$digest" "$runtime" | sha256sum --check --strict

# Upstream's fixed-size configuration field preserves the ELF/image boundary.
# Mode 2 tries FUSE first, then extracts regardless of the AppImage's size.
if ! LC_ALL=C grep -aq 'URUNTIME_EXTRACT=3' "$runtime"; then
    echo "The pinned runtime has an unexpected extraction configuration" >&2
    exit 1
fi
LC_ALL=C sed -i 's/URUNTIME_EXTRACT=3/URUNTIME_EXTRACT=2/g' "$runtime"
chmod +x "$runtime"
runtime_size=$(stat -c %s "$runtime")
if [[ $("$runtime" --appimage-offset) != "$runtime_size" ]]; then
    echo "The pinned runtime has an unexpected filesystem offset" >&2
    exit 1
fi

# Keep the deployer's complete filesystem byte-for-byte, including Qt's
# patched libraries and AppRun. Replace the original only after success.
cp "$runtime" "$work/application.AppImage"
tail -c "+$((offset + 1))" "$image" >> "$work/application.AppImage"
chmod +x "$work/application.AppImage"
mv -f "$work/application.AppImage" "$image"
echo "Packaged $image with automatic no-FUSE fallback ($version, $arch)"
