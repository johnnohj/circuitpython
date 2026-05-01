// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on CircuitPython port conventions (ports/wasm/)
// SPDX-FileCopyrightText: Contributions by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/ffi_exports.c — WASM export declarations.
//
// Thin wrappers that attach __attribute__((export_name)) to functions
// defined in other port/ files.  This is the single place to see all
// exports visible to JS.
//
// ── Exports defined inline in other files ──
//
// port/port_memory.c:
//   cp_port_memory_addr() → uintptr_t   (base of port_mem)
//   cp_port_memory_size() → uint32_t    (sizeof port_memory_t)
//
// port/hal.c:
//   hal_mark_gpio_dirty(pin) → void
//   hal_mark_analog_dirty(pin) → void
//   hal_get_change_count() → uint32_t
//
// Design refs:
//   design/wasm-layer.md  (wasm layer, JS↔C boundary)

#include <stdint.h>
#include <string.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "port/port_memory.h"
#include "port/port_step.h"
#include "port/constants.h"
#include "port/hal.h"
#include "supervisor/shared/serial.h"
#include "common-hal/microcontroller/Pin.h"

// ── port/main.c functions ──

extern void port_init(void);
extern uint32_t port_frame(uint32_t now_us, uint32_t budget_us);

// ── Top-level entry points ──

__attribute__((export_name("chassis_init")))
void chassis_init(void) {
    port_init();
}

__attribute__((export_name("chassis_frame")))
uint32_t chassis_frame(uint32_t now_us, uint32_t budget_us) {
    return port_frame(now_us, budget_us);
}

// ── Lifecycle controls ──

__attribute__((export_name("cp_ctrl_c")))
void cp_ctrl_c(void) {
    mp_sched_keyboard_interrupt();
}

__attribute__((export_name("cp_ctrl_d")))
void cp_ctrl_d(void) {
    port_step_soft_reboot();
}

__attribute__((export_name("cp_start_code")))
void cp_start_code(void) {
    port_step_start_code();
}

__attribute__((export_name("cp_start_repl")))
void cp_start_repl(void) {
    port_step_start_repl();
}

// ── Input buffer ──

__attribute__((export_name("cp_input_buf_addr")))
uintptr_t cp_input_buf_addr(void) {
    return (uintptr_t)port_input_buf();
}

__attribute__((export_name("cp_input_buf_size")))
uint32_t cp_input_buf_size(void) {
    return (uint32_t)port_input_buf_size();
}

// ── Serial layout ──
// JS needs to know where serial_tx lives in port_mem to drain output.
// Export the offset so JS doesn't hardcode the port_memory_t layout.

__attribute__((export_name("cp_serial_tx_addr")))
uintptr_t cp_serial_tx_addr(void) {
    return (uintptr_t)&port_mem.serial_tx;
}

__attribute__((export_name("cp_serial_tx_size")))
uint32_t cp_serial_tx_size(void) {
    return (uint32_t)sizeof(port_mem.serial_tx);
}

// ── Serial input ──
// Push a byte into the port_mem.serial_rx ring buffer.
// This is how JS delivers keystrokes to the C-side REPL.
// The upstream serial multiplexer reads from this ring via
// port_serial_read() in port/port_serial.c.
#include "port/serial.h"

__attribute__((export_name("cp_serial_push")))
void cp_serial_push(int c) {
    uint8_t byte = (uint8_t)c;
    serial_rx_write(&byte, 1);
}

// ── High-level API ──
// These match the ports/wasm cp_exec/cp_print/cp_banner/cp_cleanup API
// that board.mjs expects.

// CP_EXEC_STRING = 0, CP_EXEC_FILE = 1  (must match JS constants)
#define CP_EXEC_STRING 0
#define CP_EXEC_FILE   1

__attribute__((export_name("cp_exec")))
int cp_exec(int kind, int len) {
    char *buf = port_input_buf();
    if (len <= 0 || (size_t)len >= port_input_buf_size()) return -2;
    buf[len] = '\0';

    if (kind == CP_EXEC_FILE) {
        // Start file execution — path is already in input buffer.
        port_step_start_code();
        return 0;
    }
    // CP_EXEC_STRING: not yet supported in wasm-tmp
    // (would need pyexec_str or similar)
    return -2;
}

// Write text from the input buffer through the serial output path.
// Goes to both displayio terminal and serial_tx ring.
__attribute__((export_name("cp_print")))
void cp_print(int len) {
    if (len <= 0 || (size_t)len >= port_input_buf_size()) return;
    mp_hal_stdout_tx_strn(port_input_buf(), (size_t)len);
}

// Print the CircuitPython version banner.
// No leading \r\n — on a fresh terminal the cursor is at the top of the
// scroll area (same row as Blinka).  Text starts there.
__attribute__((export_name("cp_banner")))
void cp_banner(void) {
    mp_hal_stdout_tx_str(MICROPY_FULL_VERSION_INFO);
    mp_hal_stdout_tx_strn("\r\n", 2);
}

// Reset hardware state (pins, buses) to clean defaults.
__attribute__((export_name("cp_cleanup")))
void cp_cleanup(void) {
    hal_release_all();
    port_step_go_idle();
}

// Return supervisor state: 0=ready/idle, 1=executing, 2=repl
__attribute__((export_name("cp_state")))
int cp_state(void) {
    uint32_t phase = port_step_phase();
    if (phase == PHASE_CODE || phase == PHASE_BOOT) return 1;
    if (phase == PHASE_REPL) return 2;
    return 0;
}

// Set debug verbosity (stub — no-op for now).
__attribute__((export_name("cp_set_debug")))
void cp_set_debug(int level) {
    (void)level;
}
