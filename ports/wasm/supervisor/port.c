/*
 * supervisor/port.c — WASM port boundary layer.
 *
 * This file is where JS enters and exits C.  It implements:
 *   1. The supervisor/port.h interface (port_* functions)
 *   2. WASM exports that JS calls directly (cp_exec, cp_ctrl_c, etc.)
 *
 * On real hardware, the port is the boundary between the RTOS/hardware
 * and CircuitPython.  On WASM, the port is the boundary between JS
 * and CircuitPython.  JS pushes events through semihosting, calls
 * port exports, and reads results from linear memory.
 *
 * The supervisor (supervisor.c) handles internal coordination:
 * state machine, scheduling, VM stepping.  This file is the facade.
 */

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "py/repl.h"
#include "py/bc.h"

#include "mpthreadport.h"
#include "supervisor/context.h"
#include "supervisor/compile.h"
#include "supervisor/supervisor_internal.h"

#if CIRCUITPY_DISPLAYIO
#include "shared-module/displayio/__init__.h"
#endif
#if CIRCUITPY_MICROCONTROLLER
#include "shared-bindings/microcontroller/Pin.h"
#endif
#if CIRCUITPY_BOARD_I2C || CIRCUITPY_BOARD_SPI || CIRCUITPY_BOARD_UART
#include "shared-module/board/__init__.h"
#endif

/* ================================================================== */
/* Part 1: supervisor/port.h interface                                 */
/* ================================================================== */

extern void supervisor_tick(void);

/* CircuitPython ticks are 1/1024 second (~0.977ms). */
uint64_t port_get_raw_ticks(uint8_t *subticks) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t total_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    uint64_t ticks = (total_us * 1024) / 1000000;
    if (subticks) {
        *subticks = 0;
    }
    return ticks;
}

/* Time-gate supervisor_tick() to ~1ms (one raw tick = 1/1024s ≈ 0.977ms).
 *
 * Upstream, supervisor_tick() is called from a 1ms SysTick ISR — hardware
 * limits the rate.  We have no ISR, so port_background_task() is called
 * every background_callback_run_all() invocation (potentially frequent now
 * that MICROPY_VM_HOOK_RETURN drains callbacks on every function return).
 * The time gate keeps the tick cadence ~1ms, matching upstream behavior.
 *
 * If mid-frame hardware polling is added (e.g., I2C device registry reads),
 * this is where that work would go — it runs at every callback drain, so
 * keep it cheap.  Expensive periodic work belongs in supervisor_tick(). */
static uint64_t _last_tick_ticks = 0;

void port_background_task(void) {
    uint64_t now = port_get_raw_ticks(NULL);
    if (now != _last_tick_ticks) {
        _last_tick_ticks = now;
        supervisor_tick();
    }
}

void port_enable_tick(void) {}
void port_disable_tick(void) {}
void port_interrupt_after_ticks(uint32_t ticks) { (void)ticks; }

void port_idle_until_interrupt(void) {
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
    nanosleep(&ts, NULL);
}

void port_background_tick(void) {}
void port_start_background_tick(void) {}
void port_finish_background_tick(void) {}

void *port_malloc(size_t size, bool dma_capable) {
    (void)dma_capable;
    return malloc(size);
}

void port_free(void *ptr) {
    free(ptr);
}

size_t gc_get_max_new_split(void) {
    return 0;
}

void port_gc_collect(void) {}

void common_hal_mcu_disable_interrupts(void) {
    mp_thread_begin_atomic_section();
}

void common_hal_mcu_enable_interrupts(void) {
    mp_thread_end_atomic_section();
}

void common_hal_mcu_delay_us(uint32_t us) {
    struct timespec ts = {
        .tv_sec = us / 1000000,
        .tv_nsec = (us % 1000000) * 1000
    };
    nanosleep(&ts, NULL);
}

/* ================================================================== */
/* Part 2: WASM exports — JS-facing boundary                          */
/*                                                                     */
/* These are port-boundary operations: where JS crosses into C.        */
/* They use supervisor state via supervisor_internal.h.                 */
/* ================================================================== */

/* Forward declarations */
int cp_is_runnable(void);

/* ---- Text output ---- */

__attribute__((export_name("cp_print")))
void cp_print(int len) {
    if (len <= 0 || len >= SUP_INPUT_BUF_SIZE) return;
    mp_hal_stdout_tx_strn(sup_input_buf, (size_t)len);
}

/* ---- Shared buffer ---- */

__attribute__((export_name("cp_input_buf_addr")))
uintptr_t cp_input_buf_addr(void) {
    return (uintptr_t)sup_input_buf;
}

__attribute__((export_name("cp_input_buf_size")))
int cp_input_buf_size(void) {
    return SUP_INPUT_BUF_SIZE;
}

/* ---- Frozen modules ---- */

extern const char mp_frozen_names[];

__attribute__((export_name("cp_frozen_names_addr")))
uintptr_t cp_frozen_names_addr(void) {
    return (uintptr_t)mp_frozen_names;
}

/* ---- Execution ---- */

#define CP_SRC_EXPR 0
#define CP_SRC_FILE 1
#define CP_CTX_MAIN (-1)
#define CP_CTX_NEW  (-2)

__attribute__((export_name("cp_run")))
int cp_run(int src_kind, int src_len, int ctx, int priority) {
    #if MICROPY_VM_YIELD_ENABLED
    if (src_len <= 0 || src_len >= SUP_INPUT_BUF_SIZE) return -4;
    if (src_kind != CP_SRC_EXPR && src_kind != CP_SRC_FILE) return -4;
    sup_input_buf[src_len] = '\0';

    if (ctx == CP_CTX_MAIN && sup_state != SUP_REPL) return -1;

    int target_id;
    int prev_id = cp_context_active();
    if (ctx == CP_CTX_MAIN) {
        target_id = 0;
        if (prev_id != 0) {
            cp_context_save(prev_id);
            cp_context_restore(0);
        }
    } else if (ctx == CP_CTX_NEW) {
        target_id = cp_context_create((uint8_t)priority);
        if (target_id < 0) return -3;
        mp_obj_dict_t *caller_globals = mp_globals_get();
        mp_obj_dict_t *caller_locals = mp_locals_get();
        cp_context_save(prev_id);
        cp_context_restore(target_id);
        mp_globals_set(caller_globals);
        mp_locals_set(caller_locals);
    } else {
        return -4;
    }

    mp_code_state_t *cs;
    if (src_kind == CP_SRC_EXPR) {
        cs = cp_compile_str(sup_input_buf, (size_t)src_len, MP_PARSE_SINGLE_INPUT);
    } else {
        cs = cp_compile_file(sup_input_buf);
    }
    if (cs == NULL) {
        if (ctx == CP_CTX_NEW) {
            cp_context_restore(prev_id);
            cp_context_destroy(target_id);
        }
        return -2;
    }

    vm_yield_start(cs);
    cp_context_load(target_id, cs);
    cp_context_set_status(target_id, CTX_RUNNABLE);

    if (ctx == CP_CTX_MAIN) {
        sup_ctx0_is_code = (src_kind == CP_SRC_FILE);
        sup_state = sup_ctx0_is_code ? SUP_CODE_RUNNING : SUP_EXPR_RUNNING;
        SUP_DEBUG("cp_run → %s on ctx0",
                  sup_ctx0_is_code ? "SUP_CODE_RUNNING" : "SUP_EXPR_RUNNING");
    } else {
        cp_context_save(target_id);
        cp_context_restore(prev_id);
        SUP_DEBUG("cp_run → ctx%d (kind=%d pri=%d)", target_id, src_kind, priority);
    }
    return target_id;
    #else
    (void)src_kind; (void)src_len; (void)ctx; (void)priority;
    return -4;
    #endif
}

#define CP_EXEC_STRING 0
#define CP_EXEC_FILE   1

__attribute__((export_name("cp_exec")))
int cp_exec(int kind, int len) {
    int r = cp_run(kind, len, CP_CTX_MAIN, 0);
    if (r == 0) return 0;
    if (r == -1) return -1;
    return -2;
}

__attribute__((export_name("cp_context_exec")))
int cp_context_exec(int len, int priority) {
    int r = cp_run(CP_SRC_EXPR, len, CP_CTX_NEW, priority);
    if (r >= 0) return r;
    if (r == -3) return -1;
    return -2;
}

__attribute__((export_name("cp_context_exec_file")))
int cp_context_exec_file(int path_len, int priority) {
    int r = cp_run(CP_SRC_FILE, path_len, CP_CTX_NEW, priority);
    if (r >= 0) return r;
    if (r == -3) return -1;
    return -2;
}

/* ---- REPL support ---- */

__attribute__((export_name("cp_complete")))
int cp_complete(int len) {
    if (len <= 0 || len >= SUP_INPUT_BUF_SIZE) return 0;
    sup_input_buf[len] = '\0';
    const char *compl_str;
    size_t compl_len = mp_repl_autocomplete(sup_input_buf, (size_t)len,
        &mp_plat_print, &compl_str);
    (void)compl_str;
    return (int)compl_len;
}

__attribute__((export_name("cp_syntax_check")))
int cp_syntax_check(int len) {
    if (len <= 0 || len >= SUP_INPUT_BUF_SIZE) return 2;
    sup_input_buf[len] = '\0';

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, sup_input_buf, (size_t)len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        (void)mp_compile(&parse_tree, source_name, false);
        nlr_pop();
        return 0;
    } else {
        mp_obj_print_exception(&mp_stderr_print,
            MP_OBJ_FROM_PTR(nlr.ret_val));
        return 1;
    }
}

__attribute__((export_name("cp_continue")))
int cp_continue(int len) {
    if (len <= 0 || len >= SUP_INPUT_BUF_SIZE) return 0;
    sup_input_buf[len] = '\0';
    return mp_repl_continue_with_input(sup_input_buf) ? 1 : 0;
}

/* ---- Control ---- */

__attribute__((export_name("cp_ctrl_c")))
void cp_ctrl_c(void) {
    if (cp_is_runnable()) {
        mp_sched_keyboard_interrupt();
    }
}

__attribute__((export_name("cp_banner")))
void cp_banner(void) {
    sup_print_banner();
}

__attribute__((export_name("cp_cleanup")))
void cp_cleanup(void) {
    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_stop();
    cp_context_set_status(0, CTX_IDLE);

    #if CIRCUITPY_DISPLAYIO
    reset_displays();
    #endif
    #if CIRCUITPY_BOARD_I2C || CIRCUITPY_BOARD_SPI || CIRCUITPY_BOARD_UART
    reset_board_buses();
    #endif
    #if CIRCUITPY_MICROCONTROLLER
    reset_all_pins();
    #endif

    gc_collect();
    cp_unregister_wake_all(0);

    sup_ctx0_is_code = false;
    sup_code_header_printed = false;
    sup_state = SUP_REPL;

    SUP_DEBUG("cleanup — Layer 3 torn down, Layer 2 intact");
    #endif
}

__attribute__((export_name("cp_wake")))
void cp_wake(int ctx_id) {
    if (ctx_id >= 0 && ctx_id < CP_MAX_CONTEXTS) {
        if (cp_context_get_status(ctx_id) == CTX_SLEEPING) {
            cp_context_set_delay(ctx_id, 0);
            cp_context_set_status(ctx_id, CTX_RUNNABLE);
            SUP_DEBUG("cp_wake → ctx%d RUNNABLE", ctx_id);
        }
    } else if (ctx_id == -1) {
        for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
            if (cp_context_get_status(i) == CTX_SLEEPING) {
                cp_context_set_delay(i, 0);
                cp_context_set_status(i, CTX_RUNNABLE);
            }
        }
        SUP_DEBUG("cp_wake → broadcast, all sleeping → RUNNABLE");
    }
}

__attribute__((export_name("cp_soft_reboot")))
void cp_soft_reboot(void) {
    sup_print_soft_reboot();
    cp_cleanup();
}

__attribute__((export_name("cp_ctrl_d")))
void cp_ctrl_d(void) {
    cp_soft_reboot();
}

/* ---- Wake registrations ---- */

__attribute__((export_name("cp_register_wake")))
int _cp_register_wake(int ctx_id, int event_type, int event_data, int one_shot) {
    return cp_register_wake(ctx_id, (uint16_t)event_type, (uint16_t)event_data, one_shot != 0);
}

__attribute__((export_name("cp_unregister_wake")))
void _cp_unregister_wake(int reg_id) {
    cp_unregister_wake(reg_id);
}

__attribute__((export_name("cp_unregister_wake_all")))
void _cp_unregister_wake_all(int ctx_id) {
    cp_unregister_wake_all(ctx_id);
}

/* ---- State queries ---- */

__attribute__((export_name("cp_get_state")))
int cp_get_state(void) {
    return (int)sup_state;
}

#define CP_STATE_READY      0
#define CP_STATE_EXECUTING  1
#define CP_STATE_SUSPENDED  2

__attribute__((export_name("cp_state")))
int cp_state(void) {
    uint8_t s0 = cp_context_get_status(0);
    if (s0 == CTX_SLEEPING) return CP_STATE_SUSPENDED;
    if (s0 == CTX_RUNNABLE || s0 == CTX_RUNNING || s0 == CTX_YIELDED) {
        return CP_STATE_EXECUTING;
    }
    for (int i = 1; i < CP_MAX_CONTEXTS; i++) {
        uint8_t s = cp_context_get_status(i);
        if (s == CTX_SLEEPING) return CP_STATE_SUSPENDED;
        if (s == CTX_RUNNABLE || s == CTX_RUNNING || s == CTX_YIELDED) {
            return CP_STATE_EXECUTING;
        }
    }
    return CP_STATE_READY;
}

__attribute__((export_name("cp_is_runnable")))
int cp_is_runnable(void) {
    for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
        uint8_t s = cp_context_get_status(i);
        if (s == CTX_RUNNABLE || s == CTX_RUNNING
            || s == CTX_YIELDED || s == CTX_SLEEPING) {
            return 1;
        }
    }
    return 0;
}

__attribute__((export_name("cp_needs_input")))
int cp_needs_input(void) {
    if (cp_is_runnable()) return 0;
    uint8_t s0 = cp_context_get_status(0);
    return (s0 == CTX_IDLE || s0 == CTX_DONE || s0 == CTX_FREE) ? 1 : 0;
}

__attribute__((export_name("cp_set_debug")))
void cp_set_debug(int enabled) {
    sup_debug_enabled = (enabled != 0);
}

__attribute__((export_name("cp_get_frame_count")))
uint32_t cp_get_frame_count(void) {
    return sup_frame_count;
}

#if MICROPY_VM_YIELD_ENABLED
__attribute__((export_name("cp_get_yield_reason")))
int cp_get_yield_reason(void) {
    extern volatile int mp_vm_yield_reason;
    return mp_vm_yield_reason;
}

__attribute__((export_name("cp_get_yield_arg")))
uint32_t cp_get_yield_arg(void) {
    extern volatile uint32_t mp_vm_yield_arg;
    return mp_vm_yield_arg;
}
#endif
