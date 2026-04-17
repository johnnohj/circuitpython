/*
 * supervisor/semihosting.c — WASM ↔ JS shared-memory FFI.
 *
 * Two mechanisms, both using WASM linear memory (no WASI fds):
 *
 *   Event ring (JS → C):
 *     JS writes sh_event_t entries directly into _event_ring via
 *     sh_event_ring_addr() and advances write_idx.  The supervisor
 *     drains them in hal_step() via sh_drain_event_ring(), calling
 *     sh_on_event() for each.  Keyboard input, timer fires, and
 *     hardware changes all arrive through this single channel.
 *
 *   State export (C → JS):
 *     The supervisor writes an sh_state_t to _export_buf at the end
 *     of each cp_step().  JS reads it via sh_state_addr() — just a
 *     DataView over linear memory, no WASM export call needed.
 *
 * Why linear memory instead of WASI fds?  WASI fd_write during
 * cp_step() corrupts memory (likely memory.buffer detach after
 * GC triggers memory.grow).  Direct memory access is safe and fast.
 */

#include "supervisor/semihosting.h"

/* ------------------------------------------------------------------ */
/* sh_init                                                             */
/* ------------------------------------------------------------------ */

void sh_init(void) {
    /* Event ring and state export use linear memory — static globals
     * are zero-initialized, nothing to open.  Kept as a lifecycle
     * hook for future FFI init (e.g., jsffi proxy tables). */
}

/* ------------------------------------------------------------------ */
/* Event dispatch — weak default                                       */
/*                                                                     */
/* supervisor.c overrides this to route events:                        */
/*   KEY_DOWN → _rx_buf, Ctrl-C → mp_sched_keyboard_interrupt, etc.   */
/* ------------------------------------------------------------------ */

__attribute__((weak))
void sh_on_event(const sh_event_t *evt) {
    (void)evt;
}

/* ------------------------------------------------------------------ */
/* Linear-memory event ring                                            */
/*                                                                     */
/* JS writes events directly into this buffer via sh_event_ring_addr() */
/* and advances write_idx.  C reads and advances read_idx.  No WASI    */
/* fd calls — safe for per-frame use in cp_step().                     */
/* ------------------------------------------------------------------ */

static struct {
    uint32_t write_idx;  /* JS increments after writing an event */
    uint32_t read_idx;   /* C increments after reading an event */
    sh_event_t entries[SH_EVENT_MAX];
} _event_ring;

__attribute__((export_name("sh_event_ring_addr")))
uintptr_t sh_event_ring_addr(void) {
    return (uintptr_t)&_event_ring;
}

__attribute__((export_name("sh_event_ring_max")))
uint32_t sh_event_ring_max(void) {
    return SH_EVENT_MAX;
}

void sh_drain_event_ring(void) {
    while (_event_ring.read_idx != _event_ring.write_idx) {
        uint32_t idx = _event_ring.read_idx % SH_EVENT_MAX;
        sh_event_t *evt = &_event_ring.entries[idx];
        if (evt->event_type != SH_EVT_NONE) {
            sh_on_event(evt);
        }
        _event_ring.read_idx++;
    }
}

/* ------------------------------------------------------------------ */
/* sh_export_state — write VM state to linear memory                   */
/*                                                                     */
/* JS reads state directly from linear memory via sh_state_addr().     */
/* No WASI fd_write — safe to call every frame.                        */
/* ------------------------------------------------------------------ */

static sh_state_t _export_buf;

__attribute__((export_name("sh_state_addr")))
uintptr_t sh_state_addr(void) {
    return (uintptr_t)&_export_buf;
}

void sh_export_state(uint32_t sup_state, uint32_t yield_reason,
                     uint32_t yield_arg, uint32_t frame_count,
                     uint32_t vm_depth) {

    _export_buf.sup_state    = sup_state;
    _export_buf.yield_reason = yield_reason;
    _export_buf.yield_arg    = yield_arg;
    _export_buf.frame_count  = frame_count;
    _export_buf.vm_depth     = vm_depth;
    _export_buf.pending_call = 0;
}
