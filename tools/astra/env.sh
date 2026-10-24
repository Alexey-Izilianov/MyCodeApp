# Общие настройки сборки под Astra Linux: подключается из остальных скриптов.
# Собираем на самой старой поддерживаемой системе (1.6: glibc 2.24, gcc-astra 10) —
# результат запускается и на 1.7 (glibc 2.28), и на 1.8 (glibc 2.36).

ROOT=${ROOT:-/build}
QT_VERSION=6.11.2
QT_PREFIX=$ROOT/qt6
SRC=${SRC:-$ROOT/MyCodeApp}   # исходники редактора: копия рабочей папки или git clone
BUILD=$ROOT/app-build
DEVROOT=$ROOT/devroot   # заголовки X11/xcb/fontconfig из пакетов Debian 9, в систему не ставятся
LOGS=$ROOT/logs
JOBS=${JOBS:-$(nproc)}

export PATH=$ROOT/tools/cmake-3.31.6-linux-x86_64/bin:$ROOT/tools:$ROOT/deps/bin:$PATH
export CC=/usr/lib/gcc-astra/bin/gcc
export CXX=/usr/lib/gcc-astra/bin/g++
# Сначала свои библиотеки (xkbcommon в $QT_PREFIX), затем заголовки devroot; системные .pc не видны
export PKG_CONFIG_LIBDIR=$QT_PREFIX/lib/pkgconfig:$DEVROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$DEVROOT/usr/share/pkgconfig

mkdir -p "$LOGS"

# Шаг с журналом: вывод — в $LOGS/<имя>.log, на экран — только строки прогресса ninja и ошибки
step() {
    local name=$1
    shift
    echo "== $name: $(date +%T)"
    if ! "$@" > "$LOGS/$name.log" 2>&1; then
        echo "!! $name: ошибка, конец журнала $LOGS/$name.log:"
        tail -40 "$LOGS/$name.log"
        exit 1
    fi
}
