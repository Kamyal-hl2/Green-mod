#!/bin/bash
# =============================================================================
# Файл: setup-build-env.sh
# Назначение: Автоматическая установка ВСЕХ инструментов для сборки Green Engine
#
# Что скачивает и устанавливает:
#   1. Android SDK Command-Line Tools (sdkmanager, avdmanager)
#   2. Android NDK r21e (компилятор C/C++ для Android, 32-bit и 64-bit)
#   3. Android Build-Tools 34.0.0 (aapt2, d8, zipalign, apksigner)
#   4. Android Platform SDK 34 (android.jar для компиляции Java)
#   5. Java JDK 17 (нужна для Gradle и javac)
#
# Использование:
#   chmod +x setup-build-env.sh
#   ./setup-build-env.sh
#
# Результат: Папка build-env/ со всеми инструментами (~3 ГБ)
# =============================================================================
set -euo pipefail

BUILD_ENV="$PWD/build-env"
ANDROID_SDK="$BUILD_ENV/android-sdk"
ANDROID_NDK="$BUILD_ENV/android-ndk"
JAVA_HOME_DIR="$BUILD_ENV/jdk-17"

echo "============================================="
echo "  Green Engine v2 — Установка среды сборки"
echo "============================================="
echo ""
echo "Целевая папка: $BUILD_ENV"
echo ""

# Создаём папку для инструментов
mkdir -p "$BUILD_ENV"

# -------------------------------------------------------
# 1. Java JDK 17 (нужна для Gradle, javac, apksigner)
# -------------------------------------------------------
echo "[1/5] Установка Java JDK 17..."
if [ -d "$JAVA_HOME_DIR" ]; then
    echo "  JDK уже установлен, пропускаем"
else
    JDK_URL="https://download.java.net/java/GA/jdk17.0.2/dfd4a8d0985749f896bed50d7138ee7f/8/GPL/openjdk-17.0.2_linux-x64_bin.tar.gz"
    wget -q --show-progress "$JDK_URL" -O "$BUILD_ENV/jdk17.tar.gz"
    tar -xzf "$BUILD_ENV/jdk17.tar.gz" -C "$BUILD_ENV/"
    mv "$BUILD_ENV/jdk-17.0.2" "$JAVA_HOME_DIR"
    rm "$BUILD_ENV/jdk17.tar.gz"
    echo "  JDK 17 установлен: $JAVA_HOME_DIR"
fi
export JAVA_HOME="$JAVA_HOME_DIR"
export PATH="$JAVA_HOME/bin:$PATH"
java -version 2>&1 | head -1

# -------------------------------------------------------
# 2. Android SDK Command-Line Tools
# -------------------------------------------------------
echo ""
echo "[2/5] Установка Android SDK..."
if [ -d "$ANDROID_SDK/cmdline-tools" ]; then
    echo "  SDK уже установлен, пропускаем"
else
    SDK_URL="https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
    wget -q --show-progress "$SDK_URL" -O "$BUILD_ENV/sdk-tools.zip"
    mkdir -p "$ANDROID_SDK/cmdline-tools"
    unzip -q "$BUILD_ENV/sdk-tools.zip" -C "$ANDROID_SDK/cmdline-tools/"
    mv "$ANDROID_SDK/cmdline-tools/cmdline-tools" "$ANDROID_SDK/cmdline-tools/latest"
    rm "$BUILD_ENV/sdk-tools.zip"
    echo "  SDK Tools установлены"
fi

export ANDROID_HOME="$ANDROID_SDK"
export PATH="$ANDROID_SDK/cmdline-tools/latest/bin:$PATH"

# -------------------------------------------------------
# 3. Устанавливаем компоненты SDK через sdkmanager
# -------------------------------------------------------
echo ""
echo "[3/5] Установка компонентов SDK (platform-tools, build-tools, platform)..."
yes | sdkmanager --sdk_root="$ANDROID_SDK" \
    "platform-tools" \
    "build-tools;34.0.0" \
    "platforms;android-34" \
    2>&1 | grep -E "^(Fetch|Install|Done)" || true
echo "  Компоненты SDK установлены"

# -------------------------------------------------------
# 4. Android NDK r21e (совместим с waf build system)
# -------------------------------------------------------
echo ""
echo "[4/5] Установка Android NDK r21e..."
if [ -d "$ANDROID_NDK" ]; then
    echo "  NDK уже установлен, пропускаем"
else
    NDK_URL="https://dl.google.com/android/repository/android-ndk-r21e-linux-x86_64.zip"
    wget -q --show-progress "$NDK_URL" -O "$BUILD_ENV/ndk.zip"
    unzip -q "$BUILD_ENV/ndk.zip" -C "$BUILD_ENV/"
    mv "$BUILD_ENV/android-ndk-r21e" "$ANDROID_NDK"
    rm "$BUILD_ENV/ndk.zip"
    echo "  NDK r21e установлен: $ANDROID_NDK"
fi

# -------------------------------------------------------
# 5. Gradle (встроен в проект через wrapper, но нужен Java)
# -------------------------------------------------------
echo ""
echo "[5/5] Проверка Gradle..."
if [ -f "$PWD/srceng-android/gradlew" ]; then
    echo "  Gradle wrapper уже есть"
else
    echo "  Создаём Gradle wrapper..."
    cd srceng-android
    # Gradle wrapper будет создан при первом запуске
    if command -v gradle &>/dev/null; then
        gradle wrapper --gradle-version 7.6.3
    else
        echo "  gradle не найден, wrapper будет создан вручную"
        mkdir -p gradle/wrapper
        cat > gradle/wrapper/gradle-wrapper.properties << 'EOF'
distributionBase=GRADLE_USER_HOME
distributionPath=wrapper/dists
distributionUrl=https\://services.gradle.org/distributions/gradle-7.6.3-bin.zip
zipStoreBase=GRADLE_USER_HOME
zipStorePath=wrapper/dists
EOF
    fi
    cd ..
fi

# -------------------------------------------------------
# Сохраняем конфигурацию в .env файл
# -------------------------------------------------------
cat > "$BUILD_ENV/env.sh" << EOF
export JAVA_HOME="$JAVA_HOME_DIR"
export ANDROID_HOME="$ANDROID_SDK"
export ANDROID_NDK_HOME="$ANDROID_NDK"
export PATH="\$JAVA_HOME/bin:\$ANDROID_SDK/cmdline-tools/latest/bin:\$ANDROID_SDK/platform-tools:\$PATH"
EOF

echo ""
echo "============================================="
echo "  Установка завершена!"
echo "============================================="
echo ""
echo "Для использования:"
echo "  source build-env/env.sh"
echo ""
echo "Инструменты:"
echo "  Java:     $JAVA_HOME_DIR"
echo "  SDK:      $ANDROID_SDK"
echo "  NDK:      $ANDROID_NDK"
echo "  sdkmanager: $ANDROID_SDK/cmdline-tools/latest/bin/sdkmanager"
echo ""
echo "Следующий шаг:"
echo "  ./build-universal.sh"
