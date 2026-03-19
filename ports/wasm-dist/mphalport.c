/*
 * mphalport.c — WASM-dist Hardware Abstraction Layer implementation
 *
 * I/O is routed through virtual MEMFS devices (/dev/stdout, /dev/stdin)
 * so that JavaScript can observe Python I/O via Module.FS without any
 * special extension modules.
 *
 * Timing delegates to JavaScript via library.js (mp_js_ticks_ms etc.).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "py/mpconfig.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/mpprint.h"
#include "vdev.h"
#include "library.h"
#include "semihosting_wasm.h"
#include "shared/runtime/interrupt_char.h"
#include "opfs_regions.h"

/* ── Hardware register file ──────────────────────────────────────────────── */

static uint16_t hw_registers[HW_REG_COUNT];
static int hw_opfs_enabled = 0;  // set to 1 after opfs_init() succeeds

void hw_reg_enable_opfs(void) {
    hw_opfs_enabled = 1;
}

uint16_t hw_reg_read(int addr) {
    if (addr < 0 || addr >= HW_REG_COUNT) { return 0; }
    return hw_registers[addr];
}

void hw_reg_write(int addr, uint16_t val) {
    if (addr < 0 || addr >= HW_REG_COUNT) { return; }
    hw_registers[addr] = val;
    // Dual-write to OPFS region so other workers see the update
    if (hw_opfs_enabled) {
        opfs_write(OPFS_REGION_REGISTERS, addr * 2, &val, 2);
    }
}

void hw_reg_write_batch(const char *json, size_t len) {
    /*
     * Parse a simple JSON object { "pin_name": value, ... } and write
     * to registers.  Minimal hand-parser — flat object, string keys,
     * integer values.  Example: {"LED": 1, "A0": 512, "D5": 0}
     */
    const char *p = json;
    const char *end = json + len;

    while (p < end) {
        const char *kstart = memchr(p, '"', end - p);
        if (!kstart) { break; }
        kstart++;
        const char *kend = memchr(kstart, '"', end - kstart);
        if (!kend) { break; }

        const char *colon = memchr(kend + 1, ':', end - (kend + 1));
        if (!colon) { break; }

        const char *vstart = colon + 1;
        while (vstart < end && (*vstart == ' ' || *vstart == '\t')) { vstart++; }

        long val = strtol(vstart, (char **)&p, 10);

        size_t klen = kend - kstart;
        int addr = -1;

        if (klen == 3 && kstart[0] == 'L' && kstart[1] == 'E' && kstart[2] == 'D') {
            addr = HW_REG_LED;
        } else if (klen >= 2 && kstart[0] == 'D') {
            addr = HW_REG_D0 + (int)strtol(kstart + 1, NULL, 10);
            if (addr > HW_REG_D13) { addr = -1; }
        } else if (klen >= 2 && kstart[0] == 'A') {
            addr = HW_REG_A0 + (int)strtol(kstart + 1, NULL, 10);
            if (addr > HW_REG_A5) { addr = -1; }
        } else if (klen == 6 && memcmp(kstart, "BUTTON", 6) == 0) {
            addr = HW_REG_BUTTON;
        }

        if (addr >= 0 && addr < HW_REG_COUNT) {
            hw_registers[addr] = (uint16_t)val;
        }
    }
    // Bulk flush entire register file to OPFS after batch update
    if (hw_opfs_enabled) {
        opfs_write(OPFS_REGION_REGISTERS, 0, hw_registers, sizeof(hw_registers));
    }
}

/* Sync local register cache from OPFS (for cross-worker reads). */
void hw_reg_sync_from_opfs(void) {
    if (!hw_opfs_enabled) { return; }
    opfs_read(OPFS_REGION_REGISTERS, 0, hw_registers, sizeof(hw_registers));
}

/* Flush local register cache to OPFS (for cross-worker writes). */
void hw_reg_sync_to_opfs(void) {
    if (!hw_opfs_enabled) { return; }
    opfs_write(OPFS_REGION_REGISTERS, 0, hw_registers, sizeof(hw_registers));
}

void hw_reg_sync_from_bc_in(void) {
    /*
     * Read /dev/bc_in, parse as JSON register updates, clear bc_in.
     * Called automatically by mp_hal_hook() every 64 bytecodes, and
     * also callable explicitly from Python via _blinka.sync_registers().
     */
    char buf[2048];
    int fd = open("/dev/bc_in", O_RDONLY, 0);
    if (fd < 0) { return; }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) { return; }
    buf[n] = '\0';

    /* Clear bc_in after reading */
    fd = open("/dev/bc_in", O_WRONLY | O_TRUNC, 0);
    if (fd >= 0) { close(fd); }

    hw_reg_write_batch(buf, (size_t)n);
}

/* ---- stderr print object (used by vm.c for error output) ---- */

static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    /* Accumulate in JS _pyStderr buffer via library.js mp_js_write_stderr. */
    mp_js_write_stderr(str, (mp_uint_t)len);
}

const mp_print_t mp_stderr_print = { NULL, stderr_print_strn };

/* ---- stdout ---- */

/* Route Python print() through the JS mp_js_write library function so output
 * accumulates in Module._pyStdout (captured by mp_js_stdout_read after run). */
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    mp_js_write(str, (mp_uint_t)len);
    return len;
}

/* mp_hal_stdout_tx_strn_cooked provided by shared/runtime/stdout_helpers.c */
/* mp_js_write JS implementation is in library.js — no C override here. */

/* ---- timing ---- */

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)mp_js_ticks_ms();
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)(mp_js_ticks_ms() * 1000);
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return 0; /* not available in WASM */
}

uint64_t mp_hal_time_ms(void) {
    return (uint64_t)mp_js_time_ms();
}

uint64_t mp_hal_time_ns(void) {
    return (uint64_t)(mp_js_time_ms() * 1e6);
}

/* ---- Async delay: mp_async_request_delay (libpyasync.js) ---- */
extern void mp_async_request_delay(int ms);

void mp_hal_delay_ms(mp_uint_t ms) {
    /* Request an async delay.  In stepped mode (runStepped), the JS
     * driver honours this with a real setTimeout before resuming.
     * mp_async_request_delay also calls mp_tasks_request_yield(),
     * so the VM yields at the next branch point.
     *
     * In non-stepped mode (vm.run with timeout), the yield request
     * is harmless (no step budget set) and the busy-wait fallback
     * below handles the delay as before. */
    mp_async_request_delay((int)ms);

    /* Busy-wait fallback for non-stepped mode.  In stepped mode this
     * loop exits almost immediately because the yield request causes
     * the VM to suspend back to the JS driver at the next branch point. */
    mp_uint_t end = mp_hal_ticks_ms() + ms;
    while (mp_hal_ticks_ms() < end) {
        mp_js_hook();
        MICROPY_EVENT_POLL_HOOK
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    /* No sub-ms precision in WASM; treat as delay_ms rounding up */
    mp_hal_delay_ms((us + 999) / 1000);
}

/* ---- interrupt char ---- */

int mp_hal_get_interrupt_char(void) {
    return mp_interrupt_char;
}

/* mp_hal_set_interrupt_char provided by shared/runtime/interrupt_char.c */

/* ---- run deadline (C-side, avoids re-entrant WASM calls from JS hook) ----
 *
 * mp_js_set_deadline / mp_js_get_deadline are implemented here in C so that
 * the deadline check can happen from the VM hook macro (pure C path) without
 * any JS→WASM re-entrant call.  Emscripten SUPPORT_LONGJMP=emscripten has
 * known issues with re-entrant WASM calls from within WASM→JS callbacks.   */

static volatile uint32_t _run_deadline = 0;

/* Set when the host (deadline or /dev/interrupt) requested an abort.
 * Cleared at the start of each run by mp_js_set_deadline(). */
static volatile int _host_abort_requested = 0;

void mp_js_set_deadline(uint32_t deadline_ms) {
    _run_deadline = deadline_ms;
    _host_abort_requested = 0;  /* reset abort flag for each new run */
}

int mp_js_get_deadline(void) {
    return (int)_run_deadline;
}

/* Returns 1 if the last run was aborted by the host (timeout or interrupt). */
int mp_js_host_aborted(void) {
    return _host_abort_requested;
}

/* mp_hal_hook — called by MICROPY_VM_HOOK_POLL every 64 bytecodes.
 * 1. Check run deadline → mp_sched_keyboard_interrupt() if elapsed (pure C).
 * 2. Poll /dev/interrupt — JS writes 0x03 here to signal KeyboardInterrupt.
 * 3. Sync hardware registers from /dev/bc_in (pure C).
 * 4. Call mp_js_hook() for JS-side duties (bc_out drain).
 *
 * All signal checking is done in pure C to avoid re-entrant WASM calls,
 * which are problematic with SUPPORT_LONGJMP=emscripten.                    */
void mp_hal_hook(void) {
    /* 1. Deadline check */
    if (_run_deadline != 0 && (uint32_t)mp_js_ticks_ms() >= _run_deadline) {
        _run_deadline = 0;
        _host_abort_requested = 1;
        mp_sched_keyboard_interrupt();
        goto done; /* already signalled — skip /dev/interrupt check */
    }
    /* 2. /dev/interrupt: JS writes 0x03 here to send a KeyboardInterrupt.
     * Open once and cache the fd; if not yet created, skip silently. */
    {
        static int _interrupt_fd = -2; /* -2 = not yet attempted */
        if (_interrupt_fd == -2) {
            _interrupt_fd = open("/dev/interrupt", O_RDWR);
        }
        if (_interrupt_fd >= 0) {
            char ch = 0;
            ssize_t n = read(_interrupt_fd, &ch, 1);
            if (n == 1 && ch == '\x03') {
                /* Clear the file so the interrupt fires only once */
                lseek(_interrupt_fd, 0, SEEK_SET);
                ftruncate(_interrupt_fd, 0);
                _host_abort_requested = 1;
                mp_sched_keyboard_interrupt();
            }
        }
    }
done:
    /* 3. Sync hardware registers from /dev/bc_in (if any data pending) */
    hw_reg_sync_from_bc_in();
    /* 4. JS hook (bc_out drain) */
    mp_js_hook();
}

/* ── CircuitPython supervisor stubs ──────────────────────────────────────────
 * These are referenced by py/*.c when CIRCUITPY is defined (even as 0).
 * We provide no-op implementations since there is no real supervisor.       */

void reset_into_safe_mode(int safe_mode) {
    (void)safe_mode;
}

bool stack_ok(void) {
    return true;
}

void assert_heap_ok(void) {
}

// serial_write_compressed is provided by supervisor/shared/translate/translate.c
