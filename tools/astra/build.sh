#!/bin/bash
# Сборка редактора и автотесты против Qt из build-qt.sh.
set -euo pipefail
source "$(dirname "$(readlink -f "$0")")/env.sh"

step app-configure cmake -G Ninja -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX"
step app-build cmake --build "$BUILD" --parallel "$JOBS"
# Без экрана: тесты с окном (editorview) — на offscreen
export QT_QPA_PLATFORM=offscreen
step app-test ctest --test-dir "$BUILD" --output-on-failure
grep -E "tests passed|tests failed" "$LOGS/app-test.log"
echo "== собрано: $BUILD/appMyCodeApp"
