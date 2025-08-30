# CircuitPython WebAssembly Bare Variant Manifest
# Minimal but practical - includes asyncio for JS integration

# Include the WebAssembly-specific asyncio implementation
include("$(PORT_DIR)/variants/manifest.py")

# Essential data structures
require("collections")
require("collections-defaultdict")
require("functools")
require("itertools")

# String and text processing
require("base64")       # Common encoding for web data transfer
require("html")         # HTML entity encoding/decoding for web output

# I/O operations compatible with WebAssembly
require("io")           # Stream operations

# Basic utilities that are lightweight
require("copy")

# Note: Includes asyncio for JavaScript Promise integration
# but keeps everything else minimal
