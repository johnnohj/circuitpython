# WASM Port Design Notes

Architecture exploration and design sketches for the CircuitPython WASM port.
These documents capture design decisions and rationale — not implementation status.

## Documents

- [yield-suspend-separation.md](yield-suspend-separation.md) — MP_VM_RETURN_SUSPEND architecture, NLR sentinel propagation, future directions (WFE integration, per-task contexts, FRAME_* observability)
- [memfs-linear-memory.md](memfs-linear-memory.md) — MEMFS as snapshot, not alias; rationale for per-frame exported pointers (fd_write corruption hazard)

## Key Principles

1. **JS controls the supervisor, the supervisor controls Python.**
2. **Python runs "in JS"** — transparent, cooperative; not opaque/escaping.
3. **MEMFS endpoints are the universal I/O surface** — never bypass with C statics.
4. **No SharedArrayBuffer** — use transferable ArrayBuffers for main↔worker.
5. **Per-frame I/O uses direct linear memory, not WASI fds.** WASI fd operations
   (fd_write, fd_read, fd_seek) corrupt WASM linear memory when called per-frame
   because memory.grow() detaches memory.buffer, causing JS fd handlers to
   read/write stale DataView/Uint8Array references. Hot-path data (VM state out,
   event ring in) uses exported C pointers (`sh_state_addr()`, `sh_event_ring_addr()`)
   for direct linear memory access. WASI fds remain valid for init-time setup and
   low-frequency semihosting calls (fetch, timer, persist).
6. **Single VM, single worker.** Earlier split-port explorations (supervisor.wasm
   + fiber.wasm, reactor+worker) are abandoned. The single-VM-in-Worker model
   with OffscreenCanvas for display is what ships.
