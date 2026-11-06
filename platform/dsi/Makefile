# Okupa ROM, built inside the pinned BlocksDS image (see tools/build_dsi.sh and
# platform/dsi/toolchain.lock.json). Same core sources as the host build.
export BLOCKSDS ?= /opt/wonderful/thirdparty/blocksds/core
export WONDERFUL_TOOLCHAIN ?= /opt/wonderful
PREFIX := $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-
CC := $(PREFIX)gcc
OUT ?= build/dsi
BUILD_ID ?= unknown

SECP := third_party/secp256k1
BEAR := third_party/bearssl
CORE := $(wildcard core/src/*.c) game/src/pet.c game/src/ui.c game/src/font.c game/src/checks.c game/src/save.c \
        third_party/tiny_sha3/sha3.c platform/dsi/main.c
SECP_SRC := $(SECP)/src/secp256k1.c $(SECP)/src/precomputed_ecmult.c $(SECP)/src/precomputed_ecmult_gen.c
BEAR_SRC := $(BEAR)/src/hash/sha2small.c $(BEAR)/src/mac/hmac.c $(BEAR)/src/symcipher/aes_ct.c \
            $(BEAR)/src/symcipher/aes_ct_enc.c $(BEAR)/src/symcipher/aes_ct_cbcenc.c \
            $(BEAR)/src/codec/dec32be.c $(BEAR)/src/codec/enc32be.c $(BEAR)/src/codec/dec32le.c \
            $(BEAR)/src/codec/enc32le.c
CORE_OBJ := $(addprefix $(OUT)/obj/,$(CORE:.c=.o))
LIB_OBJ := $(addprefix $(OUT)/obj/,$(SECP_SRC:.c=.o) $(BEAR_SRC:.c=.o))

ARCH := -mthumb -mcpu=arm946e-s+nofp
SPECS := $(BLOCKSDS)/sys/crts/ds_arm9.specs
# Smaller libsecp256k1 tables for a 4 MB-era machine; same results, a bit slower.
SECP_DEFS := -DENABLE_MODULE_RECOVERY=1 -DENABLE_MODULE_ECDH=1 -DECMULT_WINDOW_SIZE=5 \
             -DCOMB_BLOCKS=2 -DCOMB_TEETH=5 -DUSE_FORCE_WIDEMUL_INT64=1
CPPFLAGS := -Icore/include -Igame/include -Ithird_party/tiny_sha3 -I$(SECP)/include -I$(BEAR)/inc \
            -Ithird_party/font8x8 -I$(BLOCKSDS)/libs/libnds/include -DOKUPA_BUILD_ID='"$(BUILD_ID)"'
CFLAGS := $(ARCH) -O2 -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
          -fstack-usage -specs=$(SPECS)
LDFLAGS := $(ARCH) -specs=$(SPECS) -Wl,-Map,$(OUT)/okupa.map -Wl,--gc-sections -Wl,--build-id=none \
           -L$(BLOCKSDS)/libs/libnds/lib -Wl,--start-group -lnds9 -lc -Wl,--end-group

.PHONY: all
all: $(OUT)/okupa.nds

$(OUT)/okupa.elf: $(CORE_OBJ) $(LIB_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OUT)/okupa.nds: $(OUT)/okupa.elf
	$(BLOCKSDS)/tools/ndstool/ndstool -c $@ -7 $(BLOCKSDS)/sys/arm7/main_core/arm7_minimal.elf -9 $< \
		-b $(BLOCKSDS)/sys/icon.gif "Okupa;A tiny squatter;victxx"

$(OUT)/obj/$(SECP)/%.o: $(SECP)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(SECP)/src $(SECP_DEFS) $(filter-out -Wextra -Werror,$(CFLAGS)) -Wno-unused-function -c -o $@ $<

$(OUT)/obj/$(BEAR)/%.o: $(BEAR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(BEAR)/src $(filter-out -Wextra -Werror,$(CFLAGS)) -c -o $@ $<

$(OUT)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
