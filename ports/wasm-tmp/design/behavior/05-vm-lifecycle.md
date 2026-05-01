# Stage 15-16: cleanup_after_vm, start_mp

Back to [README](README.md)

---

## 15. cleanup_after_vm

| Upstream | Our port |
|----------|----------|
| Save traceback to port memory | Save traceback (TBD) |
| reset_devices, reset_displays | Reset display |
| reset_port, reset_board | Reset HAL, clear pin claims |
| filesystem_flush | Flush WASI filesystem |
| gc_deinit, free pystack, free heap | Reset GC, reset pystack pointer |
| reset_all_pins | Zero MEMFS pin slots |

**Our behavior**: Should mirror upstream closely.  The main difference
is that our "free" operations are pointer resets (the memory is static
in port_mem), not actual frees.

**Deviation**: Structural (memory model), but the cleanup sequence
and its effects should be identical from the VM's perspective.

**Gap**: Need to verify our cleanup sequence matches upstream ordering.
Order matters -- e.g., displays must flush before GC is torn down.

### Upstream cleanup ordering (from main.c:339)

For reference, the exact upstream sequence:

1. Save traceback to port memory (off-heap copy)
2. `reset_devices()` -- port-independent devices (BLE HCI)
3. `atexit_reset()`
4. `reset_displays()` -- flush and disable displayio
5. `memorymonitor_reset()`
6. `bleio_user_reset()`
7. `common_hal_canio_reset()`
8. `keypad_reset()`
9. `socketpool_user_reset()`
10. `wifi_user_reset()`
11. `reset_board_buses()` -> `reset_port()` -> `reset_board()`
12. `filesystem_flush()`
13. `stop_mp()` -- gc_deinit, free pystack, free heap
14. `reset_all_pins()`
15. `supervisor_workflow_reset()`

Most of steps 2-10 are no-ops on our port (no BLE, CAN, WiFi, etc.),
but the ordering of display reset (4) before GC teardown (13) and
pin reset (14) after GC teardown matters.

### What "cleanup" means for boot.py -> code.py transition

Upstream, `cleanup_after_vm` after boot.py fully tears down the GC
heap, frees the pystack, and calls `gc_deinit()`.  Then `start_mp()`
reinitializes everything fresh for code.py.  boot.py's Python globals
do not survive -- only supervisor-level side effects persist (autoreload
config, filesystem permissions, USB endpoint config).

Whether we preserve this isolation or allow boot.py globals to carry
over into code.py is a design decision (see
[04-script-execution.md](04-script-execution.md), stage 12 note).

---

## 16. start_mp (VM initialization)

| Upstream | Our port |
|----------|----------|
| mp_stack_ctrl_init | mp_cstack_init_with_sp_here |
| filesystem_flush | Flush WASI fs |
| readline_init0 | Reset C-side readline state |
| Allocate pystack (settings.toml configurable) | Use fixed pystack from port_mem |
| Allocate heap (settings.toml configurable) | Use fixed heap from port_mem |
| gc_init | gc_init over port_mem.gc_heap |
| mp_init | mp_init |
| Set sys.path, sys.argv | Set sys.path, sys.argv |

**Our behavior**: Initialize pystack and GC heap from fixed regions in
port_mem.  Call mp_init.  Set up sys.path.  Mount VFS.

**Deviation**: Structural (fixed-size memory from port_mem vs dynamic
allocation).

### settings.toml support (decided)

We will support `settings.toml` for heap sizing, but with a limited
set of options rather than fully customizable values.  Provide one or
two size presets relative to the default heap (e.g., "large" and
"small", or two larger tiers).  This avoids the complexity of
arbitrary sizing while still giving users a knob to turn.

### mp_thread_init ordering (decided)

`mp_thread_init()` must be called before `mp_init()` in all
environments.  This is the unix port pattern and is required for
asyncio support.  Our port enables `MICROPY_PY_THREAD` -- the thread
functions are not no-ops but provide the execution context that
asyncio depends on.  See the "stubs and dismissed functionality"
section of port-consolidation.md.
