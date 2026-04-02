/*
 * supervisor/semihosting.h — WASM semihosting syscall table.
 *
 * On ARM/RV32, semihosting traps to the debugger via BKPT/EBREAK.
 * On WASM, there is no debugger — JS *is* the host.  The trap
 * mechanism is: C writes a request to /sys/call (a MEMFS endpoint),
 * then yields with YIELD_IO_WAIT.  JS reads the request from its
 * in-memory Map, fulfills it, writes the response to /sys/result,
 * and calls cp_step() to resume.
 *
 * The syscall table mirrors ARM semihosting (SYS_OPEN, SYS_READ, etc.)
 * in the low range, and adds browser-specific extensions in 0x100+.
 *
 * ┌──────────────────────────────────────────────────────────┐
 * │  /sys/call    Python→JS request (C writes, JS reads)    │
 * │  /sys/result  JS→Python response (JS writes, C reads)   │
 * │  /sys/events  JS→Python event queue (JS appends)        │
 * │  /sys/state   Python→JS VM state (C writes, JS reads)   │
 * └──────────────────────────────────────────────────────────┘
 *
 * Wire format: both /sys/call and /sys/result are fixed-size
 * binary records.  No JSON, no strings — just uint32_t fields
 * that both sides can read with DataView / direct cast.
 *
 *   /sys/call:   [call_nr:u32] [status:u32] [arg0:u32] [arg1:u32]
 *                [arg2:u32] [arg3:u32] [payload: up to 240 bytes]
 *                = 264 bytes total
 *
 *   /sys/result: [call_nr:u32] [status:u32] [ret0:u32] [ret1:u32]
 *                [payload: up to 248 bytes]
 *                = 264 bytes total
 *
 *   /sys/events: ring of [event_type:u16] [event_data:u16] [arg:u32]
 *                entries.  JS appends, supervisor drains each cp_step().
 *
 *   /sys/state:  [sup_state:u32] [yield_reason:u32] [yield_arg:u32]
 *                [frame_count:u32] [vm_depth:u32] [pending_call:u32]
 *                = 24 bytes.  Written by supervisor, read by JS.
 *
 * Status field in /sys/call:
 *   0 = SH_IDLE      — no pending request
 *   1 = SH_PENDING   — request written, waiting for host
 *   2 = SH_FULFILLED — host wrote response
 *   3 = SH_ERROR     — host could not fulfill
 */

#ifndef WASM_SEMIHOSTING_H
#define WASM_SEMIHOSTING_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Call status                                                         */
/* ------------------------------------------------------------------ */

#define SH_IDLE      0
#define SH_PENDING   1
#define SH_FULFILLED 2
#define SH_ERROR     3

/* ------------------------------------------------------------------ */
/* Syscall numbers — classic semihosting (0x01–0x31)                   */
/*                                                                     */
/* These mirror ARM/RV32 semihosting.  Not all are useful on WASM,     */
/* but keeping the same numbering lets us share test infrastructure    */
/* and mental models.                                                  */
/* ------------------------------------------------------------------ */

#define SH_SYS_OPEN          0x01  /* open file on host fs             */
#define SH_SYS_CLOSE         0x02  /* close host file handle           */
#define SH_SYS_WRITEC        0x03  /* write one char to debug console  */
#define SH_SYS_WRITE0        0x04  /* write NUL-terminated string      */
#define SH_SYS_WRITE         0x05  /* write buffer to host fd          */
#define SH_SYS_READ          0x06  /* read from host fd into buffer    */
#define SH_SYS_READC         0x07  /* read one char from console       */
#define SH_SYS_ISERROR       0x08  /* check if return val is error     */
#define SH_SYS_SEEK          0x0A  /* seek in host file                */
#define SH_SYS_FLEN          0x0C  /* get file length                  */
#define SH_SYS_REMOVE        0x0E  /* delete host file                 */
#define SH_SYS_RENAME        0x0F  /* rename host file                 */
#define SH_SYS_CLOCK         0x10  /* host clock (centiseconds)        */
#define SH_SYS_TIME          0x11  /* host time (epoch seconds)        */
#define SH_SYS_ERRNO         0x13  /* last host errno                  */
#define SH_SYS_EXIT          0x18  /* terminate                        */
#define SH_SYS_ELAPSED       0x30  /* elapsed time (tick count)        */
#define SH_SYS_TICKFREQ      0x31  /* tick frequency (Hz)              */

/* ------------------------------------------------------------------ */
/* Syscall numbers — browser extensions (0x100+)                       */
/*                                                                     */
/* Things a browser host can do that a JTAG debugger can't.            */
/* ------------------------------------------------------------------ */

/* ── Timers ── */
#define SH_SYS_TIMER_SET     0x100 /* request callback after N ms      */
                                   /* arg0=timer_id, arg1=delay_ms      */
#define SH_SYS_TIMER_CANCEL  0x101 /* cancel pending timer              */
                                   /* arg0=timer_id                     */

/* ── Network (fetch) ── */
#define SH_SYS_FETCH         0x110 /* HTTP fetch                        */
                                   /* arg0=method, arg1=url_offset,     */
                                   /* arg2=url_len, arg3=body_len       */
                                   /* payload=URL + body bytes          */
                                   /* result: status in ret0,           */
                                   /* body_len in ret1, body in payload */
#define SH_SYS_FETCH_STATUS  0x111 /* poll fetch completion             */
                                   /* arg0=request_id                   */
                                   /* result: 0=pending, 1=done, 2=err  */

/* ── Storage (IndexedDB / OPFS) ── */
#define SH_SYS_PERSIST_SYNC  0x120 /* flush CIRCUITPY to IndexedDB      */
                                   /* no args — host decides what's dirty*/
#define SH_SYS_PERSIST_LOAD  0x121 /* reload from IndexedDB             */

/* ── Display ── */
#define SH_SYS_DISPLAY_SYNC  0x130 /* signal: framebuffer is ready      */
                                   /* arg0=frame_count                   */

/* ── Hardware events (JS → Python) ── */
#define SH_SYS_HW_IRQ       0x140 /* simulated hardware interrupt      */
                                   /* arg0=irq_number, arg1=pin/channel */
#define SH_SYS_HW_POLL      0x141 /* request: refresh /hal/ endpoint   */
                                   /* arg0=hal_type (gpio=0,analog=1..) */

/* ── VM introspection (Python → JS) ── */
#define SH_SYS_VM_STATE      0x150 /* export VM scheduling state        */
                                   /* written to /sys/state, not call   */
#define SH_SYS_VM_EXCEPTION  0x151 /* report unhandled exception        */
                                   /* payload=exception string           */

/* ── Debug / logging ── */
#define SH_SYS_DEBUG_LOG     0x1F0 /* write to browser console.log      */
                                   /* arg0=level (0=log,1=warn,2=error) */
                                   /* payload=message bytes              */

/* ------------------------------------------------------------------ */
/* Wire format structures                                              */
/* ------------------------------------------------------------------ */

#define SH_CALL_SIZE     264
#define SH_RESULT_SIZE   264
#define SH_STATE_SIZE    24
#define SH_PAYLOAD_MAX   240
#define SH_RESULT_PAYLOAD_MAX 248

/* /sys/call record — C writes, JS reads */
typedef struct {
    uint32_t call_nr;      /* SH_SYS_* constant */
    uint32_t status;       /* SH_IDLE / SH_PENDING */
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint8_t  payload[SH_PAYLOAD_MAX];
} sh_call_t;

/* /sys/result record — JS writes, C reads */
typedef struct {
    uint32_t call_nr;      /* echo of request call_nr */
    uint32_t status;       /* SH_FULFILLED / SH_ERROR */
    uint32_t ret0;
    uint32_t ret1;
    uint8_t  payload[SH_RESULT_PAYLOAD_MAX];
} sh_result_t;

/* /sys/state record — C writes, JS reads (each cp_step) */
typedef struct {
    uint32_t sup_state;    /* SUP_UNINITIALIZED, SUP_REPL, etc. */
    uint32_t yield_reason; /* YIELD_BUDGET, YIELD_SLEEP, etc. */
    uint32_t yield_arg;    /* e.g., sleep duration ms */
    uint32_t frame_count;
    uint32_t vm_depth;     /* pystack depth / nesting level */
    uint32_t pending_call; /* SH_IDLE if none, else call_nr */
} sh_state_t;

/* /sys/events entry — JS appends, supervisor drains */
typedef struct {
    uint16_t event_type;   /* SH_EVT_* */
    uint16_t event_data;   /* type-specific */
    uint32_t arg;          /* type-specific */
} sh_event_t;

#define SH_EVENT_SIZE    8
#define SH_EVENT_MAX     32  /* ring capacity */

/* ------------------------------------------------------------------ */
/* Event types (JS → Python, via /sys/events)                          */
/* ------------------------------------------------------------------ */

#define SH_EVT_NONE          0x00
#define SH_EVT_KEY_DOWN      0x01  /* data=keycode, arg=modifiers       */
#define SH_EVT_KEY_UP        0x02  /* data=keycode, arg=modifiers       */
#define SH_EVT_TIMER_FIRE    0x10  /* data=timer_id, arg=0              */
#define SH_EVT_FETCH_DONE    0x11  /* data=request_id, arg=http_status  */
#define SH_EVT_HW_CHANGE     0x20  /* data=hal_type, arg=pin/channel    */
#define SH_EVT_PERSIST_DONE  0x30  /* data=0, arg=0                     */
#define SH_EVT_RESIZE        0x40  /* data=width, arg=height            */

/* ------------------------------------------------------------------ */
/* C-side API                                                          */
/* ------------------------------------------------------------------ */

/* Init: open /sys/ fd endpoints */
void sh_init(void);

/* fwip shared buffer address (linear memory, no WASI fds) */
uintptr_t sh_fwip_addr(void);

/* Issue a semihosting call.  Writes to /sys/call, returns immediately.
 * Caller must then yield (YIELD_IO_WAIT) and check sh_poll() on
 * the next cp_step(). */
void sh_call(uint32_t call_nr, uint32_t arg0, uint32_t arg1,
             uint32_t arg2, uint32_t arg3,
             const void *payload, uint32_t payload_len);

/* Synchronous call for simple cases (clock, errno, etc.) that the
 * host can fulfill instantly.  Writes /sys/call, reads /sys/result
 * in the same cp_step() — no yield needed.  Only valid for calls
 * that the MEMFS onSyscall callback handles inline. */
int sh_call_sync(uint32_t call_nr, uint32_t arg0, uint32_t arg1,
                 uint32_t arg2, uint32_t arg3);

/* Check if a pending async call has been fulfilled.
 * Returns: SH_IDLE (no pending), SH_PENDING, SH_FULFILLED, SH_ERROR */
int sh_poll(void);

/* Read result after SH_FULFILLED.  Resets status to SH_IDLE. */
void sh_read_result(sh_result_t *out);

/* Drain /sys/events via WASI fd.  NOT safe in cp_step() hot loop —
 * WASI fd I/O during cp_step corrupts WASM linear memory.
 * Use sh_drain_event_ring() instead for per-frame event processing. */
void sh_drain_events(void);

/* ---- Linear-memory event ring (safe for per-frame use) ---- */

/* Ring buffer layout in linear memory:
 *   [write_idx:u32] [read_idx:u32] [entries: SH_EVENT_MAX * sh_event_t]
 * JS writes events and advances write_idx.
 * C reads events and advances read_idx.
 * No WASI fd calls — direct memory access only. */
#define SH_EVENT_RING_HEADER  8  /* write_idx + read_idx */
#define SH_EVENT_RING_SIZE    (SH_EVENT_RING_HEADER + SH_EVENT_MAX * SH_EVENT_SIZE)

/* Drain events from the linear-memory ring.  Called in hal_step(). */
void sh_drain_event_ring(void);

/* Export VM state to linear memory.  Called at end of cp_step().
 * JS reads directly via sh_state_addr() — no WASI fd needed. */
void sh_export_state(uint32_t sup_state, uint32_t yield_reason,
                     uint32_t yield_arg, uint32_t frame_count,
                     uint32_t vm_depth);

/* ------------------------------------------------------------------ */
/* fwip — firmware package installer (linear-memory shared buffer)     */
/*                                                                     */
/* Python writes a package name + command to _fwip_buf.                */
/* JS reads it via sh_fwip_addr(), does the fetch, writes status back. */
/* Python polls _fwip_buf.state each yield until DONE or ERROR.        */
/*                                                                     */
/* Layout:                                                             */
/*   [command:u8] [state:u8] [reserved:u16]                            */
/*   [installed_count:u16] [status_len:u16]                            */
/*   [name: 128 bytes, NUL-terminated]                                 */
/*   [status: 128 bytes, NUL-terminated]                               */
/*   = 264 bytes total                                                 */
/* ------------------------------------------------------------------ */

#define FWIP_BUF_SIZE     264
#define FWIP_NAME_MAX     128
#define FWIP_STATUS_MAX   128

/* Commands (Python → JS) */
#define FWIP_CMD_NONE     0
#define FWIP_CMD_INSTALL  1
#define FWIP_CMD_REMOVE   2
#define FWIP_CMD_LIST     3

/* States (JS → Python) */
#define FWIP_STATE_IDLE       0
#define FWIP_STATE_PENDING    1   /* JS acknowledged, working */
#define FWIP_STATE_PROGRESS   2   /* status string updated */
#define FWIP_STATE_DONE       3
#define FWIP_STATE_ERROR      4

typedef struct {
    uint8_t  command;                   /* FWIP_CMD_* */
    uint8_t  state;                     /* FWIP_STATE_* */
    uint16_t reserved;
    uint16_t installed_count;           /* number of packages installed */
    uint16_t status_len;                /* length of status string */
    char     name[FWIP_NAME_MAX];       /* package name (Python writes) */
    char     status[FWIP_STATUS_MAX];   /* status message (JS writes) */
} fwip_buf_t;

#endif /* WASM_SEMIHOSTING_H */
