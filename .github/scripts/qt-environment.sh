#!/usr/bin/env bash
set -euo pipefail

: "${RUNNER_TOOL_CACHE:?RUNNER_TOOL_CACHE is required}"
: "${QT_VERSION:?QT_VERSION is required}"

# install-qt-action appends Qt to its dir input.
qt_root="$RUNNER_TOOL_CACHE/qt-$QT_VERSION/Qt/$QT_VERSION/macos"
qt_pending="$RUNNER_TOOL_CACHE/qt-$QT_VERSION/.installation-pending"

qt_is_ready() {
    local tool module version
    for tool in qmake qt-cmake macdeployqt lrelease lupdate; do
        [[ -x "$qt_root/bin/$tool" ]] || return 1
    done
    for module in Qt6 Qt6Core Qt6Widgets Qt6LinguistTools Qt6Network Qt6Svg; do
        [[ -f "$qt_root/lib/cmake/$module/${module}Config.cmake" ]] || return 1
    done
    [[ -f "$qt_root/plugins/platforms/libqcocoa.dylib" ]] || return 1
    version=$("$qt_root/bin/qmake" -query QT_VERSION 2>/dev/null) || return 1
    [[ "$version" == "$QT_VERSION" ]]
}

case "${1:-}" in
    check)
        if [[ ! -e "$qt_pending" ]] && qt_is_ready; then
            echo "available=true" >> "${GITHUB_OUTPUT:?GITHUB_OUTPUT is required}"
            echo "Reusing Qt $QT_VERSION from $qt_root"
        else
            echo "available=false" >> "${GITHUB_OUTPUT:?GITHUB_OUTPUT is required}"
            echo "Qt $QT_VERSION is missing or incomplete; installation is required"
        fi
        ;;
    prepare-install)
        mkdir -p "${qt_pending%/*}"
        touch "$qt_pending"
        ;;
    activate)
        if ! qt_is_ready; then
            echo "Qt validation failed at $qt_root" >&2
            exit 1
        fi
        if [[ -f "$qt_pending" ]]; then
            rm "$qt_pending"
        fi
        echo "$qt_root/bin" >> "${GITHUB_PATH:?GITHUB_PATH is required}"
        {
            echo "QT_ROOT_DIR=$qt_root"
            echo "QT_CMAKE=$qt_root/bin/qt-cmake"
            echo "QT_PLUGIN_PATH=$qt_root/plugins"
            echo "QML2_IMPORT_PATH=$qt_root/qml"
            echo "PKG_CONFIG_PATH=$qt_root/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
        } >> "${GITHUB_ENV:?GITHUB_ENV is required}"
        echo "Activated Qt $QT_VERSION from $qt_root"
        ;;
    *)
        echo "Usage: $0 check|prepare-install|activate" >&2
        exit 2
        ;;
esac
