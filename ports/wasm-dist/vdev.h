/*
 * vdev.h — Virtual device driver for the WASM-dist Python host
 *
 * Emscripten MEMFS is the virtual hardware bus.  All Python I/O is routed
 * through files in /dev/ instead of POSIX fd 0/1/2, so that both Python
 * (via open/read/write) and JavaScript (via Module.FS) can observe it.
 *
 * File layout created by vdev_init():
 *
 *   /dev/stdin      — JS writes; Python reads (keyboard / request input)
 *   /dev/stdout     — Python print() writes; JS reads + broadcasts
 *   /dev/stderr     — Python error writes; JS routes to exception worker
 *   /dev/time       — read() returns current ms as null-terminated decimal
 *   /dev/interrupt  — JS writes 0x03 to raise KeyboardInterrupt
 *   /dev/bc_out     — Python writes JSON lines; mp_js_hook() drains → BC
 *   /dev/bc_in      — JS writes incoming BC messages; Python reads
 *
 *   /mem/heap       — GC heap binary checkpoint (written on suspend)
 *   /mem/pystack    — pystack buffer checkpoint (written on suspend)
 *
 *   /flash/         — Python source + compiled bytecode (.py / .mpy)
 *   /flash/lib/     — sys.path entry
 *
 *   /state/snapshot.json — pre-run globals dict (JSON)
 *   /state/result.json   — {delta,stdout,stderr,aborted,duration_ms,frames:[]}
 *
 *   /proc/self.json  — {workerId, role}
 *   /proc/workers.json — active worker registry
 *
 *   /debug/trace.json      — NDJSON sys.settrace events
 *   /debug/semihosting.log — semihosting writes
 */
#pragma once

#include <stddef.h>

/* Create all virtual device directories and files.
 * Must be called after mp_init() and before any Python execution. */
void vdev_init(void);

/* Refresh /dev/time with the current ms timestamp (called periodically). */
void vdev_update_time(void);

/* Drain /dev/bc_out: read all pending JSON lines, clear the file.
 * Returns the number of lines drained.
 * Called from mp_js_hook() every MICROPY_VM_HOOK_COUNT bytecodes. */
int vdev_bc_out_drain(char *buf, size_t buf_len);

/* Append one JSON line to /dev/bc_in so Python can read it.
 * Called from JavaScript via the exported _vdev_bc_in_write symbol. */
void vdev_bc_in_write(const char *json_line);

/* Snapshot /dev/stdout into an internal buffer for result.json.
 * Clears /dev/stdout after reading.
 * Returns a pointer to a static buffer; valid until next call. */
const char *vdev_stdout_snapshot(void);

/* Return the last snapshotted stdout length. */
size_t vdev_stdout_snapshot_len(void);

/* File descriptor for /dev/py_stdout capture device. */
extern int vdev_stdout_fd;

/* File descriptor for /dev/py_stderr capture device. */
extern int vdev_stderr_fd;

/* File descriptor for /dev/stdin, opened at init for mphalport. */
extern int vdev_stdin_fd;
