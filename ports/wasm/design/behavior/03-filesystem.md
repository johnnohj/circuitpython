# Stage 7-9: Filesystem Init, Reset Port/Board, Board Init

Back to [README](README.md)

---

## 7. Filesystem init (`filesystem_init`)

| Upstream | Our port |
|----------|----------|
| Initialize SPI flash / internal flash | N/A |
| Create FAT filesystem if missing | N/A |
| Mount as CIRCUITPY | Mount VfsPosix over WASI at "/" |

**Our behavior**: Mount a VFS POSIX filesystem.  In Node, this is the
real filesystem (or memfs).  In browser, this is wasi-memfs.js backed
by IndexedDB for persistence.

The mounted root is `/CIRCUITPY/` on the WASI side, aliased to `/`
in the Python VFS.  `code.py` lives at `/CIRCUITPY/code.py` (WASI
path) = `/code.py` (Python path).

**Deviation**: Intentional.  POSIX VFS instead of FatFS.  No block
device layer.  Python code cannot tell the difference (same VFS API).

**Gap**: We don't create default files (code.py, boot.py) on first
mount like upstream does.  We should -- either in C init or in JS
before calling `chassis_init`.

**Important distinction**: The WASI/VFS POSIX filesystem is the
*user-facing* filesystem (where code.py, boot.py, and user files
live).  This is separate from port-internal MEMFS locations (where
the VM persists state, HAL slots live, etc.).  The user-facing
filesystem is what Python's `os` module sees.  MEMFS is invisible
to Python.

---

## 8. Reset port / reset board

| Upstream | Our port |
|----------|----------|
| Reset port-specific peripherals | Reset HAL state (pin claims, dirty flags) |
| Reset board-specific devices | Reset display if initialized |

**Our behavior**: Clear pin claim bitmask.  Reset HAL dirty flags.
Reset display state if CIRCUITPY_DISPLAYIO.  This is called both at
boot and between VM stages (code.py -> REPL transitions).

**Deviation**: None in spirit.

---

## 9. Board init (`board_init`)

| Upstream | Our port |
|----------|----------|
| Initialize SPI buses, I2C, displays | Initialize display framebuffer |
| Configure board-specific hardware | Populate pin categories from definition.json |

**Our behavior**: For browser board: JS parses definition.json and
populates `/hal/gpio` template slots with pin names and categories
*before any C code runs* (during WASM instantiation).  C sees a
fully-configured pin table at boot.  Initialize the displayio
framebuffer if configured.  For standard board: no board init needed
(CLI REPL only).

**Deviation**: Intentional.  Board definition is JSON, not C code.
Pin categories come from the definition file, not from compiled-in
pin tables.  This enables runtime board switching (future) and
user-provided board definitions.
