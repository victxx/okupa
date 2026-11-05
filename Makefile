CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
SANITIZE_FLAGS = -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS += -Icore/include -Ithird_party/tiny_sha3
BUILD ?= build

CORE = core/src/okupa.c core/src/keccak.c third_party/tiny_sha3/sha3.c
TESTS = core keccak

.PHONY: all test sanitize clean
all: $(addprefix $(BUILD)/test-,$(TESTS))

$(BUILD)/test-%: tests/test_%.c $(CORE) $(wildcard core/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE) -o $@

test: all
	@for t in $(TESTS); do ./$(BUILD)/test-$$t || exit 1; done

sanitize:
	$(MAKE) test BUILD=build/sanitize CFLAGS='$(SANITIZE_FLAGS)'

clean:
	rm -rf build
