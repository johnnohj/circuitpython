/*
 * chassis/port_memory.h — Port-owned memory layout for MEMFS-in-linear-memory.
 *
 * Every field in port_memory_t is also a MEMFS file.  C accesses via pointer,
 * JS accesses via memfs.readFile() — same bytes in WASM linear memory.
 *
 * At init, each region is registered with MEMFS via the memfs_register()
 * WASM import.  No copy, no alias — the MEMFS file IS the struct field.
 *
 * Memory map (addresses are offsets from &port_mem):
 *
 *   /port/state          port_state_t       Port state machine
 *   /port/event_ring     event records      JS→C event notifications
 *   /hal/gpio            12 bytes × 64      GPIO pin slots
 *   /hal/analog          4 bytes × 64       ADC values
 *   /hal/serial/rx       ring buffer        Serial input (JS→C)
 *   /hal/serial/tx       ring buffer        Serial output (C→JS)
 */

#ifndef CHASSIS_PORT_MEMORY_H
#define CHASSIS_PORT_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define PORT_MAX_PINS          64
#define PORT_GPIO_SLOT_SIZE    12    /* bytes per pin (matches hal.h) */
#define PORT_ANALOG_SLOT_SIZE   4   /* bytes per analog channel */
#define PORT_EVENT_RING_SIZE  512   /* bytes for event ring */
#define PORT_SERIAL_BUF_SIZE 4096   /* bytes per serial direction */

/* ── VM regions ── */
#define VM_GC_HEAP_SIZE     (256 * 1024)  /* 256K for chassis PoC (512K in real port) */
#define VM_MAX_CONTEXTS     4             /* max concurrent Python contexts */
#define VM_PYSTACK_SIZE     (8 * 1024)    /* 8K per context pystack */
#define VM_INPUT_BUF_SIZE   4096          /* JS→C string passing buffer */

/* ------------------------------------------------------------------ */
/* Port state — the port's own execution state                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t phase;         /* current phase (INIT, HAL, VM, EXPORT) */
    uint32_t sub_phase;     /* within-phase position */
    uint32_t frame_count;   /* monotonic frame counter */
    uint32_t now_us;        /* current frame timestamp (µs, from JS) */
    uint32_t budget_us;     /* frame budget (µs) */
    uint32_t elapsed_us;    /* elapsed time this frame */
    uint32_t status;        /* last frame result code */
    uint32_t flags;         /* port flags (initialized, etc.) */
} port_state_t;

/* Port phases */
#define PORT_PHASE_UNINIT   0
#define PORT_PHASE_INIT     1
#define PORT_PHASE_HAL      2
#define PORT_PHASE_WORK     3   /* future: VM phase */
#define PORT_PHASE_EXPORT   4

/* Port status / frame return codes */
#define PORT_RC_DONE        0   /* frame complete, no more work */
#define PORT_RC_YIELD       1   /* budget exhausted, more work pending */
#define PORT_RC_EVENTS      2   /* processed events, may need repaint */
#define PORT_RC_ERROR       3   /* something went wrong */

/* Port flags */
#define PORT_FLAG_INITIALIZED  (1 << 0)
#define PORT_FLAG_HAS_EVENTS   (1 << 1)
#define PORT_FLAG_HAL_DIRTY    (1 << 2)

/* ------------------------------------------------------------------ */
/* Event ring — JS→C notification channel                              */
/*                                                                     */
/* JS writes events (8 bytes each) into this ring in linear memory.    */
/* C drains them each frame.  The event carries a type + pin/channel   */
/* identifier — the actual state is already in the HAL MEMFS region    */
/* (JS wrote it before pushing the event).                             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  type;      /* event type (EVT_*) */
    uint8_t  pin;       /* pin/channel index */
    uint16_t arg;       /* type-specific argument */
    uint32_t data;      /* type-specific data */
} port_event_t;

/* Event types */
#define EVT_NONE           0x00
#define EVT_GPIO_CHANGE    0x01   /* pin value changed */
#define EVT_ANALOG_CHANGE  0x02   /* analog value changed */
#define EVT_KEY_DOWN       0x10   /* keyboard input */
#define EVT_KEY_UP         0x11
#define EVT_WAKE           0x20   /* wake from idle */

/* Ring header (first 8 bytes of the event ring region) */
typedef struct {
    uint32_t write_head;   /* JS increments (byte offset into ring data) */
    uint32_t read_head;    /* C increments (byte offset into ring data) */
} event_ring_header_t;

/* Usable ring data starts after the header */
#define EVENT_RING_DATA_SIZE  (PORT_EVENT_RING_SIZE - sizeof(event_ring_header_t))
#define EVENT_RING_CAPACITY   (EVENT_RING_DATA_SIZE / sizeof(port_event_t))

/* ------------------------------------------------------------------ */
/* GPIO slot layout (matches current hal.h for compatibility)          */
/*                                                                     */
/*   [0]  enabled    int8: -1=never_reset, 0=disabled, 1=enabled      */
/*   [1]  direction  uint8: 0=input, 1=output, 2=output_open_drain   */
/*   [2]  value      uint8: 0/1                                       */
/*   [3]  pull       uint8: 0=none, 1=up, 2=down                     */
/*   [4]  role       uint8: what peripheral claimed this pin           */
/*   [5]  flags      uint8: JS_WROTE / C_WROTE / C_READ / LATCHED    */
/*   [6]  category   uint8: board-defined pin category                */
/*   [7]  latched    uint8: captured input value (IRQ-like)           */
/*   [8-11] reserved                                                   */
/* ------------------------------------------------------------------ */

/* Byte offsets within a GPIO slot */
#define GPIO_OFF_ENABLED    0
#define GPIO_OFF_DIRECTION  1
#define GPIO_OFF_VALUE      2
#define GPIO_OFF_PULL       3
#define GPIO_OFF_ROLE       4
#define GPIO_OFF_FLAGS      5
#define GPIO_OFF_CATEGORY   6
#define GPIO_OFF_LATCHED    7

/* Roles */
#define ROLE_UNCLAIMED   0x00
#define ROLE_DIGITAL_IN  0x01
#define ROLE_DIGITAL_OUT 0x02
#define ROLE_ADC         0x03
#define ROLE_PWM         0x05

/* Flags */
#define FLAG_JS_WROTE    0x01
#define FLAG_C_WROTE     0x02
#define FLAG_C_READ      0x04
#define FLAG_LATCHED     0x08

/* ------------------------------------------------------------------ */
/* Port stack — heap-resident execution state for halt/resume          */
/*                                                                     */
/* The port (and eventually the VM) can halt mid-work by saving its    */
/* position here.  Resume = read position, continue.  The C call       */
/* stack is ephemeral — this struct IS the durable execution state.    */
/*                                                                     */
/* Stack frames are indexed by `depth`.  Each frame has a phase,       */
/* sub-phase, and scratch data — enough to resume any operation.       */
/* ------------------------------------------------------------------ */

#define PORT_STACK_MAX_DEPTH  8
#define PORT_STACK_FRAME_DATA 24  /* bytes of scratch per frame */

typedef struct {
    uint32_t phase;
    uint32_t sub_phase;
    uint8_t  data[PORT_STACK_FRAME_DATA];
} port_stack_frame_t;

typedef struct {
    uint32_t depth;     /* current frame index (0 = top-level) */
    uint32_t flags;     /* STACK_FLAG_* */
    port_stack_frame_t frames[PORT_STACK_MAX_DEPTH];
} port_stack_t;

/* Stack flags */
#define STACK_FLAG_ACTIVE     (1 << 0)  /* work in progress */
#define STACK_FLAG_YIELDED    (1 << 1)  /* halted mid-work, resume next frame */
#define STACK_FLAG_COMPLETE   (1 << 2)  /* work finished */

/* ------------------------------------------------------------------ */
/* VM context metadata — per-context execution state                   */
/*                                                                     */
/* Each context has a pystack region + metadata.  The chassis          */
/* doesn't run the VM, but reserves the slots so the VM can bolt on.  */
/* Context 0 = main (code.py + REPL).  Others are for background      */
/* tasks, asyncio coroutines, etc.                                     */
/* ------------------------------------------------------------------ */

/* Context status values (matches supervisor/context.h) */
#define CTX_FREE      0
#define CTX_IDLE      1
#define CTX_RUNNABLE  2
#define CTX_RUNNING   3
#define CTX_YIELDED   4
#define CTX_SLEEPING  5
#define CTX_DONE      6

typedef struct {
    uint8_t  status;        /* CTX_* */
    uint8_t  priority;      /* 0 = highest */
    uint16_t reserved;
    uint32_t pystack_used;  /* bytes used in this context's pystack */
    uint32_t yield_ip;      /* saved instruction pointer (offset into pystack) */
    uint32_t delay_until;   /* sleep deadline (ms, low 32 bits) */
} vm_context_meta_t;

/* ------------------------------------------------------------------ */
/* Serial ring buffers                                                 */
/*                                                                     */
/* Simple ring: first 8 bytes are head/tail uint32, rest is data.     */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t write_head;
    uint32_t read_head;
    uint8_t  data[PORT_SERIAL_BUF_SIZE - 8];
} serial_ring_t;

/* ------------------------------------------------------------------ */
/* The port memory layout — one struct, everything MEMFS-registered    */
/* ------------------------------------------------------------------ */

typedef struct {
    /* ── Port core ── */

    /* /port/state — port state machine */
    port_state_t state;

    /* /port/stack — heap-resident execution stack */
    port_stack_t stack;

    /* /port/event_ring — JS→C events */
    uint8_t event_ring[PORT_EVENT_RING_SIZE];

    /* ── HAL ── */

    /* /hal/gpio — GPIO pin slots (12 bytes × 64 pins) */
    uint8_t hal_gpio[PORT_MAX_PINS * PORT_GPIO_SLOT_SIZE];

    /* /hal/analog — ADC values (4 bytes × 64 channels) */
    uint8_t hal_analog[PORT_MAX_PINS * PORT_ANALOG_SLOT_SIZE];

    /* /hal/serial/rx — serial input (JS→C) */
    serial_ring_t serial_rx;

    /* /hal/serial/tx — serial output (C→JS) */
    serial_ring_t serial_tx;

    /* Dirty flags — which pins changed since last hal_step */
    volatile uint64_t gpio_dirty;
    volatile uint64_t analog_dirty;
    volatile uint32_t change_count;  /* monotonic, any change */

    /* ── VM regions (reserved for MicroPython) ── */

    /* /py/heap — GC heap */
    uint8_t gc_heap[VM_GC_HEAP_SIZE]
        __attribute__((aligned(sizeof(void *))));

    /* /py/ctx/N/pystack — per-context pystacks */
    uint8_t pystacks[VM_MAX_CONTEXTS][VM_PYSTACK_SIZE]
        __attribute__((aligned(sizeof(void *))));

    /* /py/ctx/meta — context metadata array */
    vm_context_meta_t ctx_meta[VM_MAX_CONTEXTS];

    /* /py/input — JS→C string passing buffer */
    char input_buf[VM_INPUT_BUF_SIZE];

    /* /py/state — VM-level state (active context, etc.) */
    uint32_t vm_active_ctx;    /* currently active context id */
    uint32_t vm_flags;         /* VM_FLAG_* */

    /* /py/wakeup — cooperative yield: WFE stores wakeup deadline here.
     * JS reads this to know when to schedule the next chassis_frame. */
    uint32_t wakeup_ms;        /* 0 = no deadline (wake on next event) */

    /* /py/vm_abort_reason — why the VM was halted.
     * 0 = normal completion, 1 = budget, 2 = cooperative (WFE) */
    uint32_t vm_abort_reason;
} port_memory_t;

/* VM flags */
#define VM_FLAG_INITIALIZED  (1 << 0)  /* mp_init() has been called */
#define VM_FLAG_RUNNING      (1 << 1)  /* VM is executing bytecode */
#define VM_FLAG_HAS_CODE     (1 << 2)  /* code.py or REPL code loaded */

/* VM abort reasons — why chassis_frame returned */
#define VM_ABORT_NONE        0  /* normal completion */
#define VM_ABORT_BUDGET      1  /* frame budget expired */
#define VM_ABORT_WFE         2  /* cooperative yield (WFE) */

/* The single instance — defined in port_memory.c */
extern port_memory_t port_mem;

/* Register all regions with MEMFS (call once at init) */
void port_memory_register(void);

/* Zero all memory (regions stay registered — same addresses) */
void port_memory_reset(void);

/* ------------------------------------------------------------------ */
/* Convenience accessors                                               */
/* ------------------------------------------------------------------ */

/* GPIO slot pointer for pin N */
static inline uint8_t *gpio_slot(int pin) {
    return &port_mem.hal_gpio[pin * PORT_GPIO_SLOT_SIZE];
}

/* Analog slot pointer for channel N */
static inline uint8_t *analog_slot(int ch) {
    return &port_mem.hal_analog[ch * PORT_ANALOG_SLOT_SIZE];
}

/* Event ring header */
static inline event_ring_header_t *event_ring_hdr(void) {
    return (event_ring_header_t *)port_mem.event_ring;
}

/* Event ring data (after header) */
static inline port_event_t *event_ring_data(void) {
    return (port_event_t *)(port_mem.event_ring + sizeof(event_ring_header_t));
}

/* Port state */
static inline port_state_t *port_state(void) {
    return &port_mem.state;
}

/* Port stack */
static inline port_stack_t *port_stack(void) {
    return &port_mem.stack;
}

/* Current stack frame */
static inline port_stack_frame_t *port_stack_top(void) {
    return &port_mem.stack.frames[port_mem.stack.depth];
}

/* GC heap */
static inline uint8_t *vm_gc_heap(void) { return port_mem.gc_heap; }
static inline size_t vm_gc_heap_size(void) { return VM_GC_HEAP_SIZE; }

/* Pystack for context N */
static inline uint8_t *vm_pystack(int ctx) { return port_mem.pystacks[ctx]; }
static inline size_t vm_pystack_size(void) { return VM_PYSTACK_SIZE; }

/* Context metadata */
static inline vm_context_meta_t *vm_ctx_meta(int ctx) {
    return &port_mem.ctx_meta[ctx];
}

/* Input buffer */
static inline char *vm_input_buf(void) { return port_mem.input_buf; }
static inline size_t vm_input_buf_size(void) { return VM_INPUT_BUF_SIZE; }

/* ------------------------------------------------------------------ */
/* Memory map — address validation for machine.mem32                   */
/*                                                                     */
/* Each entry maps a base address + size to a named region.            */
/* port_memory_init_map() populates this from the static layout.       */
/* port_memory_validate_addr() checks if an address falls in a known   */
/* region — the machine.mem32 get_addr function calls this.            */
/* ------------------------------------------------------------------ */

#define MEM_MAP_MAX_ENTRIES 16

typedef struct {
    uint32_t base;
    uint32_t size;
    const char *name;  /* e.g., "/py/heap", "/hal/gpio" */
} mem_map_entry_t;

typedef struct {
    mem_map_entry_t entries[MEM_MAP_MAX_ENTRIES];
    uint32_t count;
} mem_map_t;

extern mem_map_t port_mem_map;

/* Initialize the memory map from port_mem layout */
void port_memory_init_map(void);

/* Validate an address — returns entry name or NULL if invalid */
const char *port_memory_validate_addr(uint32_t addr, uint32_t size);

#endif /* CHASSIS_PORT_MEMORY_H */
