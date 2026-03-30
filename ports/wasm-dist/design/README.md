# WASM Port Design Notes

Architecture exploration and design sketches for the CircuitPython WASM port.
These documents capture design decisions and rationale — not implementation status.

## Documents

- [fiber-coroutine-architecture.md](fiber-coroutine-architecture.md) — Split build with supervisor.wasm + fiber.wasm + JS hardware modules
- [semihosting-syscall-table.md](semihosting-syscall-table.md) — WASM semihosting protocol via MEMFS /sys/ endpoints
- [memfs-linear-memory.md](memfs-linear-memory.md) — MEMFS as snapshot, not alias; rationale for the fiber architecture

## Key Principles

1. **JS controls the supervisor, the supervisor controls Python.**
2. **Python runs "in JS"** — transparent, cooperative; not opaque/escaping.
3. **MEMFS endpoints are the universal I/O surface** — never bypass with C statics.
4. **No SharedArrayBuffer** — use transferable ArrayBuffers for main↔worker.
5. **Semihosting model** — write request to known MEMFS path, yield, host fulfills.
6. **Per-frame I/O uses direct linear memory, not WASI fds.** — WASI fd operations
   (fd_write, fd_read, fd_seek) corrupt WASM linear memory when called per-frame
   because memory.grow() detaches memory.buffer, causing JS fd handlers to
   read/write stale DataView/Uint8Array references. Hot-path data (VM state out,
   event ring in) uses exported C pointers (`sh_state_addr()`, `sh_event_ring_addr()`)
   for direct linear memory access. WASI fds remain valid for init-time setup and
   low-frequency semihosting calls (fetch, timer, persist).
