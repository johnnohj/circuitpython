# CircuitPython WebAssembly Base Manifest
# Shared foundation for all CircuitPython WebAssembly variants
# This file contains the core asyncio implementation and common base functionality

# The asyncio package is built from the standard implementation but with the
# core scheduler replaced with a custom scheduler that uses the JavaScript
# runtime (with setTimeout and Promise's) to control the scheduling.
add_library("modjsffi", "$(PORT_DIR)/modjsffi")
require("mip-cmdline")
require("ssl")
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

# Base modules that all CircuitPython WebAssembly variants should have
# Individual variants can extend this by including this file and adding more modules
