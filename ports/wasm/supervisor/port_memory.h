/*
 * supervisor/port_memory.h — Port-owned memory layout.
 *
 * The port is supreme: it owns all static memory and allocates regions
 * to the supervisor, VM, and context system.  This header defines the
 * layout and provides accessors.
 *
 * All regions are static arrays at fixed addresses in WASM linear
 * memory (no custom allocator — same model as DTCM on ARM ports).
 * Consumers receive pointers; the port owns the storage.
 *
 * JS can inspect the layout via exported cp_port_memory_addr/size.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "supervisor/context.h"

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#ifndef WASM_GC_HEAP_SIZE
#define WASM_GC_HEAP_SIZE (512 * 1024)
#endif

#ifndef WASM_FRAME_BUDGET_MS
#define WASM_FRAME_BUDGET_MS 13
#endif

#define PORT_INPUT_BUF_SIZE 4096

/* ------------------------------------------------------------------ */
/* Port memory layout                                                  */
/*                                                                     */
/* One struct, one region.  The port owns it; everyone else gets        */
/* pointers.  Ordering: large aligned regions first (GC heap,          */
/* pystacks), then smaller structures.                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    /* ── VM allocation: GC heap ── */
    char gc_heap[WASM_GC_HEAP_SIZE];

    /* ── Context system: per-context pystacks ── */
    uint8_t pystacks[CP_MAX_CONTEXTS][CP_CTX_PYSTACK_SIZE]
        __attribute__((aligned(sizeof(void *))));

    /* ── Context system: metadata + VM state ── */
    cp_context_meta_t ctx_meta[CP_MAX_CONTEXTS];
    cp_context_vm_t   ctx_vm[CP_MAX_CONTEXTS];

    /* ── Context system: wake registrations ── */
    cp_wake_reg_t wake_regs[CP_MAX_WAKE_REGS];

    /* ── Supervisor: shared input buffer (JS → C string passing) ── */
    char input_buf[PORT_INPUT_BUF_SIZE];

    /* ── Supervisor: state ── */
    int      sup_state;
    bool     sup_ctx0_is_code;
    bool     sup_code_header_printed;
    bool     sup_debug_enabled;
    bool     wasm_cli_mode;
    uint32_t frame_count;

    /* ── Port: JS time source ── */
    volatile uint64_t js_now_ms;

    /* ── Context system: active context id ── */
    int active_ctx_id;

    /* ── HAL: dirty flags for change detection ──
     *
     * JS sets these bitmasks when it writes to /hal/ endpoints via
     * updateHardwareState().  C reads and clears them in hal_step().
     * Bit N = pin N was written since last hal_step().
     *
     * This keeps all HAL data in MEMFS (read/written via fd I/O).
     * The dirty flags are the only new thing in port_mem — they're
     * the "interrupt pending" bits that make the HAL self-aware.
     */
    volatile uint64_t hal_gpio_dirty;
    volatile uint64_t hal_analog_dirty;
    volatile uint64_t hal_pwm_dirty;
    volatile uint32_t hal_neopixel_dirty;  /* 1 = any neopixel data changed */
    uint32_t hal_change_count;             /* monotonic, incremented on any change */

    /* ── HAL: per-pin metadata ──
     *
     * Self-describing pin state visible to both C and JS.
     * role:     what peripheral Python claimed this pin as (set by common-hal)
     * flags:    report/ack bitmask (JS_WROTE, C_WROTE, C_READ)
     * category: what the board designed this pin for (set at init from board table)
     */
    struct {
        uint8_t role;       /* HAL_ROLE_* */
        uint8_t flags;      /* HAL_FLAG_* */
        uint8_t category;   /* HAL_CAT_*  */
        uint8_t latched;    /* Port-latched input value (like IRQ capture) */
    } pin_meta[64];         /* 256 bytes */
} port_memory_t;

/* The single instance — defined in port_memory.c */
extern port_memory_t port_mem;

/* ------------------------------------------------------------------ */
/* Convenience accessors                                               */
/* ------------------------------------------------------------------ */

/* GC heap region */
static inline char *port_gc_heap(void) { return port_mem.gc_heap; }
static inline size_t port_gc_heap_size(void) { return WASM_GC_HEAP_SIZE; }

/* Pystack region for a context */
static inline uint8_t *port_pystack(int id) { return port_mem.pystacks[id]; }
static inline uint8_t *port_pystack_end(int id) {
    return port_mem.pystacks[id] + CP_CTX_PYSTACK_SIZE;
}

/* Context metadata */
static inline cp_context_meta_t *port_ctx_meta(int id) {
    return &port_mem.ctx_meta[id];
}
static inline cp_context_vm_t *port_ctx_vm(int id) {
    return &port_mem.ctx_vm[id];
}

/* Wake registrations */
static inline cp_wake_reg_t *port_wake_regs(void) {
    return port_mem.wake_regs;
}

/* Input buffer */
static inline char *port_input_buf(void) { return port_mem.input_buf; }
static inline size_t port_input_buf_size(void) { return PORT_INPUT_BUF_SIZE; }

/* ------------------------------------------------------------------ */
/* Backward-compat aliases                                             */
/*                                                                     */
/* Code throughout the port historically used these as extern globals.  */
/* Now they're macros that resolve to port_mem fields.                  */
/* Guard: port_memory.c must NOT see these while initializing port_mem. */
/* ------------------------------------------------------------------ */

#ifndef PORT_MEMORY_IMPL
#define wasm_cli_mode       port_mem.wasm_cli_mode
#define wasm_js_now_ms      port_mem.js_now_ms
#endif
