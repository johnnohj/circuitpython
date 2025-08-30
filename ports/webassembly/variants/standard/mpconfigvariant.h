#define MICROPY_VARIANT_ENABLE_JS_HOOK (1)

#define MICROPY_VFS (1)

// Enable uctypes module
#define MICROPY_PY_UCTYPES (1)

// Enable ring I/O buffer
#define MICROPY_PY_MICROPYTHON_RINGIO (1)

// Use standard REPL instead of event-driven due to CircuitPython compatibility issues
#define MICROPY_REPL_EVENT_DRIVEN (1)
