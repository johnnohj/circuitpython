// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/semihosting.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/event_ring.c — JS→C event ring drain.
//
// The supervisor's semihosting.c had the event ring in a separate
// static buffer with its own WASM exports (sh_event_ring_addr).
// Here the ring lives in port_mem.event_ring (MEMFS-registered),
// and JS accesses it via the port_mem base address + known offset.
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer, event delivery)
//   design/behavior/01-hardware-init.md     (GPIO events)

#include "port/event_ring.h"
#include "port/port_memory.h"
#include "port/constants.h"

// ── Drain ──

void event_ring_drain(void) {
    event_ring_header_t *hdr = event_ring_hdr();
    port_event_t *data = event_ring_data();

    while (hdr->read_head != hdr->write_head) {
        uint32_t idx = (hdr->read_head / sizeof(port_event_t))
                     % EVENT_RING_CAPACITY;
        port_event_t *evt = &data[idx];

        if (evt->type != EVT_NONE) {
            event_ring_dispatch(evt);
        }

        hdr->read_head += sizeof(port_event_t);
        // Wrap read_head to prevent unbounded growth
        if (hdr->read_head >= EVENT_RING_DATA_SIZE) {
            hdr->read_head = 0;
        }
    }
}

// ── Dispatch — weak default ──
// Supervisor (Phase 4) overrides to route KEY_DOWN → rx buffer, etc.

__attribute__((weak))
void event_ring_dispatch(const void *evt) {
    (void)evt;
}

// ── Query ──

uint32_t event_ring_pending(void) {
    event_ring_header_t *hdr = event_ring_hdr();
    if (hdr->write_head >= hdr->read_head) {
        return (hdr->write_head - hdr->read_head) / sizeof(port_event_t);
    }
    return (EVENT_RING_DATA_SIZE - hdr->read_head + hdr->write_head)
         / sizeof(port_event_t);
}
