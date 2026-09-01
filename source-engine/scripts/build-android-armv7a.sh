#!/bin/sh
# =============================================================================
# Файл: build-android-armv7a.sh
# Назначение: Сборка Green Engine v2 для Android (ARMv7a 32-bit)
#
# Этот скрипт:
#   1. Инициализирует git-подмодули (LuaJIT, SDL2)
#   2. Скачивает Android NDK r10e (компилятор C/C++ для Android)
#   3. Запускает waf configure с настройками для Android ARMv7a
#   4. Собирает движок (waf build)
#
# Требования:
#   - Linux x86_64 (для NDK r10e)
#   - Internet (для скачивания NDK)
#   - waf (встроен в дерево исходников)
#
# Результат: Собранные .so-библиотеки в build/ подкаталоге
# =============================================================================
set -euo pipefail

# Инициализация git-подмодулей (SDL2, LuaJIT и т.д.)
git submodule init && git submodule update

# Скачиваем Android NDK r10e (компилятор для Android ARM)
# -q: тихий режим (без вывода прогресса)
wget -q https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip

# Распаковываем NDK
unzip -q android-ndk-r10e-linux-x86_64.zip

# Устанавливаем переменные окружения для NDK
# ANDROID_NDK_HOME используется waf для поиска компилятора
export ANDROID_NDK_HOME="$PWD/android-ndk-r10e/"
export NDK_HOME="$PWD/android-ndk-r10e/"

# Конфигурация и сборка:
# -T debug          — отладочная сборка (с debug-символами)
# --android=...     — целевая платформа: ARMv7a с аппаратным float
#   armeabi-v7a-hard — ARMv7a с hard-float ABI (быстрее на大部分 Android)
#   4.9              — версия GCC из NDK r10e
#   21               — минимальная версия Android API (5.0 Lollipop)
# --togles          — использовать OpenGL ES (для мобильных GPU)
# --disable-warns   — отключить предупреждения компилятора (старый код)
# --build-games=garrysmod — собирать Garry's Mod (а не HL2)
./waf configure -T debug --android=armeabi-v7a-hard,4.9,21 --togles --disable-warns --build-games=garrysmod &&
./waf build
