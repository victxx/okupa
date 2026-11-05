CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
SANITIZE_FLAGS = -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS += -Icore/include -Ithird_party/tiny_sha3 -Ithird_party/secp256k1/include
BUILD ?= build

CORE = core/src/okupa.c core/src/keccak.c core/src/rlp.c core/src/keys.c core/src/tx.c core/src/act.c third_party/tiny_sha3/sha3.c
TESTS = core keccak rlp secp keys tx act

# libsecp256k1 is built as upstream ships it, with its own warning level, plus
# the modules we need: recoverable signatures and ECDH (for RLPx).
SECP = third_party/secp256k1
SECP_SRC = $(SECP)/src/secp256k1.c $(SECP)/src/precomputed_ecmult.c $(SECP)/src/precomputed_ecmult_gen.c
SECP_DEFS = -DENABLE_MODULE_RECOVERY=1 -DENABLE_MODULE_ECDH=1
SECP_OBJ = $(patsubst $(SECP)/src/%.c,$(BUILD)/secp/%.o,$(SECP_SRC))

.PHONY: all test sanitize clean
all: $(addprefix $(BUILD)/test-,$(TESTS))

$(BUILD)/secp/%.o: $(SECP)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -Wpedantic -Werror -Wextra,$(CFLAGS)) -Wno-unused-function -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) -c $< -o $@

$(BUILD)/test-%: tests/test_%.c $(CORE) $(SECP_OBJ) $(wildcard core/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) -o $@

test: all
	@for t in $(TESTS); do ./$(BUILD)/test-$$t || exit 1; done

sanitize:
	$(MAKE) test BUILD=build/sanitize CFLAGS='$(SANITIZE_FLAGS)'

clean:
	rm -rf build
