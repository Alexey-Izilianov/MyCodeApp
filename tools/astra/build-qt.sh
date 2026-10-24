#!/bin/bash
# Qt из исходников в $QT_PREFIX — один раз на машину сборки (около часа на 8 ядрах).
# Нужны исходники модулей в $ROOT/src/<модуль>-everywhere-src-$QT_VERSION.tar.xz
# (download.qt.io/official_releases/qt/6.11/$QT_VERSION/submodules).
# Только то, что нужно редактору: qtbase, qtshadertools, qtdeclarative и Linguist из qttools.
# Готовый модуль помечается qt-build/<модуль>.done — повторный запуск продолжает с прерванного.
set -euo pipefail
source "$(dirname "$(readlink -f "$0")")/env.sh"

unpack() {
    local dir=$ROOT/src/$1-everywhere-src-$QT_VERSION
    if [ ! -d "$dir" ]; then
        tar -xJf "$dir.tar.xz" -C "$ROOT/src" >&2 || { echo "!! нет исходников $dir.tar.xz" >&2; exit 1; }
    fi
    echo "$dir"
}

module() { # имя, команда настройки CMake с флагами
    local name=$1 build=$ROOT/qt-build/$1
    shift
    [ -f "$build.done" ] && { echo "== $name: уже собран"; return; }
    local src
    src=$(unpack "$name")
    step "$name-configure" "$@" -G Ninja -S "$src" -B "$build" -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    step "$name-build" cmake --build "$build" --parallel "$JOBS"
    step "$name-install" cmake --install "$build"
    touch "$build.done"
}

# Библиотеки, которых может не оказаться или которые разных версий на 1.6–1.8, — свои копии
# (zlib, png, jpeg, pcre2, harfbuzz; freetype — из build-deps.sh); fontconfig — системный
# (его требует шрифтовой движок), dbus подгружается при работе. Без OpenSSL: ассистент ходит к модели по http.
module qtbase cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$QT_PREFIX" \
    -DCMAKE_PREFIX_PATH="$DEVROOT/usr" \
    -DFEATURE_xcb=ON -DFEATURE_xcb_xlib=ON -DFEATURE_bundled_xcb_xinput=ON -DFEATURE_xkbcommon=ON \
    -DFEATURE_fontconfig=ON -DFEATURE_system_freetype=ON \
    -DFEATURE_system_zlib=OFF -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF \
    -DFEATURE_system_pcre2=OFF -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_doubleconversion=OFF \
    -DFEATURE_openssl=OFF -DFEATURE_glib=OFF -DFEATURE_icu=OFF -DFEATURE_libudev=OFF \
    -DFEATURE_vulkan=OFF -DFEATURE_eglfs=OFF -DFEATURE_sql=OFF -DFEATURE_printsupport=OFF \
    -DFEATURE_dbus=ON -DFEATURE_dbus_linked=OFF
module qtshadertools "$QT_PREFIX/bin/qt-cmake"
module qtdeclarative "$QT_PREFIX/bin/qt-cmake"
module qttools "$QT_PREFIX/bin/qt-cmake" -DFEATURE_assistant=OFF -DFEATURE_designer=OFF \
    -DFEATURE_distancefieldgenerator=OFF -DFEATURE_pixeltool=OFF -DFEATURE_qtdiag=OFF \
    -DFEATURE_qtplugininfo=OFF -DFEATURE_qdbus=OFF -DFEATURE_clang=OFF -DFEATURE_qdoc=OFF -DFEATURE_linguist=ON

echo "== Qt $QT_VERSION готов: $QT_PREFIX ($(date +%T))"
