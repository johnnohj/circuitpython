# VM Contract — What the Chassis Provides

The chassis is a standalone port program that manages memory, hardware,
frame budgets, and FFI.  The VM (MicroPython/CircuitPython) bolts on by
using the regions and APIs the chassis provides.

## Memory regions

All regions are in `port_memory_t` — a single static struct in WASM linear
memory.  Every region is also a MEMFS file (same bytes, zero copy).

| MEMFS path | C accessor | Size | Purpose |
|---|---|---|---|
| `/py/heap` | `vm_gc_heap()` | 256K (PoC) / 512K (real) | GC heap for `gc_init()` |
| `/py/ctx/N/pystack` | `vm_pystack(N)` | 8K per context | Per-context pystack for `mp_pystack_init()` |
| `/py/ctx/meta` | `vm_ctx_meta(N)` | 16 bytes × 4 | Context status, priority, yield state |
| `/py/input` | `vm_input_buf()` | 4K | JS→C string passing (REPL input, cp_exec) |
| `/port/state` | `port_state()` | 32 bytes | Frame count, budget, elapsed, flags |
| `/port/stack` | `port_stack()` | 264 bytes | Resumable execution stack |

## What the VM needs to do

### At init (`mp_init` time)
```c
// GC heap — chassis owns the memory, VM initializes it
gc_init(vm_gc_heap(), vm_gc_heap() + vm_gc_heap_size());

// Pystack — one per context
mp_pystack_init(vm_pystack(0), vm_pystack(0) + vm_pystack_size());

// Context metadata — set status to IDLE
vm_ctx_meta(0)->status = CTX_IDLE;
```

### Each frame (called from `port_step()` VM phase)
```c
// The chassis calls this during Phase 3 of chassis_frame().
// Budget is already ticking — check budget_soft_expired() between
// bytecode instruction batches.

int vm_phase_step(void) {
    // 1. Check if any context is runnable
    // 2. Restore context state from vm_ctx_meta(active)
    // 3. Call mp_execute_bytecode() for a budget slice
    // 4. On yield: save state to vm_ctx_meta, return RC_YIELD
    // 5. On completion: set status = CTX_DONE, return RC_DONE
}
```

### Budget enforcement
The VM checks `budget_soft_expired()` at backwards branches (HOOK_LOOP).
When expired:
- Save bytecode IP/SP to pystack (already there — pystack is in linear memory)
- Save context metadata to `vm_ctx_meta(active)`
- Return `PORT_RC_YIELD` from the VM phase
- Chassis calls `ffi_request_frame()` to schedule next frame
- Next frame: chassis calls VM phase again, VM reads state, continues

### Halt/resume contract
**The VM does NOT need to save/restore anything special.**  All VM state
lives in MEMFS-backed linear memory:
- Pystack frames → `/py/ctx/N/pystack` (bytecode IP, SP, locals)
- GC heap → `/py/heap` (all Python objects)
- Context metadata → `/py/ctx/meta` (status, yield point)

When `chassis_frame()` returns, the C stack evaporates.  When it's called
again next frame, the VM reads its state from the same addresses.  The
state was never on the C stack.

### machine.mem32 integration
The chassis validates addresses via `port_memory_validate_addr()`.
`machine.mem32[addr]` dereferences `addr` directly — it's linear memory.
The validation just confirms the address falls in a known MEMFS region.

```c
uintptr_t mod_machine_mem_get_addr(mp_obj_t addr_o, uint align) {
    uintptr_t addr = mp_obj_get_int_truncated(addr_o);
    if (!chassis_validate_addr(addr, align)) {
        mp_raise_ValueError("address not in mapped region");
    }
    return addr;
}
```

## What the chassis provides to the VM

| Service | API | Notes |
|---|---|---|
| Frame budget | `budget_soft_expired()`, `budget_elapsed_us()` | VM checks at HOOK_LOOP |
| Hardware state | `gpio_slot(pin)`, `hal_read_pin()` | Direct pointer access |
| Serial I/O | `serial_rx_read()`, `serial_tx_write()` | Ring buffers in linear memory |
| Event notification | Events arrive in `/port/event_ring`, drained before VM phase | VM never touches the ring |
| C→JS notification | `ffi_notify()`, `ffi_request_frame()` | VM can notify JS of output |
| Address validation | `port_memory_validate_addr()` | For machine.mem32 safety |
| Context scheduling | `vm_ctx_meta(N)->status` | Chassis can pick next context |

## What the chassis does NOT provide

- **mp_init / mp_deinit** — the VM owns its own initialization
- **Bytecode compilation** — the VM compiles Python source
- **GC collection** — the VM owns GC; chassis just provides the heap region
- **Module system** — the VM loads modules; chassis provides the filesystem (WASI)
- **Exception handling** — the VM handles exceptions internally

## Memory map (machine.mem32 address space)

JS can enumerate regions via `chassis_mem_map_count()` and reading the
`/port/state` MEMFS region.  The memory map is built at init from the
static `port_memory_t` layout.

```
port_mem base (e.g., 0x10E58):
  +0x00000  /port/state          32 bytes
  +0x00020  /port/stack         264 bytes
  +0x00128  /port/event_ring    512 bytes
  +0x00328  /hal/gpio           768 bytes
  +0x00628  /hal/analog         256 bytes
  +0x00728  /hal/serial/rx     4096 bytes
  +0x01728  /hal/serial/tx     4096 bytes
  +0x02728  (dirty flags)        20 bytes
  +0x0273C  /py/heap          256K bytes
  +0x4273C  /py/ctx/0/pystack   8K bytes
  +0x4473C  /py/ctx/1/pystack   8K bytes
  +0x4673C  /py/ctx/2/pystack   8K bytes
  +0x4873C  /py/ctx/3/pystack   8K bytes
  +0x4A73C  /py/ctx/meta        64 bytes
  +0x4A77C  /py/input          4096 bytes
  +0x4B77C  vm state             8 bytes
```

Total: ~309K (dominated by the 256K GC heap).
