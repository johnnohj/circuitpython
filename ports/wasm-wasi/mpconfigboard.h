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

#define MICROPY_HW_BOARD_NAME       "WASM-WASI"
#define MICROPY_HW_MCU_NAME         "wasm32"

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

// ---- No hardware peripherals ----
#define CIRCUITPY_MICROCONTROLLER   (0)
#define CIRCUITPY_PROCESSOR_COUNT   (1)
#define CIRCUITPY_NVM               (0)
#define CIRCUITPY_WATCHDOG          (0)
#define CIRCUITPY_DIGITALIO         (0)
#define CIRCUITPY_ANALOGIO          (0)
#define CIRCUITPY_BUSIO             (0)
#define CIRCUITPY_AUDIOBUSIO        (0)
#define CIRCUITPY_AUDIOIO           (0)
#define CIRCUITPY_AUDIOCORE         (0)
#define CIRCUITPY_AUDIOPWMIO        (0)
#define CIRCUITPY_BITBANGIO         (0)
#define CIRCUITPY_BLEIO_HCI         (0)
#define CIRCUITPY_COUNTIO           (0)
#define CIRCUITPY_DISPLAYIO         (0)
#define CIRCUITPY_FREQUENCYIO       (0)
#define CIRCUITPY_I2CTARGET         (0)
#define CIRCUITPY_KEYPAD            (0)
#define CIRCUITPY_NEOPIXEL_WRITE    (0)
#define CIRCUITPY_PIXELBUF          (0)
#define CIRCUITPY_PULSEIO           (0)
#define CIRCUITPY_PWMIO             (0)
#define CIRCUITPY_ROTARYIO          (0)
#define CIRCUITPY_SDCARDIO          (0)
#define CIRCUITPY_TOUCHIO           (0)

// ---- No status bar (no display/USB serial) ----
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
