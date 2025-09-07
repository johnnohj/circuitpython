include("$(PORT_DIR)/variants/manifest.py")

# CIRCUITPY-CHANGE: Do not include extmod/aysncio

# Core Python data structures and utilities - essential for most Python programs
require("collections")
require("collections-defaultdict")
require("functools")
require("itertools")

# Essential web-compatible modules for data handling
# Note: json is built into CircuitPython core, no need to require it
require("base64")       # Common encoding for web data transfer

# File system and path operations (works in WebAssembly virtual filesystem)
require("os")
require("os-path")
require("pathlib")      # Modern, object-oriented path handling

# String and text processing
require("fnmatch")      # Pattern matching, useful for file operations
require("html")         # HTML entity encoding/decoding for web output

# Development and debugging tools
require("inspect")      # Code introspection, helpful for interactive development

# Data processing and manipulation
require("copy")         # Deep/shallow copying of objects
