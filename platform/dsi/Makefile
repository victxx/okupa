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
CORE := $(wildcard core/src/*.c) game/src/pet.c game/src/ui.c game/src/r3d.c game/src/monkey.c game/src/house.c game/src/intro.c game/src/icons.c game/src/room_skin.c game/src/font.c game/src/font_sans.c game/src/checks.c game/src/save.c game/src/mind.c game/src/facts.c third_party/qrcodegen/qrcodegen.c \
        third_party/tiny_sha3/sha3.c platform/dsi/main.c platform/dsi/net.c platform/dsi/account.c platform/dsi/audio.c game/src/music.c
SECP_SRC := $(SECP)/src/secp256k1.c $(SECP)/src/precomputed_ecmult.c $(SECP)/src/precomputed_ecmult_gen.c
BEAR_SRC := $(shell find $(BEAR)/src -name '*.c')
CORE_OBJ := $(addprefix $(OUT)/obj/,$(CORE:.c=.o))
LIB_OBJ := $(addprefix $(OUT)/obj/,$(SECP_SRC:.c=.o) $(BEAR_SRC:.c=.o))

ARCH := -mthumb -mcpu=arm946e-s+nofp
SPECS := $(BLOCKSDS)/sys/crts/ds_arm9.specs
# Smaller libsecp256k1 tables for a 4 MB-era machine; same results, a bit slower.
SECP_DEFS := -DENABLE_MODULE_RECOVERY=1 -DENABLE_MODULE_ECDH=1 -DECMULT_WINDOW_SIZE=5 \
             -DCOMB_BLOCKS=2 -DCOMB_TEETH=5 -DUSE_FORCE_WIDEMUL_INT64=1
CPPFLAGS := -Icore/include -Igame/include -Ithird_party/tiny_sha3 -I$(SECP)/include -I$(BEAR)/inc \
            -Ithird_party/font8x8 -Ithird_party/qrcodegen -I$(BLOCKSDS)/libs/libnds/include -I$(BLOCKSDS)/libs/dswifi/include -DOKUPA_BUILD_ID='"$(BUILD_ID)"'
CFLAGS := $(ARCH) -O2 -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
          -fstack-usage -specs=$(SPECS)
LDFLAGS := $(ARCH) -specs=$(SPECS) -Wl,-Map,$(OUT)/okupa.map -Wl,--gc-sections -Wl,--build-id=none \
           -L$(BLOCKSDS)/libs/dswifi/lib -L$(BLOCKSDS)/libs/libnds/lib -Wl,--start-group -ldswifi9 -lnds9 -lc -Wl,--end-group

.PHONY: all
all: $(OUT)/okupa.nds

$(OUT)/okupa.elf: $(CORE_OBJ) $(LIB_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OUT)/okupa.nds: $(OUT)/okupa.elf
	$(BLOCKSDS)/tools/ndstool/ndstool -c $@ -7 $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf -9 $< \
		-b $(BLOCKSDS)/sys/icon.gif "Okupa;A tiny squatter;victxx"

$(OUT)/obj/$(SECP)/%.o: $(SECP)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(SECP)/src $(SECP_DEFS) $(filter-out -Wextra -Werror,$(CFLAGS)) -Wno-unused-function -c -o $@ $<

# Third-party code compiled as upstream ships it, without our -Werror.
$(OUT)/obj/third_party/qrcodegen/%.o: third_party/qrcodegen/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(filter-out -Wextra -Werror,$(CFLAGS)) -c -o $@ $<

$(OUT)/obj/$(BEAR)/%.o: $(BEAR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(BEAR)/src $(filter-out -Wextra -Werror,$(CFLAGS)) -c -o $@ $<

$(OUT)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
