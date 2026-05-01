/*
 * supervisor/context.c — Multi-context execution for WASM port.
 *
 * All storage (pystacks, metadata, VM state, wake registrations)
 * lives in port_mem (port_memory.c).  The port is supreme — it owns
 * the memory; this module operates on port-provided regions.
 *
 * On context switch, we swap MP_STATE_THREAD's pystack pointers
 * to point at the target context's region — no memcpy needed.
 *
 * GC scans all non-free pystack regions as roots, so inactive contexts'
 * Python objects are never collected while the context exists.
 *
 * JS controls lifecycle via exported functions.  Context 0 is the main
 * context and always exists after cp_context_init().
 */

#include <string.h>
#include <stdio.h>

#include "py/mpstate.h"
#include "py/gc.h"
#include "py/pystack.h"

#include "supervisor/context.h"
#include "supervisor/port_memory.h"

/* ------------------------------------------------------------------ */
/* Convenience aliases into port_mem                                    */
/* ------------------------------------------------------------------ */

#define _meta       port_mem.ctx_meta
#define _vm         port_mem.ctx_vm
#define _active_id  port_mem.active_ctx_id
#define _wake_regs  port_mem.wake_regs

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static inline uint8_t *_pystack_base(int id) {
    return port_pystack(id);
}

static inline uint8_t *_pystack_end(int id) {
    return port_pystack_end(id);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void cp_context_init(void) {
    memset(_meta, 0, sizeof(port_mem.ctx_meta));
    memset(_vm, 0, sizeof(port_mem.ctx_vm));
    for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
        _meta[i].status = CTX_FREE;
        _meta[i].yield_state_off = CTX_NO_YIELD_STATE;
    }

    /* Create context 0 (main) and make it active. */
    _meta[0].status = CTX_IDLE;
    _meta[0].priority = 128;  /* mid-priority default */
    _active_id = 0;

    /* Point MP_STATE_THREAD pystack at context 0's region. */
    mp_pystack_init(_pystack_base(0), _pystack_end(0));
}

int cp_context_create(uint8_t priority) {
    for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
        if (_meta[i].status == CTX_FREE) {
            memset(&_meta[i], 0, sizeof(cp_context_meta_t));
            _meta[i].status = CTX_IDLE;
            _meta[i].priority = priority;
            _meta[i].yield_state_off = CTX_NO_YIELD_STATE;
            return i;
        }
    }
    return -1;  /* no free slots */
}

void cp_context_destroy(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return;
    if (id == _active_id) return;  /* can't destroy active context */

    /* Clear all metadata so GC never scans stale roots from a
     * previous lifecycle, even if a bug bypasses the CTX_FREE check. */
    memset(&_meta[id], 0, sizeof(cp_context_meta_t));
    _meta[id].status = CTX_FREE;
    _meta[id].yield_state_off = CTX_NO_YIELD_STATE;
    memset(&_vm[id], 0, sizeof(cp_context_vm_t));
}

void cp_context_load(int id, mp_code_state_t *cs) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return;
    if (id != _active_id) return;  /* code_state must be on active pystack */
    (void)cs;  /* code_state is already on the pystack; vm_yield_start handles it */
    _meta[id].status = CTX_RUNNABLE;
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

cp_context_vm_t *cp_context_vm(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return &_vm[0];
    return &_vm[id];
}

uint8_t cp_context_get_status(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return CTX_FREE;
    return _meta[id].status;
}

uint64_t cp_context_get_delay(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return 0;
    return (uint64_t)_meta[id].delay_until_lo |
           ((uint64_t)_meta[id].delay_until_hi << 32);
}

void cp_context_set_delay(int id, uint64_t delay_until) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return;
    _meta[id].delay_until_lo = (uint32_t)(delay_until & 0xFFFFFFFF);
    _meta[id].delay_until_hi = (uint32_t)(delay_until >> 32);
}

/* ------------------------------------------------------------------ */
/* Context switching                                                   */
/* ------------------------------------------------------------------ */

#if !MICROPY_ENABLE_VM_ABORT
/* Yield state — single global, valid only for the active context. */
extern void *mp_vm_yield_state;
#endif

void cp_context_save(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return;

    uint8_t *base = _pystack_base(id);

    /* Save pystack cursor as offset from base. */
    _meta[id].pystack_cur_off =
        (uint32_t)(MP_STATE_THREAD(pystack_cur) - base);

    #if !MICROPY_ENABLE_VM_ABORT
    /* Save yield_state as offset from base (old yield protocol). */
    if (mp_vm_yield_state != NULL) {
        _meta[id].yield_state_off =
            (uint32_t)((uint8_t *)mp_vm_yield_state - base);
    } else {
        _meta[id].yield_state_off = CTX_NO_YIELD_STATE;
    }
    #endif

    /* Save globals/locals. */
    _meta[id].globals_ptr = (uint32_t)(uintptr_t)MP_STATE_THREAD(dict_globals);
    _meta[id].locals_ptr  = (uint32_t)(uintptr_t)MP_STATE_THREAD(dict_locals);
}

void cp_context_restore(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return;

    uint8_t *base = _pystack_base(id);

    /* Restore pystack pointers. */
    MP_STATE_THREAD(pystack_start) = base;
    MP_STATE_THREAD(pystack_cur)   = base + _meta[id].pystack_cur_off;
    MP_STATE_THREAD(pystack_end)   = _pystack_end(id);

    #if !MICROPY_ENABLE_VM_ABORT
    /* Restore yield_state (old yield protocol). */
    if (_meta[id].yield_state_off != CTX_NO_YIELD_STATE) {
        mp_vm_yield_state = base + _meta[id].yield_state_off;
    } else {
        mp_vm_yield_state = NULL;
    }
    #endif

    /* Restore globals/locals. */
    MP_STATE_THREAD(dict_globals) = (mp_obj_dict_t *)(uintptr_t)_meta[id].globals_ptr;
    MP_STATE_THREAD(dict_locals)  = (mp_obj_dict_t *)(uintptr_t)_meta[id].locals_ptr;

    _active_id = id;
}

int cp_context_active(void) {
    return _active_id;
}

void cp_context_set_active(int id) {
    _active_id = id;
}

/* ------------------------------------------------------------------ */
/* Status management                                                   */
/* ------------------------------------------------------------------ */

void cp_context_set_status(int id, uint8_t status) {
    if (id >= 0 && id < CP_MAX_CONTEXTS) {
        _meta[id].status = status;
    }
}

void cp_context_set_sleeping(int id, uint64_t delay_until) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return;
    _meta[id].status = CTX_SLEEPING;
    _meta[id].delay_until_lo = (uint32_t)(delay_until & 0xFFFFFFFF);
    _meta[id].delay_until_hi = (uint32_t)(delay_until >> 32);
}

/* ------------------------------------------------------------------ */
/* Scheduler                                                           */
/* ------------------------------------------------------------------ */

/* cp_next_wake_ms — tell JS how many ms until the VM needs attention. */
__attribute__((export_name("cp_next_wake_ms")))
uint32_t cp_next_wake_ms(uint32_t now_ms) {
    uint64_t now64 = (uint64_t)now_ms;
    uint64_t earliest = UINT64_MAX;
    bool any_runnable = false;

    for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
        uint8_t st = _meta[i].status;
        if (st == CTX_RUNNABLE || st == CTX_RUNNING || st == CTX_YIELDED) {
            any_runnable = true;
            break;
        }
        if (st == CTX_SLEEPING) {
            uint64_t deadline =
                (uint64_t)_meta[i].delay_until_lo |
                ((uint64_t)_meta[i].delay_until_hi << 32);
            if (deadline < earliest) {
                earliest = deadline;
            }
        }
    }

    if (any_runnable) {
        return 0;  /* tick now */
    }
    if (earliest < UINT64_MAX) {
        if (earliest <= now64) {
            return 0;  /* deadline already passed — tick now */
        }
        uint64_t delta = earliest - now64;
        if (delta > 0xFFFFFFFEULL) {
            return 0xFFFFFFFE;
        }
        return (uint32_t)delta;
    }
    return 0xFFFFFFFF;  /* idle indefinitely — wait for external event */
}

__attribute__((export_name("cp_scheduler_pick")))
int cp_scheduler_pick(uint64_t now_ms) {
    int best_id = -1;
    int best_pri = 256;

    for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
        /* Wake sleeping contexts whose deadline has passed. */
        if (_meta[i].status == CTX_SLEEPING) {
            uint64_t deadline =
                (uint64_t)_meta[i].delay_until_lo |
                ((uint64_t)_meta[i].delay_until_hi << 32);
            if ((uint64_t)now_ms >= deadline) {
                _meta[i].status = CTX_RUNNABLE;
            }
        }

        /* Also treat YIELDED as runnable (budget was exhausted last frame). */
        if (_meta[i].status == CTX_YIELDED) {
            _meta[i].status = CTX_RUNNABLE;
        }

        if (_meta[i].status == CTX_RUNNABLE && _meta[i].priority < best_pri) {
            best_id = i;
            best_pri = _meta[i].priority;
        }
    }

    return best_id;
}

/* ------------------------------------------------------------------ */
/* GC support                                                          */
/* ------------------------------------------------------------------ */

void *cp_context_pystack_base(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return NULL;
    return _pystack_base(id);
}

uint32_t cp_context_pystack_used(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return 0;
    if (id == _active_id) {
        /* Active context: read live pystack_cur. */
        return (uint32_t)(MP_STATE_THREAD(pystack_cur) -
                          MP_STATE_THREAD(pystack_start));
    }
    return _meta[id].pystack_cur_off;
}

void cp_context_gc_collect(void) {
    for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
        if (_meta[i].status == CTX_FREE) continue;

        uint8_t *base = _pystack_base(i);
        uint32_t used;

        if (i == _active_id) {
            /* Active context: pystack is live in MP_STATE_THREAD. */
            used = (uint32_t)(MP_STATE_THREAD(pystack_cur) - base);
        } else {
            used = _meta[i].pystack_cur_off;
        }

        /* Clamp to pystack region size — defense against corruption. */
        if (used > CP_CTX_PYSTACK_SIZE) {
            used = CP_CTX_PYSTACK_SIZE;
        }

        if (used > 0) {
            gc_collect_root((void **)base, used / sizeof(void *));
        }

        /* Also root the saved globals/locals dicts for inactive contexts. */
        if (i != _active_id) {
            if (_meta[i].globals_ptr) {
                void *g = (void *)(uintptr_t)_meta[i].globals_ptr;
                gc_collect_root(&g, 1);
            }
            if (_meta[i].locals_ptr) {
                void *l = (void *)(uintptr_t)_meta[i].locals_ptr;
                gc_collect_root(&l, 1);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* WASM exports for JS                                                 */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_context_meta_addr")))
uintptr_t _cp_context_meta_addr(int id) {
    if (id < 0 || id >= CP_MAX_CONTEXTS) return 0;
    return (uintptr_t)&_meta[id];
}

__attribute__((export_name("cp_context_meta_size")))
int _cp_context_meta_size(void) {
    return (int)sizeof(cp_context_meta_t);
}

__attribute__((export_name("cp_context_max")))
int _cp_context_max(void) {
    return CP_MAX_CONTEXTS;
}

__attribute__((export_name("cp_context_create")))
int _cp_context_create(int priority) {
    return cp_context_create((uint8_t)priority);
}

__attribute__((export_name("cp_context_destroy")))
void _cp_context_destroy(int id) {
    cp_context_destroy(id);
}

__attribute__((export_name("cp_context_active")))
int _cp_context_active(void) {
    return _active_id;
}

__attribute__((export_name("cp_context_save")))
void _cp_context_save(int id) {
    cp_context_save(id);
}

__attribute__((export_name("cp_context_restore")))
void _cp_context_restore(int id) {
    cp_context_restore(id);
}

/* ------------------------------------------------------------------ */
/* Wake registrations                                                  */
/* ------------------------------------------------------------------ */

int cp_register_wake(int ctx_id, uint16_t event_type, uint16_t event_data, bool one_shot) {
    for (int i = 0; i < CP_MAX_WAKE_REGS; i++) {
        if (!_wake_regs[i].active) {
            _wake_regs[i].ctx_id = (uint8_t)ctx_id;
            _wake_regs[i].active = 1;
            _wake_regs[i].event_type = event_type;
            _wake_regs[i].event_data = event_data;
            _wake_regs[i].flags = one_shot ? 1 : 0;
            return i;
        }
    }
    return -1;  /* no free slots */
}

void cp_unregister_wake(int reg_id) {
    if (reg_id >= 0 && reg_id < CP_MAX_WAKE_REGS) {
        _wake_regs[reg_id].active = 0;
    }
}

void cp_unregister_wake_all(int ctx_id) {
    for (int i = 0; i < CP_MAX_WAKE_REGS; i++) {
        if (_wake_regs[i].active && _wake_regs[i].ctx_id == (uint8_t)ctx_id) {
            _wake_regs[i].active = 0;
        }
    }
}

void cp_wake_check_event(uint16_t event_type, uint16_t event_data) {
    for (int i = 0; i < CP_MAX_WAKE_REGS; i++) {
        if (!_wake_regs[i].active) continue;
        if (_wake_regs[i].event_type != event_type) continue;
        if (_wake_regs[i].event_data != CP_WAKE_DATA_ANY &&
            _wake_regs[i].event_data != event_data) continue;

        /* Match — wake the context */
        extern void cp_wake(int ctx_id);
        cp_wake(_wake_regs[i].ctx_id);

        /* One-shot: unregister after firing */
        if (_wake_regs[i].flags & 1) {
            _wake_regs[i].active = 0;
        }
    }
}
