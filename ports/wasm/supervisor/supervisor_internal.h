/*
 * supervisor/supervisor_internal.h — Internal API for port.c
 *
 * Exposes supervisor state and functions that port.c (the JS-facing
 * boundary) needs to access.  Nothing outside supervisor.c and port.c
 * should include this header.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ---- Supervisor state constants ---- */

#define SUP_UNINITIALIZED    0
#define SUP_REPL             1
#define SUP_EXPR_RUNNING     2
#define SUP_CODE_RUNNING     3
#define SUP_CODE_FINISHED    4

/* ---- Shared state (owned by supervisor.c) ---- */

extern int sup_state;
extern bool sup_ctx0_is_code;
extern bool sup_code_header_printed;
extern uint32_t sup_frame_count;
extern bool sup_debug_enabled;
extern bool wasm_cli_mode;

/* Shared input buffer (JS → C string passing) */
#define SUP_INPUT_BUF_SIZE 4096
extern char sup_input_buf[];

/* ---- Debug macro ---- */

#define SUP_DEBUG(fmt, ...) do { \
    if (sup_debug_enabled) { \
        fprintf(stderr, "[sup] " fmt "\n", ##__VA_ARGS__); \
    } \
} while (0)

/* ---- Internal functions (supervisor.c provides) ---- */

void sup_print_banner(void);
void sup_print_soft_reboot(void);

/* VM yield (vm_yield.c) — use forward declaration to avoid including bc.h */
struct _mp_code_state_t;
extern void vm_yield_start(struct _mp_code_state_t *cs);
extern void vm_yield_stop(void);
extern bool vm_yield_code_running(void);

/* Serial (serial.c) */
extern void serial_push_byte(uint8_t c);
extern void serial_check_interrupt(void);

/* Port-boundary functions (port.c) — called by sh_on_event in supervisor.c */
extern int cp_exec(int kind, int len);
extern void cp_cleanup(void);
extern void cp_wake(int ctx_id);
extern int cp_is_runnable(void);
