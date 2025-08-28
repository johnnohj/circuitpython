# CircuitPython WebAssembly Full Variant
# Combines trial2WASM proven base with comprehensive module support

# Core trial2WASM modules (proven to work)
add_library("modjsffi", "$(PORT_DIR)/modjsffi")
require("mip-cmdline")
require("ssl")

# The asyncio package is built from the standard implementation but with the
# core scheduler replaced with a custom scheduler that uses the JavaScript
# runtime (with setTimeout and Promise's) to control the scheduling.
package(
    "asyncio",
    (
        "event.py",
        "funcs.py", 
        "lock.py",
    ),
    base_path="$(MPY_DIR)/extmod",
    opt=3,
)

package(
    "asyncio",
    (
        "__init__.py",
        "core.py",
    ),
    base_path="$(PORT_DIR)",
    opt=3,
)

# Enhanced modules for better development experience
require("datetime")      # Date/time handling
require("base64")        # Data encoding/decoding  
require("copy")          # Object copying utilities
require("functools")     # Higher-order functions

# Modern Python conveniences  
require("pathlib")       # Modern path handling
require("operator")      # Functional programming operators
require("contextlib")    # Context managers

# Development tools
require("unittest")      # Testing framework
require("logging")       # Debugging support
require("inspect")       # Code introspection

# Utility modules
require("itertools")     # Iterator tools
require("argparse")      # Argument parsing
require("tempfile")      # Temporary files
require("pprint")        # Pretty printing
require("textwrap")      # Text utilities
require("uu")            # UUencode/decode utilities
require("io")            # I/O stream utilities

# Collections
require("collections-defaultdict")

# Enhanced modules
require("os-path")       # Enhanced os.path

# Web-specific utilities
require("html")          # HTML utilities
require("hmac")          # Message authentication
require("gzip")          # Compression

# Additional useful modules for Node.js development
require("re")            # Regular expressions
require("json")          # JSON support
require("binascii")      # Binary/ASCII utilities
require("heapq")         # Heap queue algorithm
require("bisect")        # Array bisection