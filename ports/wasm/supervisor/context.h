/*
 * supervisor/context.h — Multi-context execution for WASM port.
 *
 * Up to CP_MAX_CONTEXTS independent Python execution contexts, each with
 * its own pystack region.  JS controls lifecycle and scheduling via
 * exported functions.  Context 0 is the main context (code.py + REPL).
 *
 * Each context has:
 *   - A pystack region (static array in linear memory)
 *   - Saved VM state (yield_state, globals, delay_until)
 *   - Priority (0 = highest) for scheduler
 *   - Status (free, idle, runnable, running, sleeping, done)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "py/bc.h"

#define CP_MAX_CONTEXTS     8
#define CP_CTX_PYSTACK_SIZE (8 * 1024)

/* Context status values */
#define CTX_FREE      0   /* slot available */
#define CTX_IDLE      1   /* created but no code loaded */
#define CTX_RUNNABLE  2   /* ready to run (has code_state) */
#define CTX_RUNNING   3   /* currently executing in vm_yield_step */
#define CTX_YIELDED   4   /* yielded (budget exhausted) */
#define CTX_SLEEPING  5   /* time.sleep() — waiting for deadline */
#define CTX_DONE      6   /* execution completed */

/* Sentinel for "no yield state saved".  Offset 0 is valid (first
 * code_state in a pystack region), so we use UINT32_MAX instead. */
#define CTX_NO_YIELD_STATE UINT32_MAX

/* Context metadata — exported to JS via cp_context_meta_addr().
 * JS reads this directly from linear memory. */
typedef struct {
    uint8_t  status;
    uint8_t  priority;        /* 0 = highest, 255 = lowest */
    uint16_t reserved;
    uint32_t pystack_cur_off; /* offset of pystack_cur from region base */
    uint32_t yield_state_off; /* offset of yield_state from region base (CTX_NO_YIELD_STATE = none) */
    uint32_t delay_until_lo;  /* sleep deadline (low 32 bits) */
    uint32_t delay_until_hi;  /* sleep deadline (high 32 bits) */
    uint32_t globals_ptr;     /* saved dict_globals */
    uint32_t locals_ptr;      /* saved dict_locals */
} cp_context_meta_t;

/* Per-context VM stepping state.
 * These mirror the file-scope globals in vm_yield.c but are indexed
 * by context id so context switching preserves each context's state. */
typedef struct {
    mp_code_state_t *code_state;   /* root code_state on this context's pystack */
    bool started;                  /* has code loaded */
    bool first_entry;              /* first call since vm_yield_start */
} cp_context_vm_t;

/* Access the VM state for a context. */
cp_context_vm_t *cp_context_vm(int id);

/* Get a context's status. */
uint8_t cp_context_get_status(int id);

/* Get/set the delay_until for the active context (used by vm_yield.c). */
uint64_t cp_context_get_delay(int id);
void cp_context_set_delay(int id, uint64_t delay_until);

/* ── Lifecycle ── */

/* Initialize the context system.  Must be called once after mp_init(). */
void cp_context_init(void);

/* Create a new context with the given priority.
 * Returns context id (0–7) or -1 if no free slots. */
int cp_context_create(uint8_t priority);

/* Destroy a context, freeing its slot. */
void cp_context_destroy(int id);

/* Load a compiled code_state into a context and mark it runnable.
 * The code_state must have been allocated on the ACTIVE pystack
 * (i.e., this context must be the active one). */
void cp_context_load(int id, mp_code_state_t *cs);

/* ── Switching ── */

/* Save the active context's VM state (pystack pointers, globals, yield_state).
 * Call before switching away from this context. */
void cp_context_save(int id);

/* Restore a context's VM state (pystack pointers, globals, yield_state).
 * Call before running vm_yield_step() for this context. */
void cp_context_restore(int id);

/* Get/set the active context id. */
int cp_context_active(void);
void cp_context_set_active(int id);

/* ── Scheduling ── */

/* Pick the highest-priority runnable context.
 * Wakes sleeping contexts whose deadline has passed.
 * Returns context id or -1 if none runnable. */
int cp_scheduler_pick(uint64_t now_ms);

/* ── Status updates (called by vm_yield_step result handling) ── */
void cp_context_set_status(int id, uint8_t status);
void cp_context_set_sleeping(int id, uint64_t delay_until);

/* ── Wake registrations ── */

/* A wake registration: when an event matching (event_type, event_data)
 * arrives via sh_on_event, wake the associated context.
 *
 * event_data matching:
 *   0xFFFF = wildcard (match any event_data for this type)
 *   other  = exact match
 */
#define CP_MAX_WAKE_REGS 16
#define CP_WAKE_DATA_ANY 0xFFFF

typedef struct {
    uint8_t  ctx_id;       /* context to wake */
    uint8_t  active;       /* 1 = active, 0 = free slot */
    uint16_t event_type;   /* SH_EVT_* to match */
    uint16_t event_data;   /* specific data to match (or CP_WAKE_DATA_ANY) */
    uint16_t flags;        /* 1 = one-shot (auto-unregister after fire) */
} cp_wake_reg_t;

/* Register a wake condition. Returns registration id (0..15) or -1 if full.
 * one_shot: if true, registration is cleared after first match. */
int cp_register_wake(int ctx_id, uint16_t event_type, uint16_t event_data, bool one_shot);

/* Unregister a wake condition by id. */
void cp_unregister_wake(int reg_id);

/* Unregister all wake conditions for a context. */
void cp_unregister_wake_all(int ctx_id);

/* Check incoming event against all registrations. Wake matching contexts.
 * Called by sh_on_event for every event. */
void cp_wake_check_event(uint16_t event_type, uint16_t event_data);

/* ── Exports for JS ── */
/* These are declared here but defined with __attribute__((export_name)) in context.c */

/* ── GC support ── */

/* Scan all non-free pystack regions for GC roots. */
void cp_context_gc_collect(void);

/* Get the pystack region base address for a context.
 * Used by gccollect.c to scan pystack regions as roots. */
void *cp_context_pystack_base(int id);
uint32_t cp_context_pystack_used(int id);
