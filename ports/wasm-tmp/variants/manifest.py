# Base manifest for wasm-dist port.
# unix-ffi, mip, ssl removed — not useful in browser context.
# Runtime library installation is handled by fwip (JS-side).
require("argparse")
package(
    "asyncio",
    (
        "__init__.py",
        "core.py",
        "event.py",
        "funcs.py",
        "lock.py",
    ),
    base_path="$(PORT_DIR)/modules",
    opt=3,
)
