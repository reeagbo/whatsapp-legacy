#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME}"
export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
export PATH="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
mkdir -p native-deps
if [ ! -d native-deps/signal ]; then
  tar -xzf core/deps/libsignal-protocol-c_2.3.3.orig.tar.gz -C native-deps
  mv native-deps/libsignal-protocol-c-2.3.3 native-deps/signal
fi
if [ ! -d native-deps/openssl-source ]; then
  git clone --depth 1 --branch openssl-3.0.18 https://github.com/openssl/openssl.git native-deps/openssl-source
fi
# Default to the architecture currently distributed to phones.
# CI explicitly supplies x86_64 as well for its emulator-only build.
abis=("$@")
if [ "${#abis[@]}" -eq 0 ]; then abis=(armeabi-v7a); fi
for abi in "${abis[@]}"; do
  case "$abi" in
    armeabi-v7a|arm64-v8a|x86_64) ;;
    *) echo "Unsupported ABI: $abi" >&2; exit 1 ;;
  esac
done
for abi in "${abis[@]}"; do
  case "$abi" in
    armeabi-v7a) target=android-arm;;
    arm64-v8a) target=android-arm64;;
    x86_64) target=android-x86_64;;
  esac
  prefix="$PWD/native-deps/$abi"
  if [ ! -f "$prefix/lib/libcrypto.a" ]; then
    mkdir -p "native-deps/openssl-$abi"
    (cd "native-deps/openssl-$abi"
      ../openssl-source/Configure "$target" -D__ANDROID_API__=21 no-shared no-tests no-module --prefix="$prefix" --libdir=lib -fPIC
      make -j2 build_libs
      make install_dev)
  fi
done
