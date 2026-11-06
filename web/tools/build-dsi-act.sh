#!/bin/sh
# Builds web/tools/dsi-act with the console's core. SECP points at an initialised
# third_party/secp256k1 checkout (the pinned submodule).
set -e
cd "$(dirname "$0")/../.."
SECP=${SECP:-third_party/secp256k1}
OUT=build/web
mkdir -p $OUT/secp
for f in secp256k1 precomputed_ecmult precomputed_ecmult_gen; do
  cc -O2 -w -DENABLE_MODULE_RECOVERY=1 -DENABLE_MODULE_ECDH=1 -I$SECP/include -c $SECP/src/$f.c -o $OUT/secp/$f.o
done
cc -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror -Icore/include -Ithird_party/tiny_sha3 -I$SECP/include \
  -o $OUT/dsi-act web/tools/dsi-act.c core/src/okupa.c core/src/keccak.c core/src/rlp.c core/src/keys.c \
  core/src/tx.c core/src/act.c third_party/tiny_sha3/sha3.c $OUT/secp/*.o
echo $OUT/dsi-act
