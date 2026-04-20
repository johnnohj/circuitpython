/*
 * supervisor/semihosting.h — WASM ↔ JS shared-memory FFI.
 *
 * "Semihosting" is the conceptual frame: JS is the host, WASM is the
 * target, and this module provides the communication substrate between
 * them — analogous to ARM semihosting where a debugger provides I/O
 * services to the chip.
 *
 * All data transfer uses WASM linear memory (no WASI fd round-trips):
 *
 *   Event ring (JS → C):
 *     JS writes sh_event_t records into a circular buffer in linear
 *     memory via sh_event_ring_addr().  The supervisor drains them
 *     in hal_step() via sh_drain_event_ring().  Keyboard input,
 *     timer fires, hardware changes — all arrive as events.
 *
 *   State export (C → JS):
 *     The supervisor writes an sh_state_t struct to a static buffer
 *     each cp_step().  JS reads it via sh_state_addr() — no WASM
 *     export call needed, just a DataView over linear memory.
 *
 *   WASM exports (JS → C, direct calls):
 *     cp_exec(), cp_step(), cp_ctrl_c(), etc.
 *
 * Future bidirectional FFI (Python ↔ JS object proxies) will use
 * the jsffi/proxy pattern from the MicroPython webassembly port,
 * not fd-based RPC.
 */

#ifndef WASM_SEMIHOSTING_H
#define WASM_SEMIHOSTING_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* VM state export (C → JS)                                            */
/* ------------------------------------------------------------------ */

#define SH_STATE_SIZE    24

/* Written by supervisor each cp_step(), read by JS via sh_state_addr() */
typedef struct {
    uint32_t sup_state;    /* SUP_UNINITIALIZED, SUP_REPL, etc. */
    uint32_t yield_reason; /* YIELD_BUDGET, YIELD_SLEEP, etc. */
    uint32_t yield_arg;    /* e.g., sleep duration ms */
    uint32_t frame_count;
    uint32_t vm_depth;     /* pystack depth / nesting level */
    uint32_t pending_call; /* reserved for future FFI use */
} sh_state_t;

/* ------------------------------------------------------------------ */
/* Event ring (JS → C)                                                 */
/* ------------------------------------------------------------------ */

/* Event entry — JS appends, supervisor drains */
typedef struct {
    uint16_t event_type;   /* SH_EVT_* */
    uint16_t event_data;   /* type-specific */
    uint32_t arg;          /* type-specific */
} sh_event_t;

#define SH_EVENT_SIZE    8
#define SH_EVENT_MAX     32  /* ring capacity */

/* Event types */
#define SH_EVT_NONE          0x00
#define SH_EVT_KEY_DOWN      0x01  /* data=keycode, arg=modifiers       */
#define SH_EVT_KEY_UP        0x02  /* data=keycode, arg=modifiers       */
#define SH_EVT_TIMER_FIRE    0x10  /* data=timer_id, arg=0              */
#define SH_EVT_FETCH_DONE    0x11  /* data=request_id, arg=http_status  */
#define SH_EVT_HW_CHANGE     0x20  /* data=hal_type, arg=pin/channel    */
#define SH_EVT_PERSIST_DONE  0x30  /* data=0, arg=0                     */
#define SH_EVT_RESIZE        0x40  /* data=width, arg=height            */
#define SH_EVT_WAKE          0x50  /* data=ctx_id (-1=all), arg=reason  */
#define SH_EVT_EXEC          0x60  /* data=kind (0=string,1=file), arg=len */
#define SH_EVT_CTRL_C        0x70  /* data=0, arg=0                     */
#define SH_EVT_CLEANUP       0x80  /* data=0, arg=0                     */

/* Ring buffer layout in linear memory:
 *   [write_idx:u32] [read_idx:u32] [entries: SH_EVENT_MAX * sh_event_t]
 * JS writes events and advances write_idx.
 * C reads events and advances read_idx.
 * No WASI fd calls — direct memory access only. */
#define SH_EVENT_RING_HEADER  8  /* write_idx + read_idx */
#define SH_EVENT_RING_SIZE    (SH_EVENT_RING_HEADER + SH_EVENT_MAX * SH_EVENT_SIZE)

/* ------------------------------------------------------------------ */
/* C-side API                                                          */
/* ------------------------------------------------------------------ */

/* Init: set up shared-memory structures. */
void sh_init(void);

/* Drain events from the linear-memory ring.  Called in hal_step(). */
void sh_drain_event_ring(void);

/* Event dispatch — weak default is a no-op.  supervisor.c overrides
 * to route KEY_DOWN → rx_buf, etc. */
void sh_on_event(const sh_event_t *evt);

/* Export VM state to linear memory.  Called at end of cp_step().
 * JS reads directly via sh_state_addr() — no WASI fd needed. */
void sh_export_state(uint32_t sup_state, uint32_t yield_reason,
                     uint32_t yield_arg, uint32_t frame_count,
                     uint32_t vm_depth);

#endif /* WASM_SEMIHOSTING_H */
