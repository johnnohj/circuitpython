/*
 * supervisor/micropython.c — Port-local HAL I/O for WASM.
 *
 * Replaces supervisor/shared/micropython.c with WASM-specific routing.
 * Same interface (mp_hal_stdin_rx_chr, mp_hal_stdout_tx_strn), but
 * routes through our port-local serial.c instead of the shared one.
 *
 * Key design:
 *   - mp_hal_stdin_rx_chr() loops with MICROPY_VM_HOOK_LOOP so that
 *     background tasks run and the wall-clock budget is checked.
 *     When budget expires with no input, the VM yields back to JS.
 *   - mp_hal_stdout_tx_strn() routes through serial_write_substring()
 *     which handles display + console + CLI paths.
 */

#include <string.h>

#include "py/mpconfig.h"
#include "py/mphal.h"
#include "py/mpstate.h"
#include "py/runtime.h"
#include "py/stream.h"

#include "supervisor/shared/serial.h"

/* ------------------------------------------------------------------ */
/* stdin — hook-aware blocking read                                    */
/*                                                                     */
/* Pattern from supervisor/shared/micropython.c:26-37.                 */
/* The MICROPY_VM_HOOK_LOOP fires background tasks + budget check.     */
/* In browser mode, if budget expires with no input, the VM yields     */
/* back to JS via MP_VM_RETURN_YIELD.  The REPL suspends mid-call;     */
/* next cp_step() resumes where it left off.                           */
/* ------------------------------------------------------------------ */

/* Runtime mode flag — set by main() in supervisor.c */
extern bool wasm_cli_mode;

int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        #ifdef MICROPY_VM_HOOK_LOOP
        MICROPY_VM_HOOK_LOOP
        #endif
        mp_handle_pending(true);
        if (serial_bytes_available()) {
            return serial_read();
        }
        /* CLI mode: serial_bytes_available only checks the rx ring buffer.
         * Fall through to serial_read() which does a blocking WASI stdin
         * read when the ring buffer is empty. */
        if (wasm_cli_mode) {
            return serial_read();
        }
        /* Browser mode: loop back — hook will yield when budget is spent */
    }
}

/* ------------------------------------------------------------------ */
/* stdout — route through serial multiplexer                           */
/* ------------------------------------------------------------------ */

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    return serial_write_substring(str, len);
}

void mp_hal_stdout_tx_str(const char *str) {
    mp_hal_stdout_tx_strn(str, strlen(str));
}

/* Cooked version: convert \n to \r\n.
 * In practice, the terminal handles this, but provide for completeness. */
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    while (len > 0) {
        /* Find next newline */
        const char *nl = memchr(str, '\n', len);
        if (nl == NULL) {
            mp_hal_stdout_tx_strn(str, len);
            break;
        }
        /* Write up to and including the character before \n */
        size_t prefix = nl - str;
        if (prefix > 0) {
            mp_hal_stdout_tx_strn(str, prefix);
        }
        /* Write \r\n */
        mp_hal_stdout_tx_strn("\r\n", 2);
        str = nl + 1;
        len -= prefix + 1;
    }
}

/* ------------------------------------------------------------------ */
/* stdio poll — check if input is available                            */
/* ------------------------------------------------------------------ */

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if ((poll_flags & MP_STREAM_POLL_RD) && serial_bytes_available()) {
        ret |= MP_STREAM_POLL_RD;
    }
    return ret;
}
