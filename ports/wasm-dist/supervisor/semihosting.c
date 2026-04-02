/*
 * supervisor/semihosting.c — WASM semihosting via MEMFS.
 *
 * The "trap" mechanism: instead of BKPT/EBREAK, C writes a binary
 * request record to /sys/call (a WASI fd backed by wasi-memfs.js).
 * JS sees the write via onSyscall callback, fulfills the request,
 * writes the response to /sys/result.
 *
 * Two modes:
 *   - Synchronous: sh_call_sync() — write + read in one go.
 *     For fast host operations (clock, errno) that the MEMFS
 *     callback handles inline before fd_write returns.
 *
 *   - Asynchronous: sh_call() + yield(YIELD_IO_WAIT) + sh_poll().
 *     For operations that need real async work (fetch, timer, I2C).
 *     C writes the request, yields to JS.  JS fulfills at its
 *     leisure, writes /sys/result.  Next cp_step(), supervisor
 *     calls sh_poll() → SH_FULFILLED → sh_read_result().
 *
 * Event injection: JS appends sh_event_t records to /sys/events.
 * Supervisor drains them in hal_step() (phase 1 of cp_step).
 * This replaces cp_push_key() — keyboard input becomes
 * SH_EVT_KEY_DOWN events through the same channel as timers,
 * fetch completions, and hardware changes.
 *
 * VM state export: supervisor writes sh_state_t to /sys/state
 * at the end of each cp_step().  JS can read this without calling
 * any WASM export — just read from its in-memory Map.  This lets
 * JS make scheduling decisions: skip cp_step() during long sleeps,
 * batch hardware updates, show VM state in devtools.
 */

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "supervisor/semihosting.h"

/* ------------------------------------------------------------------ */
/* File descriptors for /sys/ endpoints                                */
/* ------------------------------------------------------------------ */

static int _call_fd   = -1;   /* /sys/call   — C writes, JS reads    */
static int _result_fd = -1;   /* /sys/result — JS writes, C reads    */
static int _events_fd = -1;   /* /sys/events — JS appends, C drains  */
static int _state_fd  = -1;   /* /sys/state  — C writes, JS reads    */

/* ------------------------------------------------------------------ */
/* sh_init — open /sys/ fd endpoints                                   */
/* ------------------------------------------------------------------ */

void sh_init(void) {
    mkdir("/sys", 0755);

    /* Per-frame data (state, events) uses direct linear memory —
     * NOT WASI fds.  See pitfall_wasi_fd_write_corruption.md.
     * Static globals (_export_buf, _event_ring) are zero-initialized.
     *
     * The /sys/call and /sys/result fds are for low-frequency
     * semihosting requests (fetch, timer, persist).  They'll be
     * opened lazily when a semihosting call is actually issued. */
}

/* ------------------------------------------------------------------ */
/* sh_call — issue async semihosting request                           */
/* ------------------------------------------------------------------ */

void sh_call(uint32_t call_nr, uint32_t arg0, uint32_t arg1,
             uint32_t arg2, uint32_t arg3,
             const void *payload, uint32_t payload_len) {

    sh_call_t req = {
        .call_nr = call_nr,
        .status  = SH_PENDING,
        .arg0    = arg0,
        .arg1    = arg1,
        .arg2    = arg2,
        .arg3    = arg3,
    };

    if (payload && payload_len > 0) {
        uint32_t n = payload_len < SH_PAYLOAD_MAX ? payload_len : SH_PAYLOAD_MAX;
        memcpy(req.payload, payload, n);
    }

    lseek(_call_fd, 0, SEEK_SET);
    write(_call_fd, &req, sizeof(req));

    /* Caller should now yield with YIELD_IO_WAIT. */
}

/* ------------------------------------------------------------------ */
/* sh_call_sync — issue + read in one step (for fast host ops)         */
/*                                                                     */
/* The MEMFS onSyscall callback can handle some calls inline:          */
/* it sees the fd_write to /sys/call, fulfills immediately, writes     */
/* /sys/result before fd_write returns.  C can then read the result    */
/* without yielding.                                                   */
/* ------------------------------------------------------------------ */

int sh_call_sync(uint32_t call_nr, uint32_t arg0, uint32_t arg1,
                 uint32_t arg2, uint32_t arg3) {

    sh_call(call_nr, arg0, arg1, arg2, arg3, NULL, 0);

    /* Read result immediately — host handled inline */
    sh_result_t res;
    lseek(_result_fd, 0, SEEK_SET);
    ssize_t n = read(_result_fd, &res, sizeof(res));

    if (n < (ssize_t)sizeof(res) || res.status != SH_FULFILLED) {
        return -1;
    }

    /* Clear pending state */
    sh_call_t idle = { .call_nr = 0, .status = SH_IDLE };
    lseek(_call_fd, 0, SEEK_SET);
    write(_call_fd, &idle, sizeof(idle));

    return (int)res.ret0;
}

/* ------------------------------------------------------------------ */
/* sh_poll — check if async call is fulfilled                          */
/* ------------------------------------------------------------------ */

int sh_poll(void) {
    sh_result_t res;
    lseek(_result_fd, 0, SEEK_SET);
    ssize_t n = read(_result_fd, &res, sizeof(res));

    if (n < (ssize_t)sizeof(uint32_t) * 2) {
        return SH_IDLE;
    }

    return (int)res.status;
}

/* ------------------------------------------------------------------ */
/* sh_read_result — consume fulfilled result, reset to idle            */
/* ------------------------------------------------------------------ */

void sh_read_result(sh_result_t *out) {
    lseek(_result_fd, 0, SEEK_SET);
    read(_result_fd, out, sizeof(*out));

    /* Reset call record to idle */
    sh_call_t idle = { .call_nr = 0, .status = SH_IDLE };
    lseek(_call_fd, 0, SEEK_SET);
    write(_call_fd, &idle, sizeof(idle));
}

/* ------------------------------------------------------------------ */
/* sh_drain_events — process JS→Python event queue                     */
/*                                                                     */
/* Called during hal_step() (phase 1 of cp_step).  Reads all pending  */
/* events from /sys/events, dispatches each, then truncates.           */
/* ------------------------------------------------------------------ */

/* Weak — supervisor or board code can override to handle events */
__attribute__((weak))
void sh_on_event(const sh_event_t *evt) {
    (void)evt;
}

void sh_drain_events(void) {
    sh_event_t events[SH_EVENT_MAX];

    lseek(_events_fd, 0, SEEK_SET);
    ssize_t n = read(_events_fd, events, sizeof(events));

    if (n <= 0) {
        return;
    }

    int count = n / SH_EVENT_SIZE;
    for (int i = 0; i < count; i++) {
        if (events[i].event_type == SH_EVT_NONE) {
            continue;
        }
        sh_on_event(&events[i]);
    }

    /* Clear by writing an empty buffer at offset 0.
     * We avoid ftruncate() since wasi-memfs doesn't implement
     * fd_filestat_set_size.  Instead, just reset the fd offset
     * so the next read sees nothing new — JS overwrites the
     * entire file content on each pushEvent() batch anyway. */
    lseek(_events_fd, 0, SEEK_SET);
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
/* fwip shared buffer — linear memory, no WASI fds                     */
/* ------------------------------------------------------------------ */

static fwip_buf_t _fwip_buf;

__attribute__((export_name("sh_fwip_addr")))
uintptr_t sh_fwip_addr(void) {
    return (uintptr_t)&_fwip_buf;
}

/* ------------------------------------------------------------------ */
/* sh_export_state — write VM state to linear memory                   */
/*                                                                     */
/* JS never needs to call a WASM export to know the VM state.          */
/* It reads /sys/state from its in-memory Map directly.                */
/* ------------------------------------------------------------------ */

static sh_state_t _export_buf;

/* JS can read state directly from linear memory via this pointer.
 * We avoid WASI fd_write during cp_step — it corrupts memory when
 * called every frame (likely a memory.buffer detach issue in the
 * WASI fd_write path after GC triggers memory.grow). */
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
