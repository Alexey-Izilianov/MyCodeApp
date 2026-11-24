#!/bin/bash
# Пакет mycodeapp_<версия>_amd64.deb для Debian/Ubuntu-совместимых систем:
# Ubuntu 22.04+, Debian 11+, Astra Linux 1.8 (glibc 2.35 у раннера ubuntu-22.04).
# Astra Linux 1.7 (glibc 2.28) этим пакетом не покрывается — для неё сборка
# в лаборатории: tools/astra/package.sh (на самой Astra).
#
# Раскладка как в Astra-пакете: /opt/mycodeapp (программа, Qt, плагины),
# /usr/bin/mycodeapp, ярлык в меню, значки. Модель кладётся, если найдена.
# Без sudo: владелец файлов в пакете — root (dpkg-deb --root-owner-group).
#
#   ./tools/package-deb.sh [BUILD=build] [OUT=dist]
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=${SRC:-$(cd "$HERE/.." && pwd)}
BUILD=${BUILD:-$SRC/build}
OUT=${OUT:-$SRC/dist}
VERSION=${VERSION:-$(sed -n 's/^project(MyCodeApp VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")}
ARCH=${ARCH:-amd64}

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
app=$stage/opt/mycodeapp

cmake --install "$BUILD" --prefix "$app"
# Заголовки и статические библиотеки FetchContent-зависимостей в пакете не нужны
rm -rf "$app/include" "$app/lib/cmake" "$app/lib/pkgconfig" "$app"/lib/*.a

# Модель ассистента (GGUF), если есть рядом со сборкой или исходниками
shopt -s nullglob
for candidate in "$BUILD/models/"*.gguf "${SRC%/}/../models/"*.gguf; do
    [ -s "$candidate" ] || continue
    mkdir -p "$app/models"
    cp "$candidate" "$app/models/Qwen3-4B-Instruct-2507-Q4_K_M.gguf"
    break
done
shopt -u nullglob

install -Dm644 "$SRC/assets/icon/app-256.png" "$stage/usr/share/icons/hicolor/256x256/apps/mycodeapp.png"
install -Dm644 "$SRC/assets/icon/app.svg" "$stage/usr/share/icons/hicolor/scalable/apps/mycodeapp.svg"
install -Dm644 "$HERE/astra/mycodeapp.desktop" "$stage/usr/share/applications/mycodeapp.desktop"
mkdir -p "$stage/usr/bin"
ln -s /opt/mycodeapp/bin/appMyCodeApp "$stage/usr/bin/mycodeapp"

# Зависимости: библиотеки, нужные файлам пакета и не лежащие в нём, -> пакеты системы.
# Имена, разные в Debian 11 и 12, — с альтернативой (как в tools/astra/package.sh).
declare -A renamed=(
    [libgl1-mesa-glx]="libgl1 | libgl1-mesa-glx" [libgl1]="libgl1 | libgl1-mesa-glx"
    [libegl1-mesa]="libegl1 | libegl1-mesa" [libegl1]="libegl1 | libegl1-mesa"
    [libxcb-util0]="libxcb-util1 | libxcb-util0" [libxcb-util1]="libxcb-util1 | libxcb-util0"
)
export LC_ALL=C
mapfile -d '' elves < <(find "$app" -type f -exec sh -c 'head -c4 "$1" | grep -q ELF' _ {} \; -print0)
glibc_min=$(readelf --version-info "${elves[@]}" 2>/dev/null |
    sed -n 's/.*Name: GLIBC_\([0-9][0-9.]*\) .*/\1/p' | sort -V | tail -1)
if [ -n "$glibc_min" ]; then
    renamed[libc6]="libc6 (>= $glibc_min)"
fi

needed=$(readelf -d "${elves[@]}" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort -u)
libraries=$(/sbin/ldconfig -p)
packages=()
for lib in $needed; do
    [ -e "$app/lib/$lib" ] && continue
    path=$(awk -v l="$lib" '$1==l && /x86-64/ {print $NF; exit}' <<< "$libraries")
    if [ -z "$path" ]; then
        echo "!! в системе нет $lib — пакет без зависимости соберётся неверно" >&2
        exit 1
    fi
    owner=$(dpkg -S "$path" 2>/dev/null || dpkg -S "$(readlink -f "$path")")
    packages+=("${owner%%:*}")
done
[ ${#packages[@]} -gt 0 ] || { echo "!! не найдено системных зависимостей — проверьте readelf" >&2; exit 1; }
depends=$(printf '%s\n' "${packages[@]}" | sort -u | while read -r p; do echo "${renamed[$p]:-$p}"; done | paste -sd, | sed 's/,/, /g')

mkdir -p "$stage/DEBIAN"
cat > "$stage/DEBIAN/control" <<EOF
Package: mycodeapp
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
Maintainer: Изилянов Алексей Дмитриевич
Installed-Size: $(du -sk "$stage" | cut -f1)
Depends: $depends
Description: MyCodeApp — редактор исходного кода
 Редактор кода на Qt: файлы до 1 ГБ, подсветка tree-sitter, языковые серверы,
 Git и Mercurial, офлайн-ассистент на локальной модели.
EOF
cat > "$stage/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
command -v update-desktop-database > /dev/null && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache > /dev/null && gtk-update-icon-cache -q /usr/share/icons/hicolor || true
EOF
cp "$stage/DEBIAN/postinst" "$stage/DEBIAN/postrm"
chmod 755 "$stage/DEBIAN/postinst" "$stage/DEBIAN/postrm"

mkdir -p "$OUT"
deb=$OUT/mycodeapp_${VERSION}_${ARCH}.deb
dpkg-deb --root-owner-group -Zxz -b "$stage" "$deb" >/dev/null
echo "== пакет: $deb ($(du -h "$deb" | cut -f1))"
echo "   Depends: $depends"
