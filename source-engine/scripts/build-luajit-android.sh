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
# Toolchain naming (NDK r10e, gcc 4.9) matches scripts/build-android-armv7a.sh
# and scripts/waifulib/xcompile.py exactly — arm-linux-androideabi-4.9 /
# aarch64-linux-android-4.9 under toolchains/<...>/prebuilt/linux-x86_64/bin/.
# Don't change these without re-checking xcompile.py's gen_gcc_toolchain_path()
# and ndk_triplet(), the same warning as in .github/workflows/build.yml.
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

case "$WAF_ARCH" in
	arm)
		NDK_TRIPLET="arm-linux-androideabi"
		SYSROOT_ARCH="arm"
		LUAJIT_ARCH_FLAGS=""
		;;
	aarch64)
		NDK_TRIPLET="aarch64-linux-android"
		SYSROOT_ARCH="arm64"
		LUAJIT_ARCH_FLAGS=""
		;;
	*)
		echo "unsupported arch: $WAF_ARCH (expected arm or aarch64)" >&2
		exit 1
		;;
esac

TOOLCHAIN_DIR="$ANDROID_NDK_HOME/toolchains/${NDK_TRIPLET}-4.9/prebuilt/linux-x86_64"
CROSS="$TOOLCHAIN_DIR/bin/${NDK_TRIPLET}-"
SYSROOT="$ANDROID_NDK_HOME/platforms/android-${API}/arch-${SYSROOT_ARCH}"

if [ ! -x "${CROSS}gcc" ]; then
	echo "toolchain not found at ${CROSS}gcc — check ANDROID_NDK_HOME and NDK version (expects r10e)" >&2
	exit 1
fi

LUAJIT_DIR="$(cd "$(dirname "$0")/../thirdparty/luajit" && pwd)"
OUT_DIR="$(cd "$(dirname "$0")/.." && pwd)/lib/android/${WAF_ARCH}"
mkdir -p "$OUT_DIR"

echo "Building LuaJIT for android/${WAF_ARCH} (API ${API}) -> ${OUT_DIR}/libluajit.a"

# LuaJIT needs a host-native minilua/buildvm during its own build (they
# generate bytecode headers used to compile the target lib). HOST_CC is
# left at its Makefile default (plain gcc) — the "-m32" some cross-compile
# examples use is a legacy mingw32/PS3 quirk, not something GitHub's
# ubuntu-latest x86_64 runners have 32-bit libs installed for, and it's not
# needed for an Android target.
# DHAS_ANDROID_JIT enables LuaJIT's Android-specific mmap/mcode handling —
# required, plain "Linux" TARGET_SYS alone mis-detects exec-mmap behavior
# on Android and the JIT compiler silently falls back to interpreter-only.
make -C "$LUAJIT_DIR" clean
make -C "$LUAJIT_DIR" amalg \
	CROSS="$CROSS" \
	TARGET_SYS="Linux" \
	TARGET_FLAGS="--sysroot=${SYSROOT} -DANDROID -D__ANDROID__ -D__ANDROID_API__=${API} -DHAS_ANDROID_JIT ${LUAJIT_ARCH_FLAGS}" \
	BUILDMODE="static"

cp "$LUAJIT_DIR/src/libluajit.a" "$OUT_DIR/libluajit.a"
echo "OK: $OUT_DIR/libluajit.a"
