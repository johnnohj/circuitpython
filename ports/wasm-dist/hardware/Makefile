EMCC ?= /home/jef/.local/bin/emsdk/upstream/emscripten/emcc
WASI_SDK ?= $(HOME)/.local/bin/wasi-sdk-32.0-x86_64-linux
WASI_CC = $(WASI_SDK)/bin/clang-22

# Unified Emscripten build: state + SDL2 renderer in one binary
board.js board.wasm: board.c
	$(EMCC) board.c -o board.js -s USE_SDL=2 -s MODULARIZE=1 -s EXPORT_ES6=1 -s 'EXPORTED_FUNCTIONS=["_board_init","_board_add_pin","_board_add_neopixel","_board_init_display","_board_update_gpio","_board_update_neopixel","_board_update_framebuffer","_board_reset_state","_board_render","_board_hit_test","_board_set_hover","_board_set_pressed","_board_get_pin_name","_board_get_pin_category","_malloc","_free"]' -s 'EXPORTED_RUNTIME_METHODS=["UTF8ToString","HEAPU8"]' -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=4194304 -s NO_EXIT_RUNTIME=1 -O2

clean:
	rm -f board.js board.wasm hardware.wasm renderer.js renderer.wasm

.PHONY: clean
