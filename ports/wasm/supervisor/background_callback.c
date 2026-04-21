/*
 * supervisor/background_callback.c — Port-local background callback queue.
 *
 * Adapted from supervisor/shared/background_callback.c.
 * Same interface (background_callback.h), WASM-specific changes:
 *   - Critical sections use mpthreadport.c atomic sections
 *   - No USB/BLE wake (port_wake_main_task is a no-op)
 *   - No shared-bindings/microcontroller dependency
 */

#include <string.h>

#include "py/gc.h"
#include "py/mpconfig.h"
#include "supervisor/background_callback.h"
#include "supervisor/port.h"
#include "supervisor/semihosting.h"

#include "mpthreadport.h"

/* ------------------------------------------------------------------ */
/* Critical sections — use mpthreadport atomic sections                */
/* ------------------------------------------------------------------ */

#define CALLBACK_CRITICAL_BEGIN mp_thread_begin_atomic_section()
#define CALLBACK_CRITICAL_END   mp_thread_end_atomic_section()

/* ------------------------------------------------------------------ */
/* Callback queue                                                      */
/* ------------------------------------------------------------------ */

static volatile background_callback_t *volatile callback_head;
static volatile background_callback_t *volatile callback_tail;

/* On real hardware, this unblocks the main RTOS task so it runs
 * background callbacks immediately.  On WASM, JS is the main task —
 * we set a flag in semihosting state so JS knows to call cp_hw_step()
 * even when the VM is idle. */
void port_wake_main_task(void) {
    sh_set_bg_pending();
}

void background_callback_add_core(background_callback_t *cb) {
    CALLBACK_CRITICAL_BEGIN;
    if (cb->prev || callback_head == cb) {
        CALLBACK_CRITICAL_END;
        return;
    }
    cb->next = 0;
    cb->prev = (background_callback_t *)callback_tail;
    if (callback_tail) {
        callback_tail->next = cb;
    }
    if (!callback_head) {
        callback_head = cb;
    }
    callback_tail = cb;
    CALLBACK_CRITICAL_END;
}

void background_callback_add(background_callback_t *cb, background_callback_fun fun, void *data) {
    cb->fun = fun;
    cb->data = data;
    background_callback_add_core(cb);
}

bool background_callback_pending(void) {
    return callback_head != NULL;
}

static int background_prevention_count;

void background_callback_run_all(void) {
    /* Tier 3: port background tasks (tick simulation) run first */
    port_background_task();

    if (!background_callback_pending()) {
        return;
    }

    CALLBACK_CRITICAL_BEGIN;
    if (background_prevention_count) {
        CALLBACK_CRITICAL_END;
        return;
    }
    ++background_prevention_count;
    background_callback_t *cb = (background_callback_t *)callback_head;
    callback_head = NULL;
    callback_tail = NULL;
    while (cb) {
        background_callback_t *next = cb->next;
        cb->next = cb->prev = NULL;
        background_callback_fun fun = cb->fun;
        void *data = cb->data;
        CALLBACK_CRITICAL_END;
        if (fun) {
            fun(data);
        }
        CALLBACK_CRITICAL_BEGIN;
        cb = next;
    }
    --background_prevention_count;
    CALLBACK_CRITICAL_END;
}

void background_callback_prevent(void) {
    CALLBACK_CRITICAL_BEGIN;
    ++background_prevention_count;
    CALLBACK_CRITICAL_END;
}

void background_callback_allow(void) {
    CALLBACK_CRITICAL_BEGIN;
    --background_prevention_count;
    CALLBACK_CRITICAL_END;
}

void background_callback_reset(void) {
    background_callback_t *new_head = NULL;
    background_callback_t **previous_next = &new_head;
    background_callback_t *new_tail = NULL;
    CALLBACK_CRITICAL_BEGIN;
    background_callback_t *cb = (background_callback_t *)callback_head;
    while (cb) {
        background_callback_t *next = cb->next;
        cb->next = NULL;
        if (gc_ptr_on_heap((void *)cb) || gc_ptr_on_heap(cb->data)) {
            cb->prev = NULL;
        } else {
            *previous_next = cb;
            previous_next = &cb->next;
            cb->prev = new_tail;
            new_tail = cb;
        }
        cb = next;
    }
    callback_head = new_head;
    callback_tail = new_tail;
    background_prevention_count = 0;
    CALLBACK_CRITICAL_END;
}

void background_callback_gc_collect(void) {
    background_callback_t *cb = (background_callback_t *)callback_head;
    while (cb) {
        gc_collect_ptr(cb->data);
        cb = cb->next;
    }
}
