# MEMFS as Linear Memory: Alias vs Snapshot

## Decision: Snapshot (Approach B)

### The two approaches

**Approach A (alias):** MEMFS Map entries point directly into WASM linear
memory at known offsets.  JS reads the same bytes C writes to.  Zero-copy
but requires JS to know struct layouts, and each WASM instance has its
own linear memory.

**Approach B (snapshot):** C writes structured state to MEMFS fd endpoints
each cp_step().  JS reads from the Map.  Both sides speak a protocol,
neither needs to understand the other's memory layout.

### Why snapshot wins

With the fiber architecture (multiple WASM instances sharing one MEMFS Map):

1. **Multiple linear memories.** Each fiber has its own.  Aliasing requires
   tracking {instance, offset, length} per fiber.  Snapshot: each fiber
   just writes to its own /sys/ or /fiber/N/ paths.

2. **Cross-context transfer.** The main thread can't alias into a worker's
   WASM memory without SharedArrayBuffer.  Snapshots transfer naturally
   as ArrayBuffers via postMessage.

3. **Decoupling.** The whole point of MEMFS-as-protocol is that neither
   side needs to understand the other's memory layout.  Aliases reintroduce
   tight coupling the architecture is designed to eliminate.

4. **Fiber pooling.** Reassigning a warm fiber to different .mpy bytecode
   would require updating all aliases.  Snapshot: the fiber just writes
   fresh state to MEMFS on its next cp_step().

5. **Consistency.** /hal/ endpoints, /sys/ semihosting, and internal VM
   state all use the same pattern: write to fd, read from Map.

### Future possibilities

- **SharedArrayBuffer:** A future version of the port could support SAB
  for true zero-copy shared memory between threads.  The snapshot protocol
  would still work as a fallback for environments where SAB is unavailable
  (e.g., missing COOP/COEP headers).

- **WASM Component Model:** Could introduce a shared-memory module that
  multiple components access, providing a central buffer without SAB.
  The MEMFS protocol could wrap this transparently.

### What this means for PLACE_IN_*TCM

The PLACE_IN_DTCM_BSS/DATA macros currently expand to identity (no-op).
With snapshot approach, these should:

1. Continue to allocate in C linear memory as normal (static arrays)
2. The supervisor exports relevant state to MEMFS endpoints each cp_step()
3. JS reads MEMFS, never peers into linear memory directly

The key discipline: any data that JS needs to see MUST go through a
MEMFS fd write, not remain hidden in C statics.  The macros themselves
don't need to change — the export step in cp_step() is what matters.

### What this means for pystack

Pystack buffer lives in C linear memory (static array).  JS doesn't
read the raw pystack.  Instead:

- /sys/state includes pystack usage (vm_depth field)
- Fiber yield state is communicated via yield_reason + yield_arg
- If JS needs to checkpoint a fiber's pystack (for context switching),
  the fiber exports it to a MEMFS path — not by JS aliasing into
  linear memory

This preserves the "neither side understands the other's layout" principle.

## Caveat: Snapshot-via-fd is not safe per-frame

The snapshot approach (Approach B) assumes fd writes to export state each cp_step().
In practice, WASI fd operations (fd_write, fd_read, fd_seek) corrupt WASM linear
memory when called per-frame. The cause: memory.grow() detaches memory.buffer, and
JS fd handlers that created DataView/Uint8Array from the old buffer silently
read/write stale memory.

**Per-frame hot-loop data must use direct linear memory access instead:**

- `sh_state_addr()` — JS reads VM state directly from WASM memory (no fd)
- `sh_event_ring_addr()` — JS writes events into a ring buffer in WASM memory (no fd)
- Ring buffer layout: `[write_idx:u32] [read_idx:u32] [entries: N * sh_event_t]`

JS re-creates its DataView from `instance.exports.memory.buffer` at the top of each
frame, after any memory.grow() has settled. This is safe because the buffer reference
is fresh. WASI fd handlers, by contrast, may hold stale references from earlier in
the frame.

The snapshot-via-fd model remains correct for data that does not flow per-frame:
init-time filesystem setup, /hal/ endpoint updates, semihosting calls (fetch, timer,
persist). The principle "data JS needs must go through MEMFS" still holds — but for
hot-path data, the MEMFS layer is bypassed in favor of exported pointers into the
single linear memory.
