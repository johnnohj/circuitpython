// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/port_memory.h by CircuitPython contributors
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/port_memory.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/port_memory.h — Port-owned memory layout for MEMFS-in-linear-memory.
//
// The port owns all static memory and provides regions to the supervisor,
// VM, and context system.  Every named region is also a MEMFS file:
// C accesses via pointer, JS accesses via memfs — same bytes in WASM
// linear memory.
//
// At init, each region is registered with MEMFS via the memfs_register()
// WASM import.  No copy, no alias — the MEMFS file IS the struct field.
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer, port owns memory)
//   design/behavior/01-hardware-init.md     (GPIO layout, 32 pins)
//   design/behavior/02-serial-and-stack.md  (serial rings, stack)
//   design/behavior/05-vm-lifecycle.md      (GC heap, pystack)
//
// Memory map (MEMFS paths → struct fields):
//
//   /port/state          port_state_t       Port state machine
//   /port/event_ring     event records      JS→C event notifications
//   /hal/gpio            12 bytes × 32      GPIO pin slots
//   /hal/analog          4 bytes × 32       ADC values
//   /hal/serial/rx       ring buffer        Serial input (JS→C)
//   /hal/serial/tx       ring buffer        Serial output (C→JS)
//   /py/heap             GC heap            MicroPython garbage collector
//   /py/ctx/N/pystack    per-context        Python execution stacks
//   /py/ctx/meta         metadata array     Context scheduling state
//   /py/input            buffer             JS→C string passing

#ifndef PORT_MEMORY_H
#define PORT_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "port/constants.h"

// ── Configuration ──

#ifndef PORT_GC_HEAP_SIZE
#define PORT_GC_HEAP_SIZE       (512 * 1024)    // 512K GC heap
#endif

#define PORT_MAX_CONTEXTS       8               // max concurrent Python contexts
#define PORT_PYSTACK_SIZE       (8 * 1024)      // 8K per context pystack
#define PORT_INPUT_BUF_SIZE     4096            // JS→C string passing buffer
#define PORT_EVENT_RING_SIZE    512             // bytes for event ring
#define PORT_SERIAL_BUF_SIZE    4096            // bytes per serial direction
#define PORT_ANALOG_SLOT_SIZE   4              // bytes per analog channel
#define PORT_NEOPIXEL_HEADER    4              // [pin:u8][enabled:u8][num_bytes:u16le]
#define PORT_NEOPIXEL_MAX_BYTES (64 * 4)       // 64 pixels × 4 bytes (RGBW)
#define PORT_NEOPIXEL_REGION_SIZE (PORT_NEOPIXEL_HEADER + PORT_NEOPIXEL_MAX_BYTES)

// ── Port state — the port's own execution state ──
// Laid out for direct JS reads from linear memory.
// Byte offsets are defined in constants.h (PS_* macros).

typedef struct {
    uint32_t phase;         // current lifecycle phase
    uint32_t sub_phase;     // within-phase position
    uint32_t frame_count;   // monotonic frame counter
    uint32_t now_us;        // current frame timestamp (us, snapshot from JS)
    uint32_t budget_us;     // frame budget (us)
    uint32_t elapsed_us;    // elapsed time this frame
    uint32_t status;        // last frame result code (RC_*)
    uint32_t flags;         // port flags (PF_*)
} port_state_t;

// ── Event ring — JS→C notification channel ──
//
// JS writes events (8 bytes each) into this ring in linear memory.
// C drains them each frame.  The event carries a type + pin/channel
// identifier — the actual state is already in the HAL MEMFS region
// (JS wrote it before pushing the event).

typedef struct {
    uint8_t  type;      // event type (EVT_*)
    uint8_t  pin;       // pin/channel index
    uint16_t arg;       // type-specific argument
    uint32_t data;      // type-specific data
} port_event_t;

// Ring header (first 8 bytes of the event ring region)
typedef struct {
    uint32_t write_head;   // JS increments (byte offset into ring data)
    uint32_t read_head;    // C increments (byte offset into ring data)
} event_ring_header_t;

// Usable ring data starts after the header
#define EVENT_RING_DATA_SIZE  (PORT_EVENT_RING_SIZE - sizeof(event_ring_header_t))
#define EVENT_RING_CAPACITY   (EVENT_RING_DATA_SIZE / sizeof(port_event_t))

// ── Serial ring buffers ──
// Simple ring: first 8 bytes are head/tail uint32, rest is data.

typedef struct {
    uint32_t write_head;
    uint32_t read_head;
    uint8_t  data[PORT_SERIAL_BUF_SIZE - 8];
} serial_ring_t;

// ── Context metadata ──
// Per-context scheduling state, readable by JS from linear memory.

// Context status values
#define CTX_FREE      0   // slot available
#define CTX_IDLE      1   // created but no code loaded
#define CTX_RUNNABLE  2   // ready to run
#define CTX_RUNNING   3   // currently executing
#define CTX_YIELDED   4   // yielded (budget exhausted)
#define CTX_SLEEPING  5   // waiting for deadline
#define CTX_DONE      6   // execution completed

typedef struct {
    uint8_t  status;        // CTX_*
    uint8_t  priority;      // 0 = highest
    uint16_t reserved;
    uint32_t pystack_used;  // bytes used in this context's pystack
    uint32_t delay_until_lo; // sleep deadline (low 32 bits, ms)
    uint32_t delay_until_hi; // sleep deadline (high 32 bits, ms)
} ctx_meta_t;

// ── VM abort reasons ──
// Why the VM was halted (readable by JS to decide when to re-enter).

#define VM_ABORT_NONE        0  // normal completion
#define VM_ABORT_BUDGET      1  // frame budget expired
#define VM_ABORT_WFE         2  // cooperative yield (port_idle_until_interrupt)

// ── VM flags ──

#define VM_FLAG_INITIALIZED  (1 << 0)  // mp_init() has been called
#define VM_FLAG_RUNNING      (1 << 1)  // VM is executing bytecode
#define VM_FLAG_HAS_CODE     (1 << 2)  // code.py or REPL code loaded

// ── The port memory layout ──
// One struct, everything MEMFS-registered.  The port owns it;
// everyone else gets pointers.
//
// Ordering: large aligned regions first (GC heap, pystacks),
// then smaller structures.

typedef struct {
    // ── VM allocation: GC heap ──
    // /py/heap — MicroPython garbage collector
    uint8_t gc_heap[PORT_GC_HEAP_SIZE]
        __attribute__((aligned(sizeof(void *))));

    // ── Context system: per-context pystacks ──
    // /py/ctx/N/pystack — Python execution stacks
    uint8_t pystacks[PORT_MAX_CONTEXTS][PORT_PYSTACK_SIZE]
        __attribute__((aligned(sizeof(void *))));

    // ── Context system: metadata ──
    // /py/ctx/meta — scheduling state array
    ctx_meta_t ctx_meta[PORT_MAX_CONTEXTS];

    // ── Port core ──

    // /port/state — port state machine
    port_state_t state;

    // /port/event_ring — JS→C events
    uint8_t event_ring[PORT_EVENT_RING_SIZE];

    // ── HAL ──

    // /hal/gpio — GPIO pin slots (12 bytes × 32 pins)
    uint8_t hal_gpio[GPIO_MAX_PINS * GPIO_SLOT_SIZE];

    // /hal/analog — ADC values (4 bytes × 32 channels)
    uint8_t hal_analog[GPIO_MAX_PINS * PORT_ANALOG_SLOT_SIZE];

    // /hal/neopixel — NeoPixel pixel data
    // Header: [pin:u8] [enabled:u8] [num_bytes:u16le]
    // Data: GRB or GRBW bytes, up to PORT_NEOPIXEL_MAX_BYTES
    uint8_t hal_neopixel[PORT_NEOPIXEL_REGION_SIZE];

    // /hal/serial/rx — serial input (JS→C)
    serial_ring_t serial_rx;

    // /hal/serial/tx — serial output (C→JS)
    serial_ring_t serial_tx;

    // ── HAL dirty flags ──
    // Bitmask: bit N = pin N changed since last hal_step().
    // JS sets these when writing to /hal/ endpoints.
    // C reads and clears them in hal_step().
    volatile uint32_t hal_gpio_dirty;     // 32 pins → 32 bits
    volatile uint32_t hal_analog_dirty;   // 32 channels → 32 bits
    volatile uint32_t hal_change_count;   // monotonic, any change

    // ── Supervisor state ──
    // Lifecycle phase is in state.phase (port_step.h).
    // sup_state, sup_ctx0_is_code, sup_code_header_printed removed —
    // replaced by port_step lifecycle.
    bool     debug_enabled;         // enable debug output
    bool     cli_mode;              // true when running as Node CLI

    // ── Context system: active context ──
    int      active_ctx_id;         // currently active context (-1 = none)

    // ── JS→C string passing buffer ──
    // /py/input
    char input_buf[PORT_INPUT_BUF_SIZE];

    // ── VM state ──
    uint32_t vm_active_ctx;    // currently active context id (for JS)
    uint32_t vm_flags;         // VM_FLAG_*
    uint32_t vm_abort_reason;  // VM_ABORT_* (why VM was halted)

    // ── Cooperative yield ──
    // port_idle_until_interrupt stores the wakeup deadline here.
    // JS reads this to know when to schedule the next frame.
    // 0 = no deadline (wake on next event).
    uint32_t wakeup_ms;

    // ── Time source ──
    // JS writes performance.now() here before each frame.
    // Volatile: written by JS, read by C, never cached.
    volatile uint64_t js_now_ms;
} port_memory_t;

// The single instance — defined in port_memory.c
extern port_memory_t port_mem;

// ── Lifecycle ──

// Register all regions with MEMFS (call once at init)
void port_memory_register(void);

// Zero all memory (regions stay registered — same addresses)
void port_memory_reset(void);

// ── Convenience accessors ──

// GPIO slot pointer for pin N
static inline uint8_t *gpio_slot(int pin) {
    return &port_mem.hal_gpio[pin * GPIO_SLOT_SIZE];
}

// Analog slot pointer for channel N
static inline uint8_t *analog_slot(int ch) {
    return &port_mem.hal_analog[ch * PORT_ANALOG_SLOT_SIZE];
}

// Event ring header
static inline event_ring_header_t *event_ring_hdr(void) {
    return (event_ring_header_t *)port_mem.event_ring;
}

// Event ring data (after header)
static inline port_event_t *event_ring_data(void) {
    return (port_event_t *)(port_mem.event_ring + sizeof(event_ring_header_t));
}

// Port state
static inline port_state_t *port_state(void) {
    return &port_mem.state;
}

// GC heap
static inline uint8_t *port_gc_heap(void) { return port_mem.gc_heap; }
static inline size_t port_gc_heap_size(void) { return PORT_GC_HEAP_SIZE; }

// Pystack for context N
static inline uint8_t *port_pystack(int ctx) { return port_mem.pystacks[ctx]; }
static inline uint8_t *port_pystack_end(int ctx) {
    return port_mem.pystacks[ctx] + PORT_PYSTACK_SIZE;
}
static inline size_t port_pystack_size(void) { return PORT_PYSTACK_SIZE; }

// Context metadata
static inline ctx_meta_t *port_ctx_meta(int ctx) {
    return &port_mem.ctx_meta[ctx];
}

// Input buffer
static inline char *port_input_buf(void) { return port_mem.input_buf; }
static inline size_t port_input_buf_size(void) { return PORT_INPUT_BUF_SIZE; }

// ── Memory map — address validation for machine.mem32 ──
//
// Each entry maps a base address + size to a named region.
// port_memory_validate_addr() checks if an address falls in a known
// region — the machine.mem32 get_addr function calls this.

#define MEM_MAP_MAX_ENTRIES 16

typedef struct {
    uint32_t base;
    uint32_t size;
    const char *name;  // e.g., "/py/heap", "/hal/gpio"
} mem_map_entry_t;

typedef struct {
    mem_map_entry_t entries[MEM_MAP_MAX_ENTRIES];
    uint32_t count;
} mem_map_t;

extern mem_map_t port_mem_map;

// Validate an address — returns entry name or NULL if invalid
const char *port_memory_validate_addr(uint32_t addr, uint32_t size);

#endif // PORT_MEMORY_H
