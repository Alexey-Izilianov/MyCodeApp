#!/bin/bash
# То, чего на Astra 1.6 нет или что слишком старо для Qt 6.11, — из исходников:
# m4 и bison (нужны libxkbcommon), Meson 0.56 (последний для Python 3.5) и
# libxkbcommon 1.0 (у Qt 6.11 минимум 0.9, в системе 0.7.1). xkbcommon ставится в $QT_PREFIX:
# там его находит сборка Qt, а при запуске — RUNPATH Qt; в пакет он попадает вместе с Qt.
set -euo pipefail
source "$(dirname "$(readlink -f "$0")")/env.sh"

DEPS=$ROOT/deps
mkdir -p "$DEPS/src" "$QT_PREFIX"
cd "$DEPS/src"

fetch() { # url
    local file
    file=$(basename "$1")
    [ -s "$file" ] || curl -sfL --retry 3 --max-time 300 -o "$file" "$1" \
        || { echo "!! не скачался $1 — положите файл в $DEPS/src вручную"; rm -f "$file"; exit 1; }
    tar -xf "$file"
}

gnu() { # name-version
    [ -x "$DEPS/bin/${1%-*}" ] && return
    fetch "https://ftp.gnu.org/gnu/${1%-*}/$1.tar.xz"
    cd "$1"
    step "$1-configure" ./configure --prefix="$DEPS"
    step "$1-build" make -j"$JOBS"
    step "$1-install" make install
    cd ..
}

command -v m4 > /dev/null || gnu m4-1.4.19
gnu bison-3.8.2

MESON=$DEPS/src/meson-0.56.2/meson.py
[ -f "$MESON" ] || fetch https://github.com/mesonbuild/meson/releases/download/0.56.2/meson-0.56.2.tar.gz

if [ ! -f "$QT_PREFIX/lib/pkgconfig/xkbcommon-x11.pc" ]; then
    fetch https://xkbcommon.org/download/libxkbcommon-1.0.3.tar.xz
    step xkbcommon-configure python3 "$MESON" setup --prefix="$QT_PREFIX" --libdir=lib --buildtype=release \
        -Denable-docs=false -Denable-wayland=false -Denable-x11=true -Denable-xkbregistry=false \
        -Denable-tools=false -Dxkb-config-root=/usr/share/X11/xkb -Dx-locale-root=/usr/share/X11/locale \
        libxkbcommon-1.0.3/build libxkbcommon-1.0.3
    step xkbcommon-build ninja -C libxkbcommon-1.0.3/build
    step xkbcommon-install ninja -C libxkbcommon-1.0.3/build install
fi
# FreeType: Qt 6.11 зовёт функции 2.9+, в 1.6 — 2.6.3. Своя копия с тем же именем
# libfreetype.so.6 подхватывается и системным fontconfig внутри процесса редактора
if [ ! -f "$QT_PREFIX/lib/pkgconfig/freetype2.pc" ]; then
    fetch https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
    cd freetype-2.13.3
    step freetype-configure ./configure --prefix="$QT_PREFIX" --libdir="$QT_PREFIX/lib" --with-zlib=yes \
        --with-bzip2=no --with-png=no --with-harfbuzz=no --with-brotli=no
    step freetype-build make -j"$JOBS"
    step freetype-install make install
    cd ..
fi
echo "== зависимости готовы: $(pkg-config --modversion xkbcommon xkbcommon-x11 freetype2 | tr '\n' ' ')"
