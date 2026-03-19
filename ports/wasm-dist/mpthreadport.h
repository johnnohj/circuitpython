/*
 * mpthreadport.h — WASM-dist thread port
 *
 * Each WebWorker is a separate WASM instance with its own GIL.
 * mp_thread_create() serializes spawn requests to /dev/bc_out so
 * PythonHost.js can launch a new Worker. No pthreads, no SharedArrayBuffer.
 *
 * Mutex operations are no-ops: each worker runs single-threaded Python,
 * so intra-worker mutual exclusion is never needed.
 */
#pragma once

#include <stdint.h>

/* Stub mutex type — no real locking needed inside a single WASM worker */
typedef struct { volatile int locked; } mp_thread_mutex_t;
typedef struct { volatile int locked; int count; } mp_thread_recursive_mutex_t;
