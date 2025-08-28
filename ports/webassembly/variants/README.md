# CircuitPython WebAssembly Variants

This directory contains different build configurations for CircuitPython WebAssembly, each optimized for different use cases.

## Shared Base (`manifest.py`)

All variants include:
- **Custom asyncio implementation** - JavaScript-integrated async/await support
- **Base WebAssembly runtime** - Core CircuitPython functionality

## Available Variants

### `minimal/` - Ultra-Lightweight
**6 core modules** for maximum performance and smallest binary size:
- `json`, `collections`, `base64`, `functools`, `os`, `io`

**Best for:**
- Embedded applications where size is critical
- Simple web interactions only
- Performance-critical applications

**Build:** `make VARIANT=minimal`

### `standard/` - Recommended Default
**11 modules** providing balanced functionality for typical web development:
- All minimal modules plus:
- `collections-defaultdict`, `itertools`, `pathlib`, `html`, `copy`, `fnmatch`, `inspect`

**Best for:**
- Most CircuitPython WebAssembly projects
- General web development
- Good balance of features vs size

**Build:** `make VARIANT=standard` or `make` (default)

### `full/` - Full-Featured
**20+ modules** including compression libraries and advanced features:
- All standard modules plus:
- `gzip`, `zlib` - Compression support
- `heapq`, `bisect` - Advanced algorithms
- `datetime`, `logging`, `locale` - Full-featured utilities
- `operator`, `uu` - Development tools

**Best for:**
- Data processing applications
- Working with compressed web data
- Complex web applications
- Development and debugging

**Build:** `make VARIANT=full`

## Module Selection Philosophy

**Included:**
- Web-compatible Python standard library modules
- Modules that complement JavaScript APIs
- Lightweight, embedded-friendly implementations

**Excluded:**
- Hardware-dependent modules (GPIO, I2C, etc.)
- Networking modules (use JavaScript fetch() instead)
- Threading/multiprocessing (single-threaded environment)
- Heavy modules better handled by browser APIs (SQLite → IndexedDB)

## Adding Custom Variants

1. Create new directory: `variants/myvariant/`
2. Add required files:
   - `manifest.py` - Include base + your modules
   - `mpconfigvariant.h` - Variant-specific config
   - `mpconfigvariant.mk` - Build configuration
3. Build: `make VARIANT=myvariant`

Example custom manifest:
```python
include("$(PORT_DIR)/variants/manifest.py")
require("my-custom-module")
```