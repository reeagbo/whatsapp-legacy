#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ ! -f deps/build/src/libsignal-protocol-c.a ]; then
  (cd deps && tar xzf libsignal-protocol-c_2.3.3.orig.tar.gz && cmake -S libsignal-protocol-c-2.3.3 -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF >/dev/null && cmake --build build -j2 >/dev/null)
fi
SIG_SRC=deps/libsignal-protocol-c-2.3.3/src
SIG_LIB=deps/build/src/libsignal-protocol-c.a
FLAGS='-std=c99 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-misleading-indentation'
cc $FLAGS noise.c wire.c ws.c test_ws.c -lcrypto -o test_ws
./test_ws
cc $FLAGS -I"$SIG_SRC" noise.c wire.c ws.c handshake.c test_handshake.c "$SIG_LIB" -lcrypto -o test_handshake
./test_handshake
cc $FLAGS -I"$SIG_SRC" noise.c wire.c ws.c handshake.c test_finish.c "$SIG_LIB" -lcrypto -o test_finish
./test_finish
cc $FLAGS register.c wire.c test_register.c -lcrypto -o test_register
./test_register
cc $FLAGS node.c test_node.c -lz -o test_node
./test_node
cc $FLAGS -I"$SIG_SRC" identity.c crypto_signal.c signal_engine.c inbox.c messaging.c storage_posix.c node.c pairing.c register.c noise.c wire.c ws.c handshake.c test_identity.c "$SIG_LIB" -lcrypto -lz -o test_identity
./test_identity
cc $FLAGS -I"$SIG_SRC" identity.c crypto_signal.c signal_engine.c inbox.c messaging.c storage_posix.c node.c pairing.c register.c noise.c wire.c ws.c handshake.c probe.c "$SIG_LIB" -lssl -lcrypto -lz -o wa_probe
cc $FLAGS -I"$SIG_SRC" identity.c crypto_signal.c signal_engine.c inbox.c messaging.c storage_posix.c node.c register.c noise.c wire.c ws.c handshake.c test_pairing.c "$SIG_LIB" -lcrypto -lz -o test_pairing
./test_pairing

cc $FLAGS -I"$SIG_SRC" identity.c crypto_signal.c signal_engine.c inbox.c storage_posix.c node.c noise.c wire.c test_messaging.c "$SIG_LIB" -lcrypto -lz -o test_messaging
./test_messaging
cc $FLAGS inbox.c test_inbox.c -lcrypto -o test_inbox
./test_inbox
printf 'BUILD OK: run ./wa_probe to test a live connection (network required)\n'
