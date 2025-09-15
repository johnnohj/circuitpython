# WebAssembly/JavaScript-backed WebAssembly port base manifest
# Note: unix-ffi removed as it's incompatible with WebAssembly compilation

# Essential modules for web-based CircuitPython
require("mip-cmdline")  # Package management
require("ssl")          # Secure connections (Emscripten provides SSL)
