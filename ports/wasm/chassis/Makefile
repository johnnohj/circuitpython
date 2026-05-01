# chassis/Makefile — Build the port chassis (standalone, no MicroPython)
#
# Usage:
#   make              # build chassis.wasm
#   make test-cli     # run standalone test via Node.js WASI
#   make test-browser # run browser MEMFS test via Node.js
#   make clean

# ── wasi-sdk ──
WASI_SDK ?= $(HOME)/.local/bin/wasi-sdk-32.0-x86_64-linux
WASI_SYSROOT = $(WASI_SDK)/share/wasi-sysroot
CC = $(WASI_SDK)/bin/clang-22

# ── Output ──
BUILD = build
PROG = chassis.wasm

# ── Sources ──
SRC_C = main.c port_memory.c hal.c serial.c budget.c port_step.c

OBJ = $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

# ── Compiler flags ──
CFLAGS  = --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT)
CFLAGS += -std=gnu99 -Wall -Wextra -Wno-unused-parameter
CFLAGS += -I.
CFLAGS += -Os
CFLAGS += -D_WASI_EMULATED_PROCESS_CLOCKS

# ── Linker flags ──
LDFLAGS  = --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT)
LDFLAGS += -lwasi-emulated-process-clocks
LDFLAGS += -Wl,--initial-memory=2097152
LDFLAGS += -Wl,-z,stack-size=65536
# Core exports
LDFLAGS += -Wl,--export=chassis_init
LDFLAGS += -Wl,--export=chassis_frame
LDFLAGS += -Wl,--export=chassis_memory_addr
LDFLAGS += -Wl,--export=chassis_memory_size
# Work exports
LDFLAGS += -Wl,--export=chassis_submit_work
LDFLAGS += -Wl,--export=chassis_work_active
LDFLAGS += -Wl,--export=chassis_work_progress
# HAL exports
LDFLAGS += -Wl,--export=chassis_claim_pin
LDFLAGS += -Wl,--export=chassis_release_pin
LDFLAGS += -Wl,--export=chassis_release_all
LDFLAGS += -Wl,--export=chassis_write_pin
LDFLAGS += -Wl,--export=chassis_read_pin
# VM region exports
LDFLAGS += -Wl,--export=chassis_gc_heap_addr
LDFLAGS += -Wl,--export=chassis_gc_heap_size
LDFLAGS += -Wl,--export=chassis_pystack_addr
LDFLAGS += -Wl,--export=chassis_pystack_size
LDFLAGS += -Wl,--export=chassis_ctx_meta_addr
LDFLAGS += -Wl,--export=chassis_input_buf_addr
LDFLAGS += -Wl,--export=chassis_input_buf_size
LDFLAGS += -Wl,--export=chassis_validate_addr
LDFLAGS += -Wl,--export=chassis_mem_map_count
# Memory exports
LDFLAGS += -Wl,--export=memory
LDFLAGS += -Wl,--export=malloc
LDFLAGS += -Wl,--export=free
# Allow memfs.register import
LDFLAGS += -Wl,--allow-undefined

# ── Rules ──

all: $(BUILD)/$(PROG)

$(BUILD)/$(PROG): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

# ── Tests ──

NODE ?= $(shell which node)

test-cli: $(BUILD)/$(PROG)
	$(NODE) test-chassis.mjs

.PHONY: all clean test-cli
