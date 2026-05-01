WASI_SDK ?= $(HOME)/.local/bin/wasi-sdk-32.0-x86_64-linux
CC = $(WASI_SDK)/bin/clang-22

hardware.wasm: hardware.c
	$(CC) --target=wasm32 -nostdlib -O2 \
		-Wl,--no-entry -Wl,--export-dynamic \
		-o $@ $<

clean:
	rm -f hardware.wasm

.PHONY: clean
