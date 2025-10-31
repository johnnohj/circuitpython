
# Enable ASYNCIFY for cooperative yielding
# mp_js_hook is implemented in C (supervisor/port.c) and calls emscripten_sleep(0)
JSFLAGS += -s ASYNCIFY=1

# Allow Asyncify to work with indirect function calls (needed for Python VM)
JSFLAGS += -s ASYNCIFY_IGNORE_INDIRECT=1

# Add Emscripten's invoke wrappers and emscripten_sleep to ASYNCIFY_IMPORTS
# These are exception handling wrappers that ASYNCIFY needs to handle
# NOTE: mp_js_hook is NOT needed - we call C function directly from VM hook macro
JSFLAGS += -s 'ASYNCIFY_IMPORTS=["invoke_v","invoke_vi","invoke_ii","invoke_iii","invoke_iiii","invoke_iiiii","invoke_vii","invoke_viii","invoke_viiii","emscripten_sleep"]'

# Export Asyncify API to JavaScript (for debugging and feature detection)
EXPORTED_RUNTIME_METHODS_EXTRA +=, Asyncify

# Define EMSCRIPTEN_ASYNCIFY_ENABLED for C compilation
# This tells supervisor/port.c to compile the C implementation
CFLAGS += -DEMSCRIPTEN_ASYNCIFY_ENABLED=1
