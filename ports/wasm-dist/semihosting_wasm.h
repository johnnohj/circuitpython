/*
 * semihosting_wasm.h — WASM adaptation of ARM semihosting
 *
 * On ARM targets, semihosting uses a breakpoint instruction (bkpt 0xAB) to
 * trap to the debugger/host for I/O operations (SYS_WRITE, SYS_READ, etc.).
 *
 * For the WASM-dist port, JavaScript/Emscripten is the "host".  Instead of a
 * breakpoint, we route semihosting operations through Emscripten's MEMFS
 * virtual device files (/dev/stdout, /dev/stdin, /debug/semihosting.log).
 * Both Python (via POSIX I/O) and JS (via Module.FS) can observe these files.
 *
 * API mirrors shared-old/runtime/semihosting_arm.h so callers are unchanged.
 */
#pragma once

#include <stddef.h>

/* Initialise semihosting console (opens virtual device FDs). */
void mp_semihosting_init(void);

/* Exit with status code — writes to /state/result.json + aborts VM. */
void mp_semihosting_exit(int status);

/* Receive a single character from /dev/stdin.
 * Blocks (via mp_js_hook loop) if stdin is currently empty. */
int  mp_semihosting_rx_char(void);

/* Receive up to len characters from /dev/stdin into str.
 * Returns the number of bytes actually read. */
int  mp_semihosting_rx_chars(char *str, size_t len);

/* Transmit str (len bytes) to /dev/stdout. */
void mp_semihosting_tx_strn(const char *str, size_t len);

/* Transmit str (len bytes) to /dev/stdout, converting bare \n → \r\n. */
void mp_semihosting_tx_strn_cooked(const char *str, size_t len);

/* Write directly to /debug/semihosting.log (raw debug output). */
void mp_semihosting_debug_write(const char *str, size_t len);
