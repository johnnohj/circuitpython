/*
 * mpconfigboard.h — WASI "board" configuration
 *
 * This is the single source of truth for what this CircuitPython port
 * supports. circuitpy_mpconfig.h reads these CIRCUITPY_* flags to
 * enable/disable modules and features. Both variants (standard and
 * OPFS) share this file.
 *
 * Design principle: enable everything that's pure software. Disable
 * only what requires hardware we can't provide (GPIO, audio, display,
 * BLE, USB, flash block devices).
 */
#pragma once

// Port heap — must be visible to all shared code that calls port_malloc/port_free
#include "supervisor/port_heap.h"

#define MICROPY_HW_BOARD_NAME       "WASM-WASI"
#define MICROPY_HW_MCU_NAME         "wasm32"
#define CIRCUITPY_BOARD_ID          "wasm_wasi"

// ---- Filesystem ----
// No flash filesystem — we use VFS POSIX over WASI fd_* syscalls.
// OPFS provides persistent storage; the supervisor doesn't manage it.
#define DISABLE_FILESYSTEM          (1)
#define INTERNAL_FLASH_FILESYSTEM   (0)
#define CIRCUITPY_INTERNAL_NVM_SIZE (0)
#define MICROPY_VFS_FAT             (0)
#define MICROPY_VFS_LFS1            (0)
#define MICROPY_VFS_LFS2            (0)

// ---- No USB ----
#define CIRCUITPY_USB_DEVICE        (0)
#define USB_NUM_ENDPOINT_PAIRS      (0)

// ---- Hardware peripherals ----
// Default: off. The worker variant enables these via mpconfigvariant.h
// and provides C common-hal backed by OPFS endpoints.
// The reactor uses frozen Python shims instead (modules/*.py).
#ifndef CIRCUITPY_MICROCONTROLLER
#define CIRCUITPY_MICROCONTROLLER   (0)
#endif
#define CIRCUITPY_PROCESSOR_COUNT   (1)
#ifndef CIRCUITPY_DIGITALIO
#define CIRCUITPY_DIGITALIO         (0)
#endif
#ifndef CIRCUITPY_ANALOGIO
#define CIRCUITPY_ANALOGIO          (0)
#endif
#ifndef CIRCUITPY_PWMIO
#define CIRCUITPY_PWMIO             (0)
#endif
#ifndef CIRCUITPY_NEOPIXEL_WRITE
#define CIRCUITPY_NEOPIXEL_WRITE    (0)
#endif
#ifndef CIRCUITPY_BOARD
#define CIRCUITPY_BOARD             (0)
#endif

// ---- Bus I/O: worker enables via mpconfigvariant.h ----
#ifndef CIRCUITPY_BUSIO
#define CIRCUITPY_BUSIO             (0)
#endif
#define CIRCUITPY_NVM               (0)
#define CIRCUITPY_WATCHDOG          (0)
#define CIRCUITPY_AUDIOBUSIO        (0)
#define CIRCUITPY_AUDIOIO           (0)
#define CIRCUITPY_AUDIOCORE         (0)
#define CIRCUITPY_AUDIOPWMIO        (0)
#define CIRCUITPY_BITBANGIO         (0)
#define CIRCUITPY_BLEIO_HCI         (0)
#define CIRCUITPY_COUNTIO           (0)
#define CIRCUITPY_FREQUENCYIO       (0)
#define CIRCUITPY_I2CTARGET         (0)
#define CIRCUITPY_KEYPAD            (0)
#define CIRCUITPY_PIXELBUF          (0)
#define CIRCUITPY_PULSEIO           (0)
#define CIRCUITPY_ROTARYIO          (0)
#define CIRCUITPY_SDCARDIO          (0)
#define CIRCUITPY_TOUCHIO           (0)

// ---- Display ----
// C displayio pipeline is worker-only (set in variants/worker/mpconfigvariant.h).
// Non-worker variants use a frozen Python displayio shim that writes to OPFS.
// Defaults here disable the C pipeline; the worker variant overrides them.
#ifndef CIRCUITPY_DISPLAYIO
#define CIRCUITPY_DISPLAYIO         (0)
#endif
#ifndef CIRCUITPY_FRAMEBUFFERIO
#define CIRCUITPY_FRAMEBUFFERIO     (0)
#endif
#ifndef CIRCUITPY_TERMINALIO
#define CIRCUITPY_TERMINALIO        (0)
#endif
#ifndef CIRCUITPY_FONTIO
#define CIRCUITPY_FONTIO            (0)
#endif
#ifndef CIRCUITPY_REPL_LOGO
#define CIRCUITPY_REPL_LOGO         (0)
#endif
#define CIRCUITPY_DISPLAY_LIMIT     (1)

// Display rendering buffer size (bytes per area update)
#ifndef CIRCUITPY_DISPLAY_AREA_BUFFER_SIZE
#define CIRCUITPY_DISPLAY_AREA_BUFFER_SIZE (512)
#endif

// Bus display drivers disabled everywhere (no SPI/I2C/parallel hardware)
#define CIRCUITPY_BUSDISPLAY        (0)
#define CIRCUITPY_FOURWIRE          (0)
#define CIRCUITPY_EPAPERDISPLAY     (0)
#define CIRCUITPY_I2CDISPLAYBUS     (0)
#define CIRCUITPY_PARALLELDISPLAYBUS (0)

// Optional displayio sub-features disabled everywhere
#define CIRCUITPY_VECTORIO          (0)
#define CIRCUITPY_GIFIO             (0)
#define CIRCUITPY_BITMAPFILTER      (0)
#define CIRCUITPY_BITMAPTOOLS       (0)
#define CIRCUITPY_LVFONTIO          (0)
#define CIRCUITPY_TILEPALETTEMAPPER (0)
#define CIRCUITPY_ONDISKBITMAP      (0)

// ---- Status bar (displayed on terminal) ----
#define CIRCUITPY_STATUS_BAR        (0)

// ---- Pure software features — enable ----
// CIRCUITPY_FULL_BUILD controls many FULL_BUILD-gated features.
// Set to 1 to enable: re, binascii, json, msgpack, zlib, traceback,
// warnings, errno, getpass, atexit, locale, codeop, builtins_pow3,
// string methods, frozenset, deque, complex, special methods, etc.
#define CIRCUITPY_FULL_BUILD        (1)

// Modules always on in CircuitPython
#define CIRCUITPY_SYS               (1)
#define CIRCUITPY_OS                (1)
#define CIRCUITPY_JSON              (1)
#define CIRCUITPY_RE                (1)
#define CIRCUITPY_RANDOM            (1)
#define CIRCUITPY_TRACEBACK         (1)
#define CIRCUITPY_COLLECTIONS       (1)
#define CIRCUITPY_BINASCII          (1)
#define CIRCUITPY_ERRNO             (1)
#define CIRCUITPY_STRUCT            (1)

// zlib requires uzlib library sources — enable later
#define CIRCUITPY_ZLIB              (0)

// Skip GCC version check (we use clang via wasi-sdk)
#define CIRCUITPY_MIN_GCC_VERSION   (0)

// ---- WASI critical section (no hardware interrupts) ----
extern void wasi_critical_begin(void);
extern void wasi_critical_end(void);
#define CALLBACK_CRITICAL_BEGIN     (wasi_critical_begin())
#define CALLBACK_CRITICAL_END       (wasi_critical_end())
