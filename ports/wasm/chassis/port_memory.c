/*
 * chassis/port_memory.c — Static port memory + MEMFS registration + memory map.
 */

#include "port_memory.h"
#include "memfs_imports.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Instances                                                           */
/* ------------------------------------------------------------------ */

port_memory_t port_mem;
mem_map_t port_mem_map;

/* ------------------------------------------------------------------ */
/* Helper: register one region and add to memory map                   */
/* ------------------------------------------------------------------ */

static void register_region(const char *path, void *ptr, uint32_t size) {
    memfs_register(path, ptr, size);

    if (port_mem_map.count < MEM_MAP_MAX_ENTRIES) {
        mem_map_entry_t *e = &port_mem_map.entries[port_mem_map.count++];
        e->base = (uint32_t)(uintptr_t)ptr;
        e->size = size;
        e->name = path;
    }
}

/* ------------------------------------------------------------------ */
/* MEMFS registration + memory map init                                */
/* ------------------------------------------------------------------ */

void port_memory_register(void) {
    port_mem_map.count = 0;

    /* Port core */
    register_region("/port/state",
        &port_mem.state, sizeof(port_mem.state));
    register_region("/port/stack",
        &port_mem.stack, sizeof(port_mem.stack));
    register_region("/port/event_ring",
        port_mem.event_ring, sizeof(port_mem.event_ring));

    /* HAL */
    register_region("/hal/gpio",
        port_mem.hal_gpio, sizeof(port_mem.hal_gpio));
    register_region("/hal/analog",
        port_mem.hal_analog, sizeof(port_mem.hal_analog));
    register_region("/hal/serial/rx",
        &port_mem.serial_rx, sizeof(port_mem.serial_rx));
    register_region("/hal/serial/tx",
        &port_mem.serial_tx, sizeof(port_mem.serial_tx));

    /* VM: GC heap */
    register_region("/py/heap",
        port_mem.gc_heap, sizeof(port_mem.gc_heap));

    /* VM: per-context pystacks */
    char path[32];
    for (int i = 0; i < VM_MAX_CONTEXTS; i++) {
        snprintf(path, sizeof(path), "/py/ctx/%d/pystack", i);
        register_region(path, port_mem.pystacks[i], VM_PYSTACK_SIZE);
    }

    /* VM: context metadata */
    register_region("/py/ctx/meta",
        port_mem.ctx_meta, sizeof(port_mem.ctx_meta));

    /* VM: input buffer */
    register_region("/py/input",
        port_mem.input_buf, sizeof(port_mem.input_buf));
}

/* ------------------------------------------------------------------ */
/* Memory map — for machine.mem32 address validation                   */
/* ------------------------------------------------------------------ */

void port_memory_init_map(void) {
    /* Map is already populated by register_region() calls above.
     * This function exists for explicit initialization if register
     * and map init are ever separated. */
}

const char *port_memory_validate_addr(uint32_t addr, uint32_t size) {
    for (uint32_t i = 0; i < port_mem_map.count; i++) {
        mem_map_entry_t *e = &port_mem_map.entries[i];
        if (addr >= e->base && (addr + size) <= (e->base + e->size)) {
            return e->name;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

void port_memory_reset(void) {
    memset(&port_mem, 0, sizeof(port_mem));
}
