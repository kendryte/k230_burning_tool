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
        linux) extension=zip ;;
        windows) extension=zip ;;
        macos) extension=dmg ;;
    esac
    for arch in x86_64 arm64; do
        # Windows ARM64 is temporarily disabled in the build matrix.
        if [[ "$platform" == windows && "$arch" == arm64 ]]; then
            continue
        fi
        # Avalon release builds are temporarily disabled.
        for variant in normal; do
            packages=(K230BurningTool_"${platform}_${arch}_${variant}"_*."$extension")
            if [[ ${#packages[@]} -ne 1 ]]; then
                echo "Expected exactly one $platform $arch $variant package" >&2
                exit 1
            fi
            verify_package "${packages[0]}"
            if [[ "$platform" != macos ]]; then
                installer_extension=run
                [[ "$platform" != windows ]] || installer_extension=exe
                installers=(K230BurningToolIFW_"${platform}_${arch}_${variant}"_*_setup."$installer_extension")
                repositories=(K230BurningToolIFW_"${platform}_${arch}_${variant}"_*_repository.tar.gz)
                if [[ ${#installers[@]} -ne 1 || ${#repositories[@]} -ne 1 ]]; then
                    echo "Expected exactly one $platform $arch $variant installer and repository" >&2
                    exit 1
                fi
                verify_package "${installers[0]}"
                verify_package "${repositories[0]}"
            fi
        done
    done
done

packages=(*.zip *.tar.gz *.dmg *.run *.exe)
checksums=(*.sha256)
entries=(*)
if [[ ${#packages[@]} -ne 11 || ${#checksums[@]} -ne 11 || ${#entries[@]} -ne 22 ]]; then
    echo "Unexpected extra packages or checksums in release directory" >&2
    exit 1
fi
