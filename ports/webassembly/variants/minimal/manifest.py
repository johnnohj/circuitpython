# CircuitPython WebAssembly Bare Variant Manifest
# Minimal but practical - includes asyncio for JS integration

# Include the WebAssembly-specific asyncio implementation
include("$(PORT_DIR)/variants/manifest.py")

# Essential data structures
require("collections")

# Basic utilities that are lightweight
require("copy")

# Note: Includes asyncio for JavaScript Promise integration
# but keeps everything else minimal
