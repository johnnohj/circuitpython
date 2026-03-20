/*
 * opfs_state.h — State checkpoint/restore for OPFS-backed execution
 *
 * Saves and loads VM state (GC heap, mp_state_ctx, pystack) to files
 * via POSIX read/write (mapped to WASI fd_read/fd_write, and ultimately
 * to OPFS FileSystemSyncAccessHandle in the browser).
 *
 * All state files are loaded at fixed WASM linear memory addresses for
 * pointer stability — the same trick used by memfs_state.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

// File header for state files
#define OPFS_STATE_MAGIC 0x4F505653  // "OPVS"
#define OPFS_STATE_VERSION 1

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t data_size;       // bytes of payload after header
    uint32_t wasm_base_addr;  // WASM linear memory address of the data
    uint32_t reserved[4];     // pad to 32 bytes
} opfs_state_header_t;

// Execution state metadata (written to /state/exec)
typedef enum {
    OPFS_EXEC_IDLE = 0,
    OPFS_EXEC_RUNNING = 1,
    OPFS_EXEC_PAUSED = 2,
    OPFS_EXEC_COMPLETED = 3,
    OPFS_EXEC_ERROR = 4,
} opfs_exec_state_kind_t;

typedef enum {
    OPFS_PAUSE_NONE = 0,
    OPFS_PAUSE_BUDGET = 1,
    OPFS_PAUSE_SLEEP = 2,
    OPFS_PAUSE_IO_WAIT = 3,
    OPFS_PAUSE_EXCEPTION = 4,
} opfs_pause_reason_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t state;           // opfs_exec_state_kind_t
    uint32_t pause_reason;    // opfs_pause_reason_t
    uint32_t step_count;
    uint32_t sleep_until_ms;
    uint32_t reserved[2];
} opfs_exec_meta_t;

// ---- State save/load API ----

// Save the GC heap (ATB + pool) to state_dir/heap
int opfs_save_heap(const char *state_dir);

// Load the GC heap from state_dir/heap into the fixed heap buffer
int opfs_load_heap(const char *state_dir);

// Save mp_state_ctx to state_dir/vm
int opfs_save_vm(const char *state_dir);

// Load mp_state_ctx from state_dir/vm
int opfs_load_vm(const char *state_dir);

// Save the pystack (used portion) to state_dir/pystack
int opfs_save_pystack(const char *state_dir);

// Load the pystack from state_dir/pystack
int opfs_load_pystack(const char *state_dir);

// Save execution metadata to state_dir/exec
int opfs_save_exec(const char *state_dir, const opfs_exec_meta_t *meta);

// Load execution metadata from state_dir/exec
int opfs_load_exec(const char *state_dir, opfs_exec_meta_t *meta);

// ---- Executor API ----

// Initialize: compile source, set up initial state, save to files
int opfs_executor_init(const char *src, size_t len, const char *state_dir);

// Execute one step: load state, run N bytecodes, save state
// Returns: opfs_pause_reason_t
int opfs_executor_step(const char *state_dir);

// Get the sleep duration (valid after step returns OPFS_PAUSE_SLEEP)
uint32_t opfs_executor_sleep_ms(void);
