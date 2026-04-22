/*
 * supervisor/port_memory.c — Port-owned memory instance.
 *
 * Single static allocation for all port-owned memory.  The port is
 * supreme: this is THE memory that everything else lives in.
 *
 * JS can inspect the entire layout via cp_port_memory_addr/size exports.
 */

#define PORT_MEMORY_IMPL  /* prevent compat alias macros */
#include "supervisor/port_memory.h"

/* The single instance — all port-owned statics in one place. */
port_memory_t port_mem = {
    .sup_state = 0,  /* SUP_UNINITIALIZED */
    .sup_ctx0_is_code = false,
    .sup_code_header_printed = false,
    .sup_debug_enabled = true,
    .wasm_cli_mode = false,
    .frame_count = 0,
    .js_now_ms = 0,
    .active_ctx_id = -1,
};

/* ------------------------------------------------------------------ */
/* WASM exports — JS can inspect port memory layout                    */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_port_memory_addr")))
uintptr_t cp_port_memory_addr(void) {
    return (uintptr_t)&port_mem;
}

__attribute__((export_name("cp_port_memory_size")))
uint32_t cp_port_memory_size(void) {
    return (uint32_t)sizeof(port_memory_t);
}
