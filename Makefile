CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
SANITIZE_FLAGS = -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS += -Icore/include -Igame/include -Ithird_party/tiny_sha3 -Ithird_party/secp256k1/include -Ithird_party/bearssl/inc -Ithird_party/font8x8 -Ithird_party/qrcodegen
BUILD ?= build

CORE = core/src/okupa.c core/src/keccak.c core/src/rlp.c core/src/keys.c core/src/tx.c core/src/act.c core/src/sha256.c core/src/ecdh.c core/src/aes.c core/src/ecies.c core/src/rlpx.c core/src/snappy.c core/src/p2p.c core/src/eth.c core/src/enr.c core/src/dnsdisc.c core/src/dns.c core/src/zerog.c core/src/json.c core/src/http.c core/src/session.c core/src/discv4.c core/src/tls.c core/src/tls_anchors.c game/src/pet.c game/src/ui.c game/src/r3d.c game/src/monkey.c game/src/house.c game/src/icons.c game/src/room_skin.c game/src/music.c game/src/font.c game/src/font_sans.c game/src/checks.c game/src/save.c game/src/mind.c third_party/tiny_sha3/sha3.c game/src/facts.c third_party/qrcodegen/qrcodegen.c
TESTS = core keccak rlp secp keys tx act sha256 ecdh aes ecies rlpx snappy p2p eth enr dnsdisc dns zerog json http tls discv4 pet ui checks save mind

# libsecp256k1 is built as upstream ships it, with its own warning level, plus
# the modules we need: recoverable signatures and ECDH (for RLPx).
SECP = third_party/secp256k1
SECP_SRC = $(SECP)/src/secp256k1.c $(SECP)/src/precomputed_ecmult.c $(SECP)/src/precomputed_ecmult_gen.c
SECP_DEFS = -DENABLE_MODULE_RECOVERY=1 -DENABLE_MODULE_ECDH=1
SECP_OBJ = $(patsubst $(SECP)/src/%.c,$(BUILD)/secp/%.o,$(SECP_SRC))

# BearSSL: only the primitives we use, compiled as upstream ships them.
BEAR = third_party/bearssl
# All of BearSSL: TLS pulls in most of it; the linker keeps only what is used.
BEAR_SRC = $(shell find $(BEAR)/src -name '*.c')
BEAR_OBJ = $(patsubst $(BEAR)/src/%.c,$(BUILD)/bearssl/%.o,$(BEAR_SRC))

.PHONY: all test sanitize clean peer preview
# Keep library objects between builds.
.SECONDARY:
all: $(addprefix $(BUILD)/test-,$(TESTS)) $(BUILD)/okupa-peer $(BUILD)/okupa-preview $(BUILD)/okupa-directions $(BUILD)/okupa-monkey $(BUILD)/okupa-house $(BUILD)/okupa-music $(BUILD)/okupa-dnswalk $(BUILD)/okupa-https $(BUILD)/okupa-crawl

# The host peer: sockets and entropy around the same core.
$(BUILD)/okupa-peer: tools/peer.c $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h game/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

peer: $(BUILD)/okupa-peer

# Discovery v4 crawl with the core.
$(BUILD)/okupa-crawl: tools/crawl.c $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

# HTTPS with the core's TLS and HTTP.
$(BUILD)/okupa-https: tools/https.c $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

# Finds nodes in the EIP-1459 DNS tree with the core alone.
$(BUILD)/okupa-dnswalk: tools/dnswalk.c $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

# Host renders of the DSi screens.
$(BUILD)/okupa-preview: tools/preview.c tools/png.h $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h game/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

# The looks to choose from, with the same renderer.
$(BUILD)/okupa-directions: tools/directions.c tools/png.h $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h game/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

# Sheets of the okupa: its unfolded skin, what it wears, its moods.
$(BUILD)/okupa-monkey: tools/monkey.c tools/png.h $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h game/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

# The attic through the day; `make film` also writes the frames of a short film.
$(BUILD)/okupa-house: tools/house.c tools/png.h $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h game/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

# The music and sounds as a WAV.
$(BUILD)/okupa-music: tools/music.c $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h game/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

preview: $(BUILD)/okupa-preview $(BUILD)/okupa-directions $(BUILD)/okupa-monkey $(BUILD)/okupa-house $(BUILD)/okupa-music
	@mkdir -p build/preview
	./$(BUILD)/okupa-preview build/preview
	./$(BUILD)/okupa-directions build/preview
	./$(BUILD)/okupa-monkey build/preview
	./$(BUILD)/okupa-house build/preview
	./$(BUILD)/okupa-music build/preview/okupa-music.wav

$(BUILD)/secp/%.o: $(SECP)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -Wpedantic -Werror -Wextra,$(CFLAGS)) -Wno-unused-function -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) -c $< -o $@

$(BUILD)/bearssl/%.o: $(BEAR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -Wpedantic -Werror -Wextra,$(CFLAGS)) -I$(BEAR)/inc -I$(BEAR)/src -c $< -o $@

$(BUILD)/test-%: tests/test_%.c $(CORE) $(SECP_OBJ) $(BEAR_OBJ) $(wildcard core/include/*.h game/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) $(SECP_OBJ) $(BEAR_OBJ) -o $@

test: all
	@for t in $(TESTS); do ./$(BUILD)/test-$$t || exit 1; done
	@sh tests/peer_loopback.sh $(BUILD)/okupa-peer

sanitize:
	$(MAKE) -j8 test BUILD=build/sanitize CFLAGS='$(SANITIZE_FLAGS)'

clean:
	rm -rf build
