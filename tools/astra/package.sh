#!/bin/bash
# Пакет mycodeapp_<версия>_amd64.deb для Astra Linux 1.6–1.8: /opt/mycodeapp (программа, Qt,
# libstdc++ компилятора, свой xkbcommon), ярлык в меню, значок, команда mycodeapp.
# Зависимости — системные библиотеки X11/GL/шрифтов, которых нет в пакете.
set -euo pipefail
HERE=$(dirname "$(readlink -f "$0")")
source "$HERE/env.sh"

VERSION=${VERSION:-$(sed -n 's/^project(MyCodeApp VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")}
STAGE=$ROOT/package/mycodeapp_${VERSION}_amd64
DEB=$STAGE.deb
APP=$STAGE/opt/mycodeapp

[ -d "$STAGE" ] && sudo rm -rf "$STAGE" # прошлая сборка пакета принадлежит root
step app-install cmake --install "$BUILD" --prefix "$APP"
# libgit2 и прочие зависимости FetchContent ставят заголовки и статические библиотеки — в пакете не нужны
rm -rf "$APP/include" "$APP/lib/cmake" "$APP/lib/pkgconfig" "$APP"/lib/*.a
# Компилятор новее системного: его libstdc++ и libgcc едут с программой
cp -L "$($CXX -print-file-name=libstdc++.so.6)" "$($CXX -print-file-name=libgcc_s.so.1)" "$APP/lib/"

install -Dm644 "$SRC/assets/icon/app-256.png" "$STAGE/usr/share/icons/hicolor/256x256/apps/mycodeapp.png"
install -Dm644 "$SRC/assets/icon/app.svg" "$STAGE/usr/share/icons/hicolor/scalable/apps/mycodeapp.svg"
install -Dm644 "$HERE/mycodeapp.desktop" "$STAGE/usr/share/applications/mycodeapp.desktop"
mkdir -p "$STAGE/usr/bin"
ln -s /opt/mycodeapp/bin/appMyCodeApp "$STAGE/usr/bin/mycodeapp"

# Зависимости: библиотеки, нужные файлам пакета и не лежащие в нём, -> пакеты этой системы.
# Имена, разные в Debian 9 и 12 (основа Astra 1.6 и 1.8), — с альтернативой
declare -A renamed=(
    [libgl1-mesa-glx]="libgl1 | libgl1-mesa-glx" [libgl1]="libgl1 | libgl1-mesa-glx"
    [libegl1-mesa]="libegl1 | libegl1-mesa" [libegl1]="libegl1 | libegl1-mesa"
    [libxcb-util0]="libxcb-util1 | libxcb-util0" [libxcb-util1]="libxcb-util1 | libxcb-util0"
    [libc6]="libc6 (>= 2.24)"
)
export LC_ALL=C # readelf и dpkg в русской локали пишут не «Shared library»
mapfile -d '' elves < <(find "$APP" -type f -exec sh -c 'head -c4 "$1" | grep -q ELF' _ {} \; -print0)
needed=$(readelf -d "${elves[@]}" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort -u)
libraries=$(/sbin/ldconfig -p) # один раз: awk с exit в конвейере обрывал ldconfig (SIGPIPE + pipefail)
packages=()
for lib in $needed; do
    [ -e "$APP/lib/$lib" ] && continue
    path=$(awk -v l="$lib" '$1==l && /x86-64/ {print $NF; exit}' <<< "$libraries")
    [ -n "$path" ] || { echo "!! в системе нет $lib"; exit 1; }
    owner=$(dpkg -S "$path" 2>/dev/null || dpkg -S "$(readlink -f "$path")")
    packages+=("${owner%%:*}")
done
[ ${#packages[@]} -gt 0 ] || { echo "!! не найдено ни одной системной зависимости — проверьте readelf"; exit 1; }
depends=$(printf '%s\n' "${packages[@]}" | sort -u | while read -r p; do echo "${renamed[$p]:-$p}"; done | paste -sd, | sed 's/,/, /g')

# ЗПС (замкнутая программная среда): с SIGN_KEY=<id ключа GPG> все ELF пакета подписываются
# bsign. Ключ — без пароля или с паролем в gpg-agent; открытый ключ администратор кладёт
# в /etc/digsig/keys (см. docs/operator-guide.md). Без SIGN_KEY пакет не подписан
if [ -n "${SIGN_KEY:-}" ]; then
    printf '%s\n' "${elves[@]}" > "$LOGS/sign-files.txt"
    step sign bsign -N -s -E -f "$LOGS/sign-files.txt" --pgoptions="--batch --default-key=$SIGN_KEY"
    # bsign -V сверяет только с ключами, загруженными в систему, — здесь лишь наличие подписи
    for elf in "${elves[@]}"; do # без конвейера в grep -q: SIGPIPE + pipefail дают ложный провал
        [[ $(bsign -w "$elf" 2>/dev/null) == *$'\nsignature:'* ]] || { echo "!! не подписан: $elf"; exit 1; }
    done
    echo "== подписано ключом $SIGN_KEY: ${#elves[@]} файлов"
fi

mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: mycodeapp
Version: $VERSION
Section: devel
Priority: optional
Architecture: amd64
Maintainer: Изилянов Алексей Дмитриевич
Installed-Size: $(du -sk "$STAGE" | cut -f1)
Depends: $depends
Description: MyCodeApp — редактор исходного кода
 Редактор кода на Qt: файлы до 1 ГБ, подсветка tree-sitter, языковые серверы,
 Git и Mercurial, офлайн-ассистент на локальной модели.
EOF
cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
command -v update-desktop-database > /dev/null && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache > /dev/null && gtk-update-icon-cache -q /usr/share/icons/hicolor || true
EOF
cp "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

# dpkg-deb на 1.6 не умеет --root-owner-group: файлы пакета отдаём root перед упаковкой
sudo chown -R root:root "$STAGE"
step package dpkg-deb -Zxz -b "$STAGE" "$DEB"
echo "== пакет: $DEB ($(du -h "$DEB" | cut -f1))"
echo "   Depends: $depends"
