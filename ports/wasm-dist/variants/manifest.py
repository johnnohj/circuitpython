# Base manifest for wasm-dist port.
# unix-ffi, mip, ssl removed — not useful in browser context.
# Runtime library installation is handled by fwip (JS-side).
require("argparse")
package("asyncio", base_path="$(PORT_DIR)/modules")
