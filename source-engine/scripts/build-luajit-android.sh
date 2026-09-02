#!/bin/sh
# Green Engine v2 — cross-compile LuaJIT for Android.
#
# Produces lib/android/<lib_arch>/libluajit.a, matching this repo's own
# convention for every other third-party static lib (libjpeg.a, libpng.a,
# libcurl.a, etc. all live under lib/android/<arch>/ as prebuilt archives —
# see conf.check(lib=...) calls in the top-level wscript). waf itself never
# compiles these from source; it just links the prebuilt .a. This script is
# what produces that .a for LuaJIT specifically, run once in CI before
# `./waf configure` / `./waf build`.
#
# Toolchain: NDK r21e Clang (toolchains/llvm/prebuilt/linux-x86_64/bin/).
# LuaJIT's Makefile supports CC/TARGET_LD/TARGET_AR overrides for cross-
# compilation without a traditional CROSS prefix.
#
# Usage: build-luajit-android.sh <arm|aarch64> <api-level>
# Requires ANDROID_NDK_HOME set (as the rest of the build already does).

set -e

WAF_ARCH="$1"   # "arm" or "aarch64" — matches lib/android/<arch>/ naming
API="$2"        # e.g. 21

if [ -z "$WAF_ARCH" ] || [ -z "$API" ]; then
	echo "usage: $0 <arm|aarch64> <api-level>" >&2
	exit 1
fi

if [ -z "$ANDROID_NDK_HOME" ]; then
	echo "ANDROID_NDK_HOME must be set" >&2
	exit 1
fi

NDK_TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64"

case "$WAF_ARCH" in
	arm)
		CLANG_TRIPLET="armv7a-linux-androideabi"
		LJARCH_FLAGS="-march=armv7-a"
		;;
	aarch64)
		CLANG_TRIPLET="aarch64-linux-android"
		LJARCH_FLAGS=""
		;;
	*)
		echo "unsupported arch: $WAF_ARCH (expected arm or aarch64)" >&2
		exit 1
		;;
esac

CLANG_CC="${NDK_TOOLCHAIN}/bin/${CLANG_TRIPLET}${API}-clang"

if [ ! -x "${CLANG_CC}" ]; then
	echo "Clang not found at ${CLANG_CC} — check ANDROID_NDK_HOME and NDK version (expects r21e)" >&2
	exit 1
fi

LUAJIT_DIR="$(cd "$(dirname "$0")/../thirdparty/luajit" && pwd)"
OUT_DIR="$(cd "$(dirname "$0")/.." && pwd)/lib/android/${WAF_ARCH}"
mkdir -p "$OUT_DIR"

echo "Building LuaJIT for android/${WAF_ARCH} (API ${API}) -> ${OUT_DIR}/libluajit.a"
echo "  CC=${CLANG_CC}"
echo "  AR=${NDK_TOOLCHAIN}/bin/llvm-ar"

make -C "$LUAJIT_DIR" clean
make -C "$LUAJIT_DIR" amalg \
	HOST_CC="gcc -m32" \
	CC="${CLANG_CC}" \
	TARGET_LD="${CLANG_CC}" \
	TARGET_AR="${NDK_TOOLCHAIN}/bin/llvm-ar rcus" \
	TARGET_STRIP="${NDK_TOOLCHAIN}/bin/llvm-strip" \
	TARGET_SYS="Linux" \
	TARGET_FLAGS="-DANDROID -D__ANDROID__ -D__ANDROID_API__=${API} -DHAS_ANDROID_JIT ${LJARCH_FLAGS}" \
	BUILDMODE="static"

cp "$LUAJIT_DIR/src/libluajit.a" "$OUT_DIR/libluajit.a"
echo "OK: $OUT_DIR/libluajit.a"
