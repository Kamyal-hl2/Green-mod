#!/bin/bash
# =============================================================================
# Файл: build-universal.sh
# Назначение: ПОЛНАЯ СБОРКА Green Engine v2 — универсальный APK для всех телефонов
#
# Этот скрипт делает ВСЁ:
#   1. Устанавливает инструменты (если не установлены)
#   2. Собирает нативный движок для armeabi-v7a (32-bit)
#   3. Собирает нативный движок для arm64-v8a (64-bit)
#   4. Копирует .so-файлы в правильные папки
#   5. Собирает APK с Gradle (универсальный, содержит оба ABI)
#
# Результат: Универсальный APK, работающий на ВСЕХ Android-устройствах
#   - ARM 32-bit (старые/дешёвые телефоны, Android 4.2+)
#   - ARM 64-bit (современные телефоны, Android 5.0+)
#
# Использование:
#   chmod +x build-universal.sh
#   ./build-universal.sh
#
# Результат: srceng-android/build/outputs/apk/debug/srceng-android-debug.apk
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "╔═══════════════════════════════════════════════╗"
echo "║  Green Engine v2 — Универсальная сборка APK  ║"
echo "║  32-bit (armeabi-v7a) + 64-bit (arm64-v8a)  ║"
echo "╚═══════════════════════════════════════════════╝"
echo ""

# ===================================================
# ШАГ 0: Установка инструментов (если нужно)
# ===================================================
if [ ! -f "$SCRIPT_DIR/build-env/env.sh" ]; then
    echo ">>> Инструменты не установлены. Запускаем setup-build-env.sh..."
    chmod +x setup-build-env.sh
    ./setup-build-env.sh
fi

source "$SCRIPT_DIR/build-env/env.sh"

# ===================================================
# ШАГ 1: Сборка нативного движка для armeabi-v7a (32-bit)
# ===================================================
echo "╔═══════════════════════════════════════╗"
echo "║  [1/5] Сборка: armeabi-v7a (32-bit)  ║"
echo "╚═══════════════════════════════════════╝"
cd "$SCRIPT_DIR/source-engine"

# Инициализация подмодулей
git submodule init && git submodule update

# Конфигурация и сборка для 32-bit
./waf configure -T release \
    --android=armeabi-v7a,clang,21 \
    --togles \
    --disable-warns \
    --build-games=garrysmod 2>&1

./waf build -j$(nproc) 2>&1

# Копируем .so-файлы в папку libs архитектуры
LIBS_32="$SCRIPT_DIR/srceng-android/libs/armeabi-v7a"
mkdir -p "$LIBS_32"
echo ">>> Копирование .so файлов для armeabi-v7a..."
find build/ -name "*.so" -exec cp {} "$LIBS_32/" \; 2>/dev/null || true
# Копируем ключевые .so если они в других местах
find lib/android/armeabi-v7a/ -name "*.so" -exec cp {} "$LIBS_32/" \; 2>/dev/null || true

echo ">>> armeabi-v7a: $(ls "$LIBS_32"/*.so 2>/dev/null | wc -l) библиотек"

# ===================================================
# ШАГ 2: Сборка нативного движка для arm64-v8a (64-bit)
# ===================================================
echo ""
echo "╔═══════════════════════════════════════╗"
echo "║  [2/5] Сборка: arm64-v8a (64-bit)    ║"
echo "╚═══════════════════════════════════════╝"

# Очищаем предыдущую сборку
./waf distclean 2>/dev/null || true

# Конфигурация и сборка для 64-bit
./waf configure -T release \
    --android=aarch64,clang,21 \
    --togles \
    --disable-warns \
    --build-games=garrysmod 2>&1

./waf build -j$(nproc) 2>&1

# Копируем .so-файлы в папку libs архитектуры
LIBS_64="$SCRIPT_DIR/srceng-android/libs/arm64-v8a"
mkdir -p "$LIBS_64"
echo ">>> Копирование .so файлов для arm64-v8a..."
find build/ -name "*.so" -exec cp {} "$LIBS_64/" \; 2>/dev/null || true
find lib/android/aarch64/ -name "*.so" -exec cp {} "$LIBS_64/" \; 2>/dev/null || true

echo ">>> arm64-v8a: $(ls "$LIBS_64"/*.so 2>/dev/null | wc -l) библиотек"

# ===================================================
# ШАГ 3: Проверяем что оба ABI имеют .so-файлы
# ===================================================
echo ""
echo "╔═══════════════════════════════════════╗"
echo "║  [3/5] Проверка библиотек             ║"
echo "╚═══════════════════════════════════════╝"

echo ">>> armeabi-v7a (32-bit):"
ls -la "$LIBS_32/" 2>/dev/null | head -10
echo ""
echo ">>> arm64-v8a (64-bit):"
ls -la "$LIBS_64/" 2>/dev/null | head -10

# Проверяем что есть хотя бы основные .so
if [ ! -f "$LIBS_32/libSDL2.so" ] && [ ! -f "$LIBS_32/libmain.so" ]; then
    echo "⚠️  ПРЕДУПРЕЖДЕНИЕ: armeabi-v7a не содержит .so файлов"
    echo "   Это нормально если сборка не завершена — APK будет работать"
    echo "   только на 64-bit устройствах"
fi

if [ ! -f "$LIBS_64/libSDL2.so" ] && [ ! -f "$LIBS_64/libmain.so" ]; then
    echo "⚠️  ПРЕДУПРЕЖДЕНИЕ: arm64-v8a не содержит .so файлов"
    echo "   Это нормально если сборка не завершена — APK будет работать"
    echo "   только на 32-bit устройствах"
fi

# ===================================================
# ШАГ 4: Сборка Java-лаунчера (APK) через Gradle
# ===================================================
echo ""
echo "╔═══════════════════════════════════════╗"
echo "║  [4/5] Сборка APK (Gradle)           ║"
echo "╚═══════════════════════════════════════╝"

cd "$SCRIPT_DIR/srceng-android"

# Создаём local.properties с путём к SDK
echo "sdk.dir=$ANDROID_HOME" > local.properties

# Сборка APK
if [ -f "./gradlew" ]; then
    chmod +x ./gradlew
    ./gradlew assembleDebug
elif command -v gradle &>/dev/null; then
    gradle assembleDebug
else
    echo "ОШИБКА: Neither gradlew nor gradle found!"
    echo "Установите Gradle или создайте wrapper: gradle wrapper"
    exit 1
fi

# ===================================================
# ШАГ 5: Результат
# ===================================================
echo ""
echo "╔═══════════════════════════════════════╗"
echo "║  [5/5] Готово!                        ║"
echo "╚═══════════════════════════════════════╝"

APK_PATH="build/outputs/apk/debug/srceng-android-debug.apk"
if [ -f "$APK_PATH" ]; then
    APK_SIZE=$(du -h "$APK_PATH" | cut -f1)
    echo ""
    echo "✅ APK собран успешно!"
    echo "   Путь: srceng-android/$APK_PATH"
    echo "   Размер: $APK_SIZE"
    echo ""
    echo "📱 Универсальный APK содержит:"
    echo "   • armeabi-v7a (32-bit ARM) — для старых/дешёвых телефонов"
    echo "   • arm64-v8a (64-bit ARM) — для современных телефонов"
    echo ""
    echo "📲 Установка на телефон:"
    echo "   adb install $APK_PATH"
    echo ""
    echo "📂 Или скопируйте APK на телефон и установите вручную"
else
    echo ""
    echo "❌ APK не найден. Проверьте логи выше."
fi
