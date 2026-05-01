// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/semihosting.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/event_ring.h — JS→C event ring drain.
//
// JS writes port_event_t records into the event ring region of port_mem.
// C drains them each frame via event_ring_drain().  Each event carries
// a type + pin/channel — the actual state is already in the HAL MEMFS
// region (JS wrote it before pushing the event).
//
// The event ring lives in port_mem.event_ring (defined in port_memory.h).
// No separate static buffer — MEMFS-in-linear-memory.
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer, event delivery)
//   design/behavior/01-hardware-init.md     (GPIO events)

#ifndef PORT_EVENT_RING_H
#define PORT_EVENT_RING_H

#include <stdint.h>

// Drain all pending events from the ring.
// Calls event_ring_dispatch() for each non-empty event.
// Called at the top of each frame, before VM work.
void event_ring_drain(void);

// Dispatch a single event.  Weak default is a no-op.
// The supervisor (Phase 4) overrides this to route events:
//   EVT_KEY_DOWN  → serial rx buffer, Ctrl-C check
//   EVT_GPIO_CHANGE → hal dirty flag (already set by JS, but may
//                     trigger background callback wakeup)
//   EVT_WAKE → wake sleeping context
void event_ring_dispatch(const void *evt);

// Get number of pending (unread) events.
uint32_t event_ring_pending(void);

#endif // PORT_EVENT_RING_H
