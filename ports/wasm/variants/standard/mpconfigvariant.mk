# CircuitPython WebAssembly Standard Variant Makefile
# MicroPython base + CircuitPython APIs + HAL providers

# Use standard MicroPython settings with CircuitPython additions

# JavaScript files for API
SRC_JS += \
    api.js \
    objpyproxy.js \
    proxy_js.js \

# No additional frozen modules for now
FROZEN_MANIFEST ?=

# Export program name
PROG = circuitpython.mjs
