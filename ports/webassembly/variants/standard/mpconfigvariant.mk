# This is the default variant when you `make` the Unix port.
# CIRCUITPY-CHANGE: Configure for emsdk/WebAssembly build

# Use emcc compiler for WebAssembly output
CC = emcc
PROG = circuitpython.mjs

# Emscripten-specific compile flags (no linker flags here)
CFLAGS += -DEMSCRIPTEN

# Emscripten-specific link flags (matching webassembly port)
LDFLAGS += -s WASM=1
LDFLAGS += -s ASYNCIFY
LDFLAGS += -s EXPORTED_FUNCTIONS="_free,_malloc,_mp_js_init,_mp_js_init_with_heap,_mp_js_post_init,_mp_js_repl_init,_mp_js_repl_process_char,_mp_hal_get_interrupt_char,_mp_js_do_exec,_mp_js_do_exec_async,_mp_js_do_import,_mp_js_register_js_module,_proxy_c_init,_proxy_c_is_initialized,_proxy_c_free_obj,_proxy_c_to_js_call,_proxy_c_to_js_delete_attr,_proxy_c_to_js_dir,_proxy_c_to_js_get_array,_proxy_c_to_js_get_dict,_proxy_c_to_js_get_iter,_proxy_c_to_js_get_type,_proxy_c_to_js_has_attr,_proxy_c_to_js_iternext,_proxy_c_to_js_lookup_attr,_proxy_c_to_js_resume,_proxy_c_to_js_store_attr,_proxy_convert_mp_to_js_obj_cside"
LDFLAGS += -s EXPORTED_RUNTIME_METHODS="ccall,cwrap,FS,getValue,setValue,stringToUTF8,lengthBytesUTF8,allocateUTF8,UTF8ToString"
LDFLAGS += -s ALLOW_MEMORY_GROWTH=1 -s EXIT_RUNTIME=0
# WEBASM-FIX: Increase memory and stack sizes to prevent access errors
LDFLAGS += -s STACK_SIZE=512KB -s INITIAL_MEMORY=16MB
# Add flags for proper stdio handling and exception support
LDFLAGS += -s SUPPORT_LONGJMP=emscripten
LDFLAGS += -s FORCE_FILESYSTEM=1
LDFLAGS += -s NO_DISABLE_EXCEPTION_CATCHING
# LDFLAGS += -s ENVIRONMENT=node -s NODERAWFS=1
LDFLAGS += -s MODULARIZE -s EXPORT_NAME=_createCircuitPythonModule
LDFLAGS += --js-library library.js

# Additional JavaScript files to concatenate (like webassembly port) 
SRC_JS = \
	api.js \
	objpyproxy.js \
	proxy_js.js

# Enable JavaScript FFI module
MICROPY_PY_JSFFI = 1

# Disable native FFI for WebAssembly (conflicts with libffi)
MICROPY_PY_FFI = 0

# Disable threading for WebAssembly
MICROPY_PY_THREAD = 0

# Disable Unix-specific modules for WebAssembly
MICROPY_PY_OS = 0
MICROPY_PY_TERMIOS = 0

FROZEN_MANIFEST ?= $(VARIANT_DIR)/manifest.py

# Override linker flags for WebAssembly compatibility (must be after main Makefile)
override LDFLAGS_ARCH = -Wl,-Map=$@.map -Wl,--gc-sections

# Disable stripping and size for WebAssembly (they don't understand JS/WASM format)
STRIP =
SIZE = true
