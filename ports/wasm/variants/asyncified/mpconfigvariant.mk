
# Enable ASYNCIFY for cooperative yielding
JSFLAGS += -s ASYNCIFY=1

# DON'T ignore indirect calls - Python VM needs full call chain tracking
# Removing ASYNCIFY_IGNORE_INDIRECT allows ASYNCIFY to see the full VM→background→sleep chain

# Add Emscripten's invoke wrappers and emscripten_sleep to ASYNCIFY_IMPORTS
JSFLAGS += -s 'ASYNCIFY_IMPORTS=["invoke_v","invoke_vi","invoke_ii","invoke_iii","invoke_iiii","invoke_iiiii","invoke_vii","invoke_viii","invoke_viiii","emscripten_sleep"]'

# Explicitly whitelist the CircuitPython background task chain
# Without this, ASYNCIFY won't instrument these functions for yielding
JSFLAGS += -s 'ASYNCIFY_ADD=["mp_hal_delay_ms","background_callback_run_all","port_background_task"]'

# Increase stack size for ASYNCIFY (unwinding needs extra space)
JSFLAGS += -s ASYNCIFY_STACK_SIZE=16384

# Export Asyncify API to JavaScript (for debugging and feature detection)
EXPORTED_RUNTIME_METHODS_EXTRA +=, Asyncify

# Define EMSCRIPTEN_ASYNCIFY_ENABLED for C compilation
# This tells supervisor/port.c to compile the C implementation
CFLAGS += -DEMSCRIPTEN_ASYNCIFY_ENABLED=1
