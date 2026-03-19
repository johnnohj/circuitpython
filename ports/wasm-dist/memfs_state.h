/*
 * memfs_state.h — Python execution state serialization to Emscripten MEMFS
 *
 * Provides the checkpoint/diff machinery that makes Python state portable:
 *
 *   Before each run():  mp_memfs_snapshot_globals() → /state/snapshot.json
 *   After each run():   mp_memfs_finish_run()        → /state/result.json
 *
 * result.json schema:
 *   {
 *     "delta":       { "varname": <json_value>, ... },  // changed globals
 *     "stdout":      "...",                              // captured print()
 *     "stderr":      "...",                              // captured errors
 *     "aborted":     false,
 *     "duration_ms": 47,
 *     "frames":      []   // reserved for phase-2 call-stack serialization
 *   }
 *
 * JSON type mapping (mp_obj_to_json_str):
 *   None    → null
 *   bool    → true / false
 *   int     → number
 *   float   → number
 *   str     → JSON string (escaped)
 *   list    → JSON array  (recursive, max depth 8)
 *   tuple   → JSON array  (recursive, max depth 8)
 *   dict    → JSON object (recursive, max depth 8)
 *   other   → {"__type__":"<classname>","__repr__":"<repr>"}
 */
#pragma once

#include "py/obj.h"
#include "py/misc.h"  /* vstr_t is in misc.h, not a separate vstr.h */

/* Serialize current module globals to /state/snapshot.json.
 * Call before each run() to establish the diff baseline. */
void mp_memfs_snapshot_globals(void);

/* Diff current globals against snapshot, capture /dev/stdout,
 * and write /state/result.json.
 * Call after run() returns (normal or aborted). */
void mp_memfs_finish_run(bool aborted, uint32_t start_ms);

/* Append a NDJSON trace event line to /debug/trace.json.
 * Called by the sys.settrace Python hook. */
void mp_memfs_trace_append(const char *json_line);

/* Write GC heap + mp_state_ctx to /mem/heap, /mem/mp_state_ctx, /mem/state.json. */
void mp_memfs_checkpoint_vm(void);

/* Restore GC heap + mp_state_ctx from /mem/. */
void mp_memfs_restore_vm(void);

/* Convert a Python object to a JSON string in vstr.
 * max_depth limits recursion for containers (recommend 8). */
void mp_obj_to_json_str(mp_obj_t obj, vstr_t *out, int max_depth);
