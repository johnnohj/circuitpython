// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/port_memory.c by CircuitPython contributors
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/port_memory.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/port_memory.c — Static port memory + MEMFS registration + memory map.
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer, port owns memory)
//   design/behavior/01-hardware-init.md     (GPIO init, pin reset)

#include "port/port_memory.h"
#include "port/memfs_imports.h"  // TODO(Phase 1.9): memfs_register declaration
#include <string.h>
#include <stdio.h>

// ── Instances ──

port_memory_t port_mem = {
    .debug_enabled = true,
    .cli_mode = false,
    .active_ctx_id = -1,
    .vm_flags = 0,
    .vm_abort_reason = VM_ABORT_NONE,
    .wakeup_ms = 0,
    .js_now_ms = 0,
};

mem_map_t port_mem_map;

// ── Helper: register one region and add to memory map ──

static void register_region(const char *path, void *ptr, uint32_t size) {
    memfs_register(path, ptr, size);

    if (port_mem_map.count < MEM_MAP_MAX_ENTRIES) {
        mem_map_entry_t *e = &port_mem_map.entries[port_mem_map.count++];
        e->base = (uint32_t)(uintptr_t)ptr;
        e->size = size;
        e->name = path;
    }
}

// ── MEMFS registration ──

void port_memory_register(void) {
    port_mem_map.count = 0;

    // Port core
    register_region("/port/state",
        &port_mem.state, sizeof(port_mem.state));
    register_region("/port/event_ring",
        port_mem.event_ring, sizeof(port_mem.event_ring));

    // HAL
    register_region("/hal/gpio",
        port_mem.hal_gpio, sizeof(port_mem.hal_gpio));
    register_region("/hal/analog",
        port_mem.hal_analog, sizeof(port_mem.hal_analog));
    register_region("/hal/neopixel",
        port_mem.hal_neopixel, sizeof(port_mem.hal_neopixel));
    register_region("/hal/serial/rx",
        &port_mem.serial_rx, sizeof(port_mem.serial_rx));
    register_region("/hal/serial/tx",
        &port_mem.serial_tx, sizeof(port_mem.serial_tx));

    // VM: GC heap
    register_region("/py/heap",
        port_mem.gc_heap, sizeof(port_mem.gc_heap));

    // VM: per-context pystacks
    char path[32];
    for (int i = 0; i < PORT_MAX_CONTEXTS; i++) {
        snprintf(path, sizeof(path), "/py/ctx/%d/pystack", i);
        register_region(path, port_mem.pystacks[i], PORT_PYSTACK_SIZE);
    }

    // VM: context metadata
    register_region("/py/ctx/meta",
        port_mem.ctx_meta, sizeof(port_mem.ctx_meta));

    // VM: input buffer
    register_region("/py/input",
        port_mem.input_buf, sizeof(port_mem.input_buf));
}

// ── Memory map validation (for machine.mem32) ──

const char *port_memory_validate_addr(uint32_t addr, uint32_t size) {
    for (uint32_t i = 0; i < port_mem_map.count; i++) {
        mem_map_entry_t *e = &port_mem_map.entries[i];
        if (addr >= e->base && (addr + size) <= (e->base + e->size)) {
            return e->name;
        }
    }
    return NULL;
}

// ── Reset ──

void port_memory_reset(void) {
    // Zero everything, preserving MEMFS registrations (same addresses).
    memset(&port_mem, 0, sizeof(port_mem));
    // Restore defaults that aren't zero.
    port_mem.active_ctx_id = -1;
}

// ── WASM exports — JS can inspect port memory layout ──

__attribute__((export_name("cp_port_memory_addr")))
uintptr_t cp_port_memory_addr(void) {
    return (uintptr_t)&port_mem;
}

__attribute__((export_name("cp_port_memory_size")))
uint32_t cp_port_memory_size(void) {
    return (uint32_t)sizeof(port_memory_t);
}
