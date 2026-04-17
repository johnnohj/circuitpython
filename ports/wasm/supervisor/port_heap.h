/*
 * supervisor/port_heap.h — Port-local heap interface for WASM.
 *
 * Same interface as supervisor/port_heap.h.
 * WASM implementation: malloc/free from wasi-sdk libc.
 * No DMA distinction — all WASM linear memory is equally accessible.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

void port_heap_init(void);

void *port_malloc(size_t size, bool dma_capable);
void *port_malloc_zero(size_t size, bool dma_capable);
void port_free(void *ptr);
void *port_realloc(void *ptr, size_t size, bool dma_capable);

/* All WASM memory is "DMA capable" — no distinction needed. */
#define CIRCUITPY_ALL_MEMORY_DMA_CAPABLE (1)

size_t port_heap_get_largest_free_size(void);
