# sync-bus: frameless memory synchronization service

A standalone WASM binary + JS module that provides named-region storage
with dirty tracking, sequence numbers, and automatic transport selection
(SharedArrayBuffer when available, postMessage+Transferable when not).

## Design principles

- **Frameless**: no coordination between reader and writer clocks
- **Sample, don't replay**: readers see latest state, may miss intermediates
- **Ring buffers are FIFO**: serial data is never lost, just drained at reader's pace
- **Transport-agnostic**: endpoints don't know if they're reading shared memory or posted buffers
- **Tiny WASM**: no libc, no WASI — just integer ops on linear memory

## Region types

| Type | Semantics | Dirty tracking |
|------|-----------|----------------|
| `slab` | Opaque byte range (framebuffer) | Per-region sequence number |
| `ring` | FIFO byte stream (serial) | Write head != read head |
| `slots` | Fixed-size records (GPIO) | Per-slot dirty bitmask |

## Memory layout (WASM linear memory)

```
Offset 0: Region table header
  [0..3]   region_count (u32)
  [4..7]   total_allocated (u32)
  [8..11]  global_seq (u32)
  [12..15] reserved

Offset 16: Region table entries (32 bytes each, max 64 regions)
  [0..3]   name_hash (u32) — FNV-1a of region name
  [4..7]   data_offset (u32) — offset into linear memory
  [8..11]  data_size (u32)
  [12..15] region_type (u32) — 0=slab, 1=ring, 2=slots
  [16..19] seq (u32) — bumped on every write
  [20..23] dirty (u32) — bitmask for slots, boolean for slab
  [24..27] slot_size (u32) — only for type=slots
  [28..31] aux (u32) — ring: write_head; slots: slot_count

Offset 2064: Region data starts here
  (dynamically allocated by region_create)
```

## C source (sync.c)

Compiles to ~2KB WASM. No libc. Exported functions only.

## JS module (sync.mjs)

Three classes:
- `SyncBus` — owns the WASM instance and memory, creates endpoints
- `SyncPort` — an endpoint view over the bus (read/write/drain/push)
- `SyncTransport` — abstract; `SharedTransport` and `PostMessageTransport`

## API

```js
const bus = await SyncBus.create();

bus.region('serial_tx', { size: 4096, type: 'ring' });
bus.region('framebuffer', { size: 345600, type: 'slab' });
bus.region('gpio', { size: 384, type: 'slots', slotSize: 12 });

const vmPort = bus.port('vm');       // for the Worker
const uiPort = bus.port('ui');       // for the main thread

// Worker gets vmPort via postMessage
worker.postMessage({ port: vmPort.transfer() }, vmPort.transferList());

// Writer (VM side):
vmPort.write('gpio', slotIndex, data);  // marks slot dirty, bumps seq
vmPort.push('serial_tx', bytes);        // advances ring write head
vmPort.writeSlab('framebuffer', rgbData); // bumps seq

// Reader (UI side):
if (uiPort.changed('framebuffer')) {
    const fb = uiPort.read('framebuffer');  // zero-copy view or snapshot
    paintCanvas(fb);
}
const bytes = uiPort.drain('serial_tx');    // FIFO drain
const dirtySlots = uiPort.dirty('gpio');    // which slots changed
for (const i of dirtySlots) {
    updateSvgPin(i, uiPort.readSlot('gpio', i));
}
```
