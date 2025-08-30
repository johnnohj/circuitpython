# CircuitPython WebAssembly Standard Variant
# Balanced feature set for typical web development projects

include("$(PORT_DIR)/variants/manifest.py")

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

# Math and utility modules (if not built into CircuitPython core)
# Note: Many of these may already be built-in, but including for completeness
# require("math")       # Usually built-in to CircuitPython
# require("random")     # Usually built-in to CircuitPython  
# require("time")       # CircuitPython has enhanced time module built-in

# I/O operations compatible with WebAssembly
# require("io")           # Stream operations - likely built-in

# Data processing and manipulation
require("copy")         # Deep/shallow copying of objects

# Optional: Lightweight modules for specific use cases
# Uncomment based on your needs:

# require("operator")   # Operator functions as first-class objects
# require("locale")     # Internationalization support (may be heavy)
# require("logging")    # Structured logging (may be overkill for WebAssembly)

# Web development focused modules
# require("urllib-parse") # URL parsing (not urllib.urequest - that's for networking)

# Advanced data structures (only if needed)
# require("heapq")      # Heap queue algorithm

# Note: The following modules are intentionally excluded for WebAssembly:
# - datetime: Too heavy, use built-in time module instead  
# - ssl/hmac: Security modules may not work properly in WebAssembly sandbox
# - socket/networking: Hardware-dependent, use JavaScript fetch() instead
# - threading: Not applicable in WebAssembly single-threaded environment
# - subprocess: Not available in WebAssembly
# - unittest: Too heavy for embedded environment, use simple asserts
# - tarfile/gzip/zlib: Heavy compression modules, use JavaScript alternatives
# - sqlite3: Database operations better handled by web storage APIs

# CircuitPython WebAssembly uses custom asyncio implementation
# See: ports/webassembly/asyncio/ for JavaScript-integrated async support