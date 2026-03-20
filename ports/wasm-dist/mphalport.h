/*
 * mphalport.h — WASM-dist Hardware Abstraction Layer
 *
 * I/O is routed through virtual devices in Emscripten MEMFS instead of
 * POSIX fd 0/1/2, making all Python I/O observable from JavaScript via
 * Module.FS without any special extension modules.
 */
#pragma once

#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#if MICROPY_WASI_HAL
/* WASI: stdin/stdout via standard POSIX fd 0/1 */
int mp_hal_stdin_rx_chr(void);
#else
/* Emscripten: stdin via semihosting virtual device */
static inline int mp_hal_stdin_rx_chr(void) {
    extern int mp_semihosting_rx_char(void);
    return mp_semihosting_rx_char();
}
#endif

/* stdout */
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len);
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);

/* Timing (delegated to JS via library.js) */
mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);
uint64_t  mp_hal_time_ms(void);
uint64_t  mp_hal_time_ns(void);

void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);

/* Interrupt character */
int mp_hal_get_interrupt_char(void);
void mp_hal_set_interrupt_char(int c);

/* ── Hardware register file ──────────────────────────────────────────────────
 *
 * 256 × 16-bit registers in WASM linear memory.  Simulates memory-mapped I/O
 * for GPIO, analog, and control state.  JS writes registers via exported C
 * functions; Python reads them via _blinka.read_reg().  mp_hal_hook() auto-
 * syncs from /dev/bc_in every 64 bytecodes.
 *
 * Register layout:
 *   0x00–0x0D: Digital pins D0–D13 (0 or 1)
 *   0x0E:      LED
 *   0x0F:      BUTTON
 *   0x10–0x15: Analog pins A0–A5 (0–65535)
 *   0x20–0x2F: Reserved (I2C, SPI, UART state)
 *   0xF0:      Flags (bit 0 = yield requested)
 */

#define HW_REG_COUNT  256

/* Register address constants */
#define HW_REG_D0     0x00
#define HW_REG_D1     0x01
#define HW_REG_D2     0x02
#define HW_REG_D3     0x03
#define HW_REG_D4     0x04
#define HW_REG_D5     0x05
#define HW_REG_D6     0x06
#define HW_REG_D7     0x07
#define HW_REG_D8     0x08
#define HW_REG_D9     0x09
#define HW_REG_D10    0x0A
#define HW_REG_D11    0x0B
#define HW_REG_D12    0x0C
#define HW_REG_D13    0x0D
#define HW_REG_LED    0x0E
#define HW_REG_BUTTON 0x0F
#define HW_REG_A0     0x10
#define HW_REG_A1     0x11
#define HW_REG_A2     0x12
#define HW_REG_A3     0x13
#define HW_REG_A4     0x14
#define HW_REG_A5     0x15
#define HW_REG_FLAGS  0xF0

/* C API — callable from Python wrappers and JS (via EXPORTED_FUNCTIONS) */
uint16_t hw_reg_read(int addr);
void     hw_reg_write(int addr, uint16_t val);
void     hw_reg_write_batch(const char *json, size_t len);
void     hw_reg_sync_from_bc_in(void);
void     hw_reg_enable_opfs(void);      /* enable dual-write to OPFS region */
void     hw_reg_sync_from_opfs(void);   /* pull register state from OPFS */
void     hw_reg_sync_to_opfs(void);     /* push register state to OPFS */

/* POSIX VFS retry macro (PEP 475 — retry on EINTR) */
#if MICROPY_VFS_POSIX
#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) \
    { \
        while (1) { \
            MP_THREAD_GIL_EXIT(); \
            ret = syscall; \
            MP_THREAD_GIL_ENTER(); \
            if (ret == -1) { \
                int err = errno; \
                if (err == EINTR) { \
                    mp_handle_pending(true); \
                    continue; \
                } \
                raise; \
            } \
            break; \
        } \
    }
#endif
