/*
 * chassis/main.c — Port chassis entry point.
 *
 * Standalone proof-of-concept: MEMFS-in-linear-memory, frame loop with
 * budget, event drain, HAL step.  No MicroPython dependency.
 *
 * Exports:
 *   chassis_init()                      — one-time init
 *   chassis_frame(now_ms, budget_ms)    — per-frame entry point
 *   chassis_memory_addr/size()          — layout inspection
 *   chassis_claim_pin(pin, role)        — claim a pin
 *   chassis_release_pin(pin)            — release a pin
 *   chassis_release_all()               — release all pins (soft reset)
 *   chassis_write_pin(pin, value)       — write output pin
 *   chassis_read_pin(pin)               — read input pin
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "port_memory.h"
#include "memfs_imports.h"
#include "hal.h"
#include "serial.h"
#include "budget.h"
#include "port_step.h"
#include "ffi_imports.h"
#include "chassis_constants.h"

/* ------------------------------------------------------------------ */
/* Exports — visible to JS via WASM exports                            */
/* ------------------------------------------------------------------ */

#define EXPORT __attribute__((visibility("default")))

/* ------------------------------------------------------------------ */
/* Event drain — process JS→C events from the event ring               */
/* ------------------------------------------------------------------ */

static uint32_t drain_events(void) {
    event_ring_header_t *hdr = event_ring_hdr();
    port_event_t *ring = event_ring_data();
    uint32_t count = 0;

    while (hdr->read_head != hdr->write_head) {
        uint32_t idx = (hdr->read_head / sizeof(port_event_t))
                       % EVENT_RING_CAPACITY;
        port_event_t *evt = &ring[idx];

        switch (evt->type) {
        case EVT_GPIO_CHANGE:
            if (evt->pin < PORT_MAX_PINS) {
                port_mem.gpio_dirty |= (1ULL << evt->pin);
                gpio_slot(evt->pin)[GPIO_OFF_FLAGS] |= FLAG_JS_WROTE;
            }
            break;

        case EVT_ANALOG_CHANGE:
            if (evt->pin < PORT_MAX_PINS) {
                port_mem.analog_dirty |= (1ULL << evt->pin);
            }
            break;

        case EVT_KEY_DOWN: {
            /* Push keystroke into serial RX ring */
            uint8_t ch = (uint8_t)(evt->arg & 0xFF);
            serial_tx_write(&ch, 1);  /* TODO: this should be RX, fix direction */
            break;
        }

        case EVT_WAKE:
            break;

        default:
            break;
        }

        hdr->read_head += sizeof(port_event_t);
        if (hdr->read_head >= EVENT_RING_DATA_SIZE) {
            hdr->read_head = 0;
        }
        count++;
    }

    return count;
}

/* ------------------------------------------------------------------ */
/* chassis_init — one-time initialization                              */
/* ------------------------------------------------------------------ */

EXPORT void chassis_init(void) {
    memset(&port_mem, 0, sizeof(port_mem));
    port_memory_register();
    hal_init();

    port_mem.state.phase = PORT_PHASE_HAL;
    port_mem.state.flags = PORT_FLAG_INITIALIZED;

    printf("[chassis] init: port_mem at %p (%u bytes)\n",
           (void *)&port_mem, (unsigned)sizeof(port_mem));
}

/* ------------------------------------------------------------------ */
/* chassis_frame — per-frame entry point                               */
/*                                                                     */
/* Budget-aware: tracks elapsed time, returns YIELD if firm deadline    */
/* is approaching.  All state is in port_mem (linear memory).           */
/* ------------------------------------------------------------------ */

EXPORT uint32_t chassis_frame(double now_ms, double budget_ms) {
    port_state_t *st = port_state();

    /* Start budget clock */
    budget_frame_start();
    if (budget_ms > 0.0) {
        budget_set_deadlines(
            (uint32_t)(budget_ms * 800.0),   /* soft = 80% of budget */
            (uint32_t)(budget_ms * 1000.0)   /* firm = 100% of budget */
        );
    }

    st->now_us = (uint32_t)(now_ms * 1000.0);
    st->budget_us = budget_get_firm_us();
    st->frame_count++;
    st->flags &= ~(PORT_FLAG_HAS_EVENTS | PORT_FLAG_HAL_DIRTY);

    /* Phase 1: Drain events */
    uint32_t nevents = drain_events();
    if (nevents > 0) {
        st->flags |= PORT_FLAG_HAS_EVENTS;
    }

    /* Phase 2: HAL step */
    hal_step();

    /* Phase 3: Port state machine — runs work in budget-aware slices.
     * If work is active, port_step() processes items until budget
     * expires, then returns YIELD.  Next frame resumes from the
     * saved position in port_stack_t. */
    uint32_t work_rc = port_step();

    /* Phase 4: Export status + notify JS */
    st->elapsed_us = budget_elapsed_us();

    uint32_t rc;
    if (work_rc == PORT_RC_YIELD) {
        rc = PORT_RC_YIELD;
        /* Tell JS to schedule another frame — we have more work */
        ffi_request_frame();
    } else if (st->flags & PORT_FLAG_HAL_DIRTY) {
        rc = PORT_RC_EVENTS;
    } else {
        rc = PORT_RC_DONE;
    }
    st->status = rc;
    return rc;
}

/* ------------------------------------------------------------------ */
/* HAL exports — JS can claim/release/read/write pins                  */
/* ------------------------------------------------------------------ */

EXPORT uint32_t chassis_claim_pin(uint32_t pin, uint32_t role) {
    return hal_claim_pin((uint8_t)pin, (uint8_t)role) ? 1 : 0;
}

EXPORT void chassis_release_pin(uint32_t pin) {
    hal_release_pin((uint8_t)pin);
}

EXPORT void chassis_release_all(void) {
    hal_release_all();
}

EXPORT void chassis_write_pin(uint32_t pin, uint32_t value) {
    hal_write_pin((uint8_t)pin, (uint8_t)value);
}

EXPORT uint32_t chassis_read_pin(uint32_t pin) {
    return hal_read_pin((uint8_t)pin);
}

/* ------------------------------------------------------------------ */
/* Work exports — submit and inspect resumable workloads               */
/* ------------------------------------------------------------------ */

EXPORT void chassis_submit_work(uint32_t total_items) {
    port_submit_work(total_items);
}

EXPORT uint32_t chassis_work_active(void) {
    return port_work_active();
}

EXPORT uint32_t chassis_work_progress(void) {
    return port_work_progress();
}

/* ------------------------------------------------------------------ */
/* VM region exports — JS queries layout for inspection/tooling        */
/* ------------------------------------------------------------------ */

EXPORT uint32_t chassis_gc_heap_addr(void) {
    return (uint32_t)(uintptr_t)port_mem.gc_heap;
}

EXPORT uint32_t chassis_gc_heap_size(void) {
    return VM_GC_HEAP_SIZE;
}

EXPORT uint32_t chassis_pystack_addr(uint32_t ctx) {
    if (ctx >= VM_MAX_CONTEXTS) return 0;
    return (uint32_t)(uintptr_t)port_mem.pystacks[ctx];
}

EXPORT uint32_t chassis_pystack_size(void) {
    return VM_PYSTACK_SIZE;
}

EXPORT uint32_t chassis_ctx_meta_addr(void) {
    return (uint32_t)(uintptr_t)port_mem.ctx_meta;
}

EXPORT uint32_t chassis_input_buf_addr(void) {
    return (uint32_t)(uintptr_t)port_mem.input_buf;
}

EXPORT uint32_t chassis_input_buf_size(void) {
    return VM_INPUT_BUF_SIZE;
}

/* Validate an address for machine.mem32 access.
 * Returns 1 if the address falls in a known MEMFS region, 0 otherwise. */
EXPORT uint32_t chassis_validate_addr(uint32_t addr, uint32_t size) {
    return port_memory_validate_addr(addr, size) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Memory map export — JS can enumerate all regions                    */
/* ------------------------------------------------------------------ */

EXPORT uint32_t chassis_mem_map_count(void) {
    return port_mem_map.count;
}

/* ------------------------------------------------------------------ */
/* Inspection exports                                                  */
/* ------------------------------------------------------------------ */

EXPORT uint32_t chassis_memory_addr(void) {
    return (uint32_t)(uintptr_t)&port_mem;
}

EXPORT uint32_t chassis_memory_size(void) {
    return (uint32_t)sizeof(port_mem);
}

/* ------------------------------------------------------------------ */
/* main — for WASI CLI testing                                         */
/* ------------------------------------------------------------------ */

int main(void) {
    chassis_init();
    printf("[chassis] standalone test\n");

    /* Test claim/release */
    printf("[chassis] claiming pin 5 as DIGITAL_IN: %s\n",
           hal_claim_pin(5, ROLE_DIGITAL_IN) ? "ok" : "fail");
    printf("[chassis] claiming pin 13 as DIGITAL_OUT: %s\n",
           hal_claim_pin(13, ROLE_DIGITAL_OUT) ? "ok" : "fail");
    printf("[chassis] claiming pin 5 as DIGITAL_OUT (should fail): %s\n",
           hal_claim_pin(5, ROLE_DIGITAL_OUT) ? "ok" : "fail");
    printf("[chassis] claimed count: %u\n", hal_claimed_count());

    /* Test write/read */
    hal_write_pin(13, 1);
    printf("[chassis] pin 13 value: %u\n", hal_read_pin(13));

    /* Simulate JS input on pin 5 */
    uint8_t *slot5 = gpio_slot(5);
    slot5[GPIO_OFF_VALUE] = 1;
    slot5[GPIO_OFF_FLAGS] |= FLAG_JS_WROTE;
    port_mem.gpio_dirty |= (1ULL << 5);

    /* Run frame */
    uint32_t rc = chassis_frame(16.667, 13.0);
    printf("[chassis] frame: rc=%u elapsed=%u us\n",
           rc, port_mem.state.elapsed_us);
    printf("[chassis] pin 5 latched: %u\n", hal_read_pin(5));

    /* Test serial */
    serial_tx_print("Hello from chassis!\n");
    printf("[chassis] serial TX has data\n");

    /* Test halt/resume workload */
    port_submit_work(200);
    int frames = 0;
    while (port_work_active()) {
        chassis_frame(100.0 + frames * 16.667, 0.5);
        frames++;
    }
    printf("[chassis] work: 200 items across %d frames, progress=%u\n",
           frames, port_work_progress());

    /* Test VM regions */
    printf("[chassis] gc_heap at %p (%u bytes)\n",
           (void *)vm_gc_heap(), (unsigned)vm_gc_heap_size());
    printf("[chassis] pystack[0] at %p (%u bytes)\n",
           (void *)vm_pystack(0), (unsigned)vm_pystack_size());
    printf("[chassis] mem_map: %u regions\n", port_mem_map.count);

    /* Test address validation */
    uint32_t heap_addr = (uint32_t)(uintptr_t)vm_gc_heap();
    const char *region = port_memory_validate_addr(heap_addr, 4);
    printf("[chassis] validate 0x%x: %s\n", heap_addr, region ? region : "INVALID");
    region = port_memory_validate_addr(0xDEAD, 4);
    printf("[chassis] validate 0xDEAD: %s\n", region ? region : "INVALID");

    /* Release all */
    hal_release_all();
    printf("[chassis] after release_all: claimed=%u\n", hal_claimed_count());

    return 0;
}
