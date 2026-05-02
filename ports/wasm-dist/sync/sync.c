// sync.c — frameless memory synchronization service
//
// Compiles to a standalone WASM binary (~2KB). No libc, no WASI.
// All storage is in linear memory. JS reads/writes directly.
//
// Build:
//   clang --target=wasm32 -nostdlib -O2 \
//     -Wl,--no-entry -Wl,--export-dynamic \
//     -Wl,--import-memory \
//     -o sync.wasm sync.c

#include <stdint.h>

// ── Memory layout ──
//
// A global struct at a known address in linear memory.
// JS discovers addresses via exported query functions.

#define MAX_REGIONS  64

// Region types
enum { TYPE_SLAB = 0, TYPE_RING = 1, TYPE_SLOTS = 2 };

// Region table entry (32 bytes)
typedef struct {
    uint32_t name_hash;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t region_type;   // 0=slab, 1=ring, 2=slots
    uint32_t seq;
    uint32_t dirty;
    uint32_t slot_size;
    uint32_t aux;           // ring: write_head; slots: slot_count
} region_entry_t;

// Bus state — lives in the data segment, linker places it
static struct {
    uint32_t        region_count;
    uint32_t        total_allocated;
    uint32_t        global_seq;
    uint32_t        reserved;
    region_entry_t  entries[MAX_REGIONS];
} bus;

// Data regions are allocated starting after the bus struct.
// We reserve a large arena for dynamic allocation.
#define ARENA_SIZE (512 * 1024)
static uint8_t arena[ARENA_SIZE];

// ── Helpers ──

static uint32_t fnv1a(const uint8_t *s, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= s[i];
        h *= 16777619u;
    }
    return h;
}

static int has_region(uint32_t hash) {
    for (uint32_t i = 0; i < bus.region_count; i++) {
        if (bus.entries[i].name_hash == hash) return 1;
    }
    return 0;
}

// ── Exported functions ──

// Create a named region. Returns data offset in linear memory, or 0.
__attribute__((export_name("region_create")))
uint32_t region_create(const uint8_t *name, int name_len,
                       uint32_t size, uint32_t type, uint32_t slot_size) {
    if (bus.region_count >= MAX_REGIONS) return 0;

    uint32_t hash = fnv1a(name, name_len);
    if (has_region(hash)) return 0;

    // Align to 8 bytes
    uint32_t arena_offset = (bus.total_allocated + 7) & ~7u;

    // Rings need 4 extra bytes for read_head
    uint32_t total = (type == TYPE_RING) ? size + 4 : size;
    if (arena_offset + total > ARENA_SIZE) return 0;

    // Compute absolute address in linear memory
    uint32_t abs_offset = (uint32_t)(uintptr_t)&arena[arena_offset];

    region_entry_t *r = &bus.entries[bus.region_count];
    r->name_hash   = hash;
    r->data_offset = abs_offset;
    r->data_size   = size;
    r->region_type = type;
    r->seq         = 0;
    r->dirty       = 0;
    r->slot_size   = slot_size;
    r->aux         = 0;

    if (type == TYPE_SLOTS && slot_size > 0) {
        r->aux = size / slot_size;
    }

    // Zero the region
    for (uint32_t i = 0; i < total; i++) arena[arena_offset + i] = 0;

    bus.total_allocated = arena_offset + total;
    bus.region_count++;

    return abs_offset;
}

__attribute__((export_name("region_find")))
int32_t region_find(uint32_t name_hash) {
    for (uint32_t i = 0; i < bus.region_count; i++) {
        if (bus.entries[i].name_hash == name_hash) return (int32_t)i;
    }
    return -1;
}

__attribute__((export_name("hash_name")))
uint32_t hash_name(const uint8_t *name, int len) {
    return fnv1a(name, len);
}

// ── Slab operations ──

__attribute__((export_name("slab_mark_dirty")))
void slab_mark_dirty(uint32_t idx) {
    bus.entries[idx].dirty = 1;
    bus.entries[idx].seq++;
    bus.global_seq++;
}

__attribute__((export_name("slab_clear_dirty")))
uint32_t slab_clear_dirty(uint32_t idx) {
    bus.entries[idx].dirty = 0;
    return bus.entries[idx].seq;
}

// ── Ring operations ──
// Layout at data_offset: [read_head:u32] [data: data_size bytes]
// write_head lives in entry.aux.

__attribute__((export_name("ring_push")))
uint32_t ring_push(uint32_t idx, const uint8_t *src, uint32_t len) {
    region_entry_t *r = &bus.entries[idx];
    uint32_t *rh_ptr = (uint32_t *)(uintptr_t)r->data_offset;
    uint8_t  *data   = (uint8_t *)(uintptr_t)(r->data_offset + 4);
    uint32_t cap = r->data_size;
    uint32_t wh = r->aux;
    uint32_t rh = *rh_ptr;

    uint32_t written = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t next = (wh + 1) % cap;
        if (next == rh) break;
        data[wh] = src[i];
        wh = next;
        written++;
    }
    r->aux = wh;
    if (written > 0) { r->seq++; bus.global_seq++; }
    return written;
}

__attribute__((export_name("ring_drain")))
uint32_t ring_drain(uint32_t idx, uint8_t *dst, uint32_t max_len) {
    region_entry_t *r = &bus.entries[idx];
    uint32_t *rh_ptr = (uint32_t *)(uintptr_t)r->data_offset;
    uint8_t  *data   = (uint8_t *)(uintptr_t)(r->data_offset + 4);
    uint32_t cap = r->data_size;
    uint32_t wh = r->aux;
    uint32_t rh = *rh_ptr;

    uint32_t count = 0;
    while (rh != wh && count < max_len) {
        dst[count++] = data[rh];
        rh = (rh + 1) % cap;
    }
    *rh_ptr = rh;
    return count;
}

__attribute__((export_name("ring_available")))
uint32_t ring_available(uint32_t idx) {
    region_entry_t *r = &bus.entries[idx];
    uint32_t rh = *(uint32_t *)(uintptr_t)r->data_offset;
    uint32_t wh = r->aux;
    return (wh - rh + r->data_size) % r->data_size;
}

// ── Slot operations ──

__attribute__((export_name("slot_mark_dirty")))
void slot_mark_dirty(uint32_t idx, uint32_t slot) {
    if (slot < 32) bus.entries[idx].dirty |= (1u << slot);
    bus.entries[idx].seq++;
    bus.global_seq++;
}

__attribute__((export_name("slot_drain_dirty")))
uint32_t slot_drain_dirty(uint32_t idx) {
    uint32_t mask = bus.entries[idx].dirty;
    bus.entries[idx].dirty = 0;
    return mask;
}

// ── Query functions ──

__attribute__((export_name("bus_addr")))
uint32_t bus_addr(void) { return (uint32_t)(uintptr_t)&bus; }

__attribute__((export_name("region_count")))
uint32_t region_count(void) { return bus.region_count; }

__attribute__((export_name("region_seq")))
uint32_t region_seq(uint32_t i) { return bus.entries[i].seq; }

__attribute__((export_name("region_offset")))
uint32_t region_offset(uint32_t i) { return bus.entries[i].data_offset; }

__attribute__((export_name("region_size")))
uint32_t region_size(uint32_t i) { return bus.entries[i].data_size; }

__attribute__((export_name("region_type")))
uint32_t region_type(uint32_t i) { return bus.entries[i].region_type; }

__attribute__((export_name("region_dirty")))
uint32_t region_dirty(uint32_t i) { return bus.entries[i].dirty; }

__attribute__((export_name("global_seq")))
uint32_t global_seq(void) { return bus.global_seq; }
