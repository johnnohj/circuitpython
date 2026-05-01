WASI_SDK ?= $(HOME)/.local/bin/wasi-sdk-32.0-x86_64-linux
CC = $(WASI_SDK)/bin/clang-22

sync.wasm: sync.c
	$(CC) --target=wasm32 -nostdlib -O2 \
		-Wl,--no-entry -Wl,--export-dynamic \
		-Wl,--import-memory \
		-o $@ $<

clean:
	rm -f sync.wasm

.PHONY: clean
