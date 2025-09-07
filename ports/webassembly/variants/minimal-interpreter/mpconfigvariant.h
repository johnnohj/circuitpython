/*
 * Minimal CircuitPython Interpreter Variant
 * 
 * This variant provides just the core Python interpreter with essential
 * CircuitPython compatibility for code validation and learning.
 * 
 * Target size: ~150KB WASM
 * Use case: Quick code checking, syntax validation, basic Python execution
 */

// Set minimal feature level
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_BASIC_FEATURES)

// Essential Python features only
#define MICROPY_PY_BUILTINS_HELP (1)
#define MICROPY_PY_BUILTINS_HELP_TEXT circuitpython_help_text
#define MICROPY_PY_BUILTINS_HELP_MODULES (1)

// Basic I/O
#define MICROPY_PY_IO (1)
#define MICROPY_PY_SYS_STDIO_BUFFER (1)

// Essential collections
#define MICROPY_PY_COLLECTIONS (1)
#define MICROPY_PY_COLLECTIONS_DEQUE (0)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT (0)

// Basic string operations
#define MICROPY_PY_BINASCII (1)
#define MICROPY_PY_JSON (1)
#define MICROPY_PY_RE (1)
#define MICROPY_PY_RE_DEBUG (0)

// Math support
#define MICROPY_PY_MATH (1)
#define MICROPY_PY_CMATH (0)  // Skip complex math

// Time support (basic)
#define MICROPY_PY_TIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS (0)
#define MICROPY_PY_TIME_CUSTOM_SLEEP (0)

// Minimal CircuitPython modules for compatibility
#define CIRCUITPY_BOARD (1)
#define CIRCUITPY_DIGITALIO (0)        // Skip hardware modules
#define CIRCUITPY_ANALOGIO (0)
#define CIRCUITPY_BUSIO (0)
#define CIRCUITPY_MICROCONTROLLER (1)  // Keep for compatibility

// Disable advanced features
#define MICROPY_PY_MICROPYTHON_MEM_INFO (0)
#define MICROPY_PY_GC (1)              // Keep GC for memory management
#define MICROPY_PY_THREAD (0)
#define MICROPY_PY_ASYNCIO (0)         // Skip async support in minimal
#define MICROPY_PY_SOCKET (0)
#define MICROPY_PY_SSL (0)

// Disable filesystem for minimal build
#define MICROPY_VFS (0)
#define MICROPY_READER_VFS (0)
#define MICROPY_PERSISTENT_CODE_LOAD (0)
#define MICROPY_PERSISTENT_CODE_SAVE (0)

// Disable network features
#define MICROPY_PY_NETWORK (0)
#define MICROPY_PY_WEBREPL (0)

// Disable hardware-specific features
#define MICROPY_PY_MACHINE (0)
#define MICROPY_PY_MACHINE_PIN_MAKE_NEW mp_pin_make_new

// Minimal error reporting
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_WARNINGS (0)

// Reduced stack sizes for smaller memory footprint
#define MICROPY_STACK_CHECK (1)
#define MICROPY_STACK_SIZE (8 * 1024)  // 8KB stack

// Optimize for size
#define MICROPY_OPT_COMPUTED_GOTO (0)
#define MICROPY_COMP_CONST (0)
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN (0)
#define MICROPY_COMP_TRIPLE_TUPLE_ASSIGN (0)

// Essential REPL features
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_EMACS_KEYS (0)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_REPL_EVENT_DRIVEN (1)

// Minimal module search path
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS (0)

// Reduce float precision to save space
#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_FLOAT)
#endif

// Essential CircuitPython identification
extern const char circuitpy_help_text[];
#define MICROPY_PY_SYS_PLATFORM "webassembly"

// Board identification for minimal interpreter
#define MICROPY_HW_BOARD_NAME "WebAssembly-Minimal"
#define MICROPY_HW_MCU_NAME "Emscripten-Core"

// Disable USB features for minimal build
#define CIRCUITPY_USB_HID (0)
#define CIRCUITPY_USB_CDC (0)
#define CIRCUITPY_USB_VENDOR (0)

// Skip most CircuitPython-specific features
#define CIRCUITPY_ALARM (0)
#define CIRCUITPY_AUDIOBUSIO (0)
#define CIRCUITPY_AUDIOCORE (0)
#define CIRCUITPY_AUDIOPWMIO (0)
#define CIRCUITPY_BLEIO (0)
#define CIRCUITPY_CAMERA (0)
#define CIRCUITPY_COUNTIO (0)
#define CIRCUITPY_DISPLAYIO (0)
#define CIRCUITPY_FRAMEBUFFERIO (0)
#define CIRCUITPY_I2CPERIPHERAL (0)
#define CIRCUITPY_NEOPIXEL_WRITE (0)
#define CIRCUITPY_PARALLELDISPLAY (0)
#define CIRCUITPY_PULSEIO (0)
#define CIRCUITPY_PWMIO (0)
#define CIRCUITPY_RGBMATRIX (0)
#define CIRCUITPY_ROTARYIO (0)
#define CIRCUITPY_RTC (0)
#define CIRCUITPY_SDCARDIO (0)
#define CIRCUITPY_SUPERVISOR (0)
#define CIRCUITPY_TOUCHIO (0)
#define CIRCUITPY_WATCHDOG (0)
#define CIRCUITPY_WIFI (0)

// Minimal frozen modules
#define MICROPY_MODULE_FROZEN (0)
#define MICROPY_MODULE_FROZEN_STR (0)
#define MICROPY_MODULE_FROZEN_MPY (0)