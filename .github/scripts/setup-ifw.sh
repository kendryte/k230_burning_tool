#!/usr/bin/env bash
set -euo pipefail

platform=${1:?Usage: setup-ifw.sh PLATFORM ARCH DESTINATION}
arch=${2:?Missing architecture}
destination=${3:?Missing destination}
case "$platform/$arch" in
    linux/x86_64)
        host=linux_x64 archive=ifw-linux-x64.7z
        digest=79ef906970c7e4be9ec15447fd5acb0a8ae2ab41aef5f9184086ce3972eddbc7 ;;
    linux/arm64)
        host=linux_arm64 archive=ifw-linux-arm64.7z
        digest=6d02441a14bc14fea5555690b63a5d8d6ef262d7ef8eb1a009d1a541b2ef0adb ;;
    windows/x86_64)
        host=windows_x86 archive=ifw-win-x64.7z
        digest=c47201c4f6a82a8b607daa245237f40831d78425e904edd1514b71fd17efefc1 ;;
    *) echo "No pinned Qt IFW tools for $platform/$arch" >&2; exit 1 ;;
esac

mkdir -p "$(dirname "$destination")"
mkdir "$destination"
url="https://download.qt.io/online/qtsdkrepository/$host/ifw/tools_ifw_411/qt.tools.ifw.411/4.11.0-0-202603231357$archive"
curl --fail --location --retry 3 --connect-timeout 20 --max-time 600 \
    "$url" --output "$destination/tools.7z"
printf '%s  %s\n' "$digest" "$destination/tools.7z" | sha256sum --check --strict
"${SEVENZIP:-7z}" x -y "-o$destination" "$destination/tools.7z" >/dev/null
extension=
[[ "$platform" != windows ]] || extension=.exe
for tool in binarycreator repogen installerbase; do
    test -f "$destination/bin/$tool$extension"
    chmod +x "$destination/bin/$tool$extension"
done
echo "Qt IFW 4.11.0 installed in $destination"
