#!/usr/bin/env bash
set -euo pipefail
cd "${1:?Usage: verify-release.sh RELEASE_DIR}"
shopt -s nullglob

verify_package() {
    local digest recorded extra
    read -r digest recorded extra < "$1.sha256"
    recorded="${recorded%$'\r'}"
    if [[ ! "$digest" =~ ^[[:xdigit:]]{64}$ || "${recorded#\*}" != "$1" || -n "$extra" ]]; then
        echo "Checksum manifest does not identify $1" >&2
        return 1
    fi
    if [[ -n "$(sed -n '2,$p' "$1.sha256")" ]]; then
        echo "Expected a single checksum entry for $1" >&2
        return 1
    fi
    printf '%s  %s\n' "$digest" "$1" | sha256sum --check --strict
}

for path in *; do
    if [[ ! -f "$path" || -L "$path" || "$path" == *unsigned* ]]; then
        echo "Unexpected release entry: $path" >&2
        exit 1
    fi
done

for platform in linux windows macos; do
    case "$platform" in
        linux) extension=tar.gz ;;
        windows) extension=zip ;;
        macos) extension=dmg ;;
    esac
    for arch in x86_64 arm64; do
        # Windows ARM64 is temporarily disabled in the build matrix.
        if [[ "$platform" == windows && "$arch" == arm64 ]]; then
            continue
        fi
        for variant in normal avalon; do
            packages=(K230BurningTool_"${platform}_${arch}_${variant}"_*."$extension")
            if [[ ${#packages[@]} -ne 1 ]]; then
                echo "Expected exactly one $platform $arch $variant package" >&2
                exit 1
            fi
            verify_package "${packages[0]}"
            if [[ "$platform" == linux ]]; then
                appimages=(K230BurningTool_linux_"$variant"_*_"$arch".AppImage)
                if [[ ${#appimages[@]} -ne 1 ]]; then
                    echo "Expected exactly one $arch $variant AppImage" >&2
                    exit 1
                fi
                verify_package "${appimages[0]}"
            fi
        done
    done
done

packages=(*.zip *.tar.gz *.dmg *.AppImage)
checksums=(*.sha256)
if [[ ${#packages[@]} -ne 14 || ${#checksums[@]} -ne 14 ]]; then
    echo "Unexpected extra packages or checksums in release directory" >&2
    exit 1
fi
