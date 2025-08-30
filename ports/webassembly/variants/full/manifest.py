# CircuitPython WebAssembly Full Variant
# Comprehensive feature set with all confirmed available modules

include("$(PORT_DIR)/variants/manifest.py")

# Core Python data structures and utilities - essential for most Python programs
require("collections")
require("collections-defaultdict")
require("functools") 
require("itertools")

# Essential web-compatible modules for data handling
# Note: json is built into CircuitPython core, no need to require it
require("base64")       # Common encoding for web data transfer
require("binascii")     # Binary/ASCII utilities

# File system and path operations (works in WebAssembly virtual filesystem)
require("os")
require("os-path")
require("pathlib")      # Modern, object-oriented path handling

# String and text processing
require("fnmatch")      # Pattern matching, useful for file operations
require("html")         # HTML entity encoding/decoding for web output
require("textwrap")     # Text wrapping and formatting

# Development and debugging tools
require("inspect")      # Code introspection, helpful for interactive development
require("logging")      # Structured logging system

# I/O operations compatible with WebAssembly
require("io")           # Stream operations

# Data processing and manipulation
require("copy")         # Deep/shallow copying of objects
require("operator")     # Functional operator interface
require("contextlib")   # Context management utilities

# Advanced data structures and algorithms (confirmed available)
require("heapq")        # Priority queue algorithm
require("bisect")       # Binary search and insertion

# Security utilities (confirmed available)
require("hmac")         # HMAC message authentication

# Date/time handling (heavy but available)
require("datetime")     # Comprehensive date/time operations

# Development frameworks (heavy but comprehensive)
require("unittest")     # Unit testing framework

# Note: The following modules are intentionally excluded for WebAssembly:
# - socket/networking: Hardware-dependent, use JavaScript fetch() instead
# - threading: Not applicable in WebAssembly single-threaded environment
# - subprocess: Not available in WebAssembly
# - ssl: Security model conflicts with WebAssembly sandbox
# - pickle: May have WebAssembly compatibility issues
# - multiprocessing: Process-based parallelism not available

# CircuitPython WebAssembly uses custom asyncio implementation
# See: ports/webassembly/asyncio/ for JavaScript-integrated async support