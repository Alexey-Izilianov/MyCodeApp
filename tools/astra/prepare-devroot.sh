#!/bin/bash
# Заголовки для сборки Qt без установки пакетов в систему: dev-пакеты Debian 9 (основа
# Astra 1.6) распаковываются в $DEVROOT, их ссылки libX.so ведут на библиотеки системы.
# Репозиторий Astra 1.6 закрыт, поэтому пакеты — из archive.debian.org (stretch).
set -euo pipefail
source "$(dirname "$(readlink -f "$0")")/env.sh"

LIST=$(dirname "$(readlink -f "$0")")/devroot-packages.txt
ARCHIVE=http://archive.debian.org/debian
mkdir -p "$ROOT/debs" "$DEVROOT"
cd "$ROOT/debs"

if [ ! -f Packages ]; then
    curl -sfL "$ARCHIVE/dists/stretch/main/binary-amd64/Packages.gz" | gzip -dc > Packages
fi
for package in $(grep -v '^#' "$LIST"); do
    file=$(awk -v p="$package" '$1=="Package:"{hit=($2==p)} hit&&$1=="Filename:"{print $2; exit}' Packages)
    [ -n "$file" ] || { echo "!! нет пакета $package"; exit 1; }
    [ -s "$(basename "$file")" ] || curl -sfL --retry 3 -o "$(basename "$file")" "$ARCHIVE/$file"
    dpkg-deb -x "$(basename "$file")" "$DEVROOT"
done

# Ссылки libX.so -> libX.so.N.M ведут в никуда: направляем на библиотеку системы
cd "$DEVROOT/usr/lib/x86_64-linux-gnu"
for link in $(find . -maxdepth 1 -name '*.so' -type l); do
    [ -e "$link" ] && continue
    base=$(basename "$(readlink "$link")")
    target=$(ls /usr/lib/x86_64-linux-gnu/"${base%%.so*}".so.[0-9]* /lib/x86_64-linux-gnu/"${base%%.so*}".so.[0-9]* \
             2>/dev/null | head -1 || true) # ls ругается на ту из папок, где библиотеки нет
    [ -n "$target" ] || { echo "!! в системе нет ${base%%.so*}"; exit 1; }
    ln -sf "$target" "$link"
done

# pkg-config: пути из /usr — в devroot
sed -i "s#=/usr#=$DEVROOT/usr#" "$DEVROOT"/usr/lib/x86_64-linux-gnu/pkgconfig/*.pc "$DEVROOT"/usr/share/pkgconfig/*.pc

# xkbcommon 0.7 и FreeType 2.6 из Debian 9 для Qt 6.11 стары: свои собирает build-deps.sh,
# старые заголовки не должны попасться раньше
rm -rf "$DEVROOT"/usr/include/xkbcommon "$DEVROOT"/usr/lib/x86_64-linux-gnu/libxkbcommon* \
       "$DEVROOT"/usr/lib/x86_64-linux-gnu/pkgconfig/xkbcommon* \
       "$DEVROOT"/usr/include/freetype2 "$DEVROOT"/usr/include/ft2build.h \
       "$DEVROOT"/usr/lib/x86_64-linux-gnu/libfreetype* "$DEVROOT"/usr/lib/x86_64-linux-gnu/pkgconfig/freetype2.pc
echo "== devroot готов: $DEVROOT"
