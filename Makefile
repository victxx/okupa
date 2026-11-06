CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
SANITIZE_FLAGS = -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS += -Icore/include -Ithird_party/tiny_sha3 -Ithird_party/secp256k1/include -Ithird_party/bearssl/inc
BUILD ?= build

CORE = core/src/okupa.c core/src/keccak.c core/src/rlp.c core/src/keys.c core/src/tx.c core/src/act.c core/src/sha256.c core/src/ecdh.c core/src/aes.c core/src/ecies.c core/src/rlpx.c core/src/snappy.c core/src/p2p.c core/src/eth.c third_party/tiny_sha3/sha3.c
TESTS = core keccak rlp secp keys tx act sha256 ecdh aes ecies rlpx snappy p2p eth

# libsecp256k1 is built as upstream ships it, with its own warning level, plus
# the modules we need: recoverable signatures and ECDH (for RLPx).
SECP = third_party/secp256k1
SECP_SRC = $(SECP)/src/secp256k1.c $(SECP)/src/precomputed_ecmult.c $(SECP)/src/precomputed_ecmult_gen.c
SECP_DEFS = -DENABLE_MODULE_RECOVERY=1 -DENABLE_MODULE_ECDH=1
SECP_OBJ = $(patsubst $(SECP)/src/%.c,$(BUILD)/secp/%.o,$(SECP_SRC))

# BearSSL: only the primitives we use, compiled as upstream ships them.
BEAR = third_party/bearssl
BEAR_SRC = $(BEAR)/src/hash/sha2small.c $(BEAR)/src/mac/hmac.c \
  $(BEAR)/src/symcipher/aes_ct.c $(BEAR)/src/symcipher/aes_ct_enc.c $(BEAR)/src/symcipher/aes_ct_cbcenc.c \
  $(BEAR)/src/codec/dec32be.c $(BEAR)/src/codec/enc32be.c $(BEAR)/src/codec/dec32le.c $(BEAR)/src/codec/enc32le.c
BEAR_OBJ = $(patsubst $(BEAR)/src/%.c,$(BUILD)/bearssl/%.o,$(BEAR_SRC))

.PHONY: all test sanitize clean peer
all: $(addprefix $(BUILD)/test-,$(TESTS)) $(BUILD)/okupa-peer

# The host peer: sockets and entropy around the same core.
$(BUILD)/okupa-peer: tools/peer.c $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

peer: $(BUILD)/okupa-peer

$(BUILD)/secp/%.o: $(SECP)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -Wpedantic -Werror -Wextra,$(CFLAGS)) -Wno-unused-function -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) -c $< -o $@

$(BUILD)/bearssl/%.o: $(BEAR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -Wpedantic -Werror -Wextra,$(CFLAGS)) -I$(BEAR)/inc -I$(BEAR)/src -c $< -o $@

$(BUILD)/test-%: tests/test_%.c $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

test: all
	@for t in $(TESTS); do ./$(BUILD)/test-$$t || exit 1; done
	@sh tests/peer_loopback.sh $(BUILD)/okupa-peer

sanitize:
	$(MAKE) test BUILD=build/sanitize CFLAGS='$(SANITIZE_FLAGS)'

clean:
	rm -rf build
