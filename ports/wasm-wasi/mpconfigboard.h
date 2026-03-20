/*
 * mpconfigboard.h — WASI "board" configuration
 *
 * Required by py/circuitpy_mpconfig.h. On real hardware, this defines
 * pin names, flash size, etc. For WASI, it's mostly empty — the
 * "board" is virtual.
 */
#pragma once

#define MICROPY_HW_BOARD_NAME       "WASM-WASI"
#define MICROPY_HW_MCU_NAME         "wasm32"

// No flash filesystem on WASI (using VFS POSIX instead)
// DISABLE_FILESYSTEM skips the *_FLASH_FILESYSTEM requirement
#define DISABLE_FILESYSTEM          (1)
#define INTERNAL_FLASH_FILESYSTEM   (0)
#define CIRCUITPY_INTERNAL_NVM_SIZE (0)

// No USB
#define CIRCUITPY_USB_DEVICE        (0)
#define USB_NUM_ENDPOINT_PAIRS      (0)

// Skip GCC version check (we use clang via wasi-sdk)
#define CIRCUITPY_MIN_GCC_VERSION   (0)

// Feature tuning
#define CIRCUITPY_TRACEBACK         (1)   // Required by MICROPY_PY_ASYNC_AWAIT
#define CIRCUITPY_STATUS_BAR        (0)

// WASI has no hardware interrupts, but the callback critical section
// still needs to protect against re-entrant callback_run_all calls.
// Override the defaults in background_callback.c to use our own guards
// (avoids pulling in shared-bindings/microcontroller/ headers).
extern void wasi_critical_begin(void);
extern void wasi_critical_end(void);
#define CALLBACK_CRITICAL_BEGIN     (wasi_critical_begin())
#define CALLBACK_CRITICAL_END       (wasi_critical_end())

#define CIRCUITPY_MICROCONTROLLER   (0)

// Feature flags referenced by py/*.c
#ifndef CIRCUITPY_FULL_BUILD
#define CIRCUITPY_FULL_BUILD        (0)
#endif
#ifndef CIRCUITPY_JSON
#define CIRCUITPY_JSON              (0)
#endif
#ifndef CIRCUITPY_ARRAY
#define CIRCUITPY_ARRAY             (0)
#endif
