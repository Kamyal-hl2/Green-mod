#!/bin/bash
# =============================================================================
# Файл: build-android-aarch64.sh
# Назначение: Сборка нативного движка Source Engine для Android ARM64 (64-bit)
#
# Этот скрипт собирает C++ код движка в .so-библиотеки для arm64-v8a.
# Результатом являются .so-файлы в source-engine/build/ для архитектуры arm64.
#
# Использование:
#   chmod +x build-android-aarch64.sh
#   ./build-android-aarch64.sh
#
# Предварительные требования:
#   source/build-env/env.sh (запустите setup-build-env.sh)
#
# Результат: .so-файлы в source-engine/build/ для arm64-v8a
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/source-engine"

echo "============================================="
echo "  Сборка Source Engine для Android ARM64"
echo "============================================="

# Проверяем что build-env установлен
if [ ! -f "$SCRIPT_DIR/build-env/env.sh" ]; then
    echo "ОШИБКА: build-env не установлен!"
    echo "Сначала запустите: ./setup-build-env.sh"
    exit 1
fi

# Загружаем переменные окружения
source "$SCRIPT_DIR/build-env/env.sh"

# Инициализация git-подмодулей (LuaJIT, SDL2)
echo "[1/4] Инициализация git-подмодулей..."
git submodule init && git submodule update

# Проверяем наличие NDK
if [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "ОШИБКА: Android NDK не найден в $ANDROID_NDK_HOME"
    echo "Запустите: ./setup-build-env.sh"
    exit 1
fi

# Конфигурация для ARM64 (aarch64)
# --android=aarch64,clang,21 — ARM64 с Clang-компилятором, API 21
# Clang используется вместо GCC因为在 NDK r21+ GCC deprecated
echo "[2/4] Конфигурация для ARM64 (aarch64)..."
./waf configure -T release \
    --android=aarch64,clang,21 \
    --togles \
    --disable-warns \
    --build-games=garrysmod 2>&1

# Сборка
echo "[3/4] Сборка движка..."
./waf build -j$(nproc) 2>&1

echo "[4/4] Сборка завершена!"
echo ""
echo "Результаты:"
find build/ -name "*.so" -type f 2>/dev/null | head -20
echo ""
echo "Следующий шаг: ./build-universal.sh"
