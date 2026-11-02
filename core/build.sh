#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ ! -f deps/build/src/libsignal-protocol-c.a ]; then
  (cd deps && tar xzf libsignal-protocol-c_2.3.3.orig.tar.gz && cmake -S libsignal-protocol-c-2.3.3 -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF >/dev/null && cmake --build build -j2 >/dev/null)
fi
SIG_SRC=deps/libsignal-protocol-c-2.3.3/src
SIG_LIB=deps/build/src/libsignal-protocol-c.a
FLAGS='-std=c99 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations'
cc $FLAGS noise.c wire.c ws.c test_ws.c -lcrypto -o test_ws
./test_ws
cc $FLAGS -I"$SIG_SRC" noise.c wire.c ws.c handshake.c test_handshake.c "$SIG_LIB" -lcrypto -o test_handshake
./test_handshake
cc $FLAGS -I"$SIG_SRC" noise.c wire.c ws.c handshake.c test_finish.c "$SIG_LIB" -lcrypto -o test_finish
./test_finish
cc $FLAGS register.c wire.c test_register.c -lcrypto -o test_register
./test_register
cc $FLAGS -I"$SIG_SRC" noise.c wire.c ws.c handshake.c probe.c "$SIG_LIB" -lssl -lcrypto -o wa_probe
printf 'BUILD OK: run ./wa_probe to test a live connection (network required)\n'
