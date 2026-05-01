# Stage 4-6: Serial Early Init, Safe Mode, Stack Init

Back to [README](README.md)

---

## 4. Serial early init (`serial_early_init`)

| Upstream | Our port |
|----------|----------|
| Configure UART pins, baud rate | Initialize serial ring buffers in port_mem |
| Print "Serial console setup" | Print banner to TX ring (JS reads it) |

**Our behavior**: Initialize RX and TX ring buffer head/tail pointers.
The actual transport varies by runtime environment (see
[06-runtime-environments.md](06-runtime-environments.md)).

**Deviation**: Environmental.  No UART hardware, but the ring buffer
provides the same abstraction to the VM.

This is optional upstream (ports provide it if they have early serial).
On Node, serial init may need to happen earlier than in the browser
(to capture early debug output).  Flag for review as runtime
differences are clarified.

---

## 5. Safe mode window (`wait_for_safe_mode_reset`)

| Upstream | Our port |
|----------|----------|
| Wait 500-1000ms for user button press | No physical button |

**Our behavior**: Skip entirely -- always returns `SAFE_MODE_NONE`.

**Deviation**: Environmental.  No physical reset button.

**Decision**: Gate behind `ENABLE_SIM_SAFE_MODE` flag, implemented as
a no-op with documentation.  Not enabled on any planned builds.  Leaves
the door open for future work (JS-triggered safe mode via URL parameter
or virtual button).

---

## 6. Stack init (`stack_init`)

| Upstream | Our port |
|----------|----------|
| Measure C stack boundaries | `mp_cstack_init_with_sp_here(16*1024)` |
| Set stack limit with margin | Fixed 16K limit |
| Fill with sentinel for measurement | No sentinel |

**Our behavior**: Set a fixed stack limit.  The JS runtime manages the
actual WASM stack; we trust it won't overflow silently.

**Deviation**: Structural.  WASM stack is managed by the engine, not
by us.  We set a conservative limit to prevent deep recursion from
exhausting the WASM stack before we can catch it.

### port_stack_get_top / port_stack_get_limit (decided)

These functions can return real pointers.  We don't run the full
upstream supervisor, so it's unclear whether they're currently wired
to anything.  On typical ports they're used for:

- GC split heap calculations (finding the largest usable memory area)
- Stack overflow detection (`stack_ok()`)
- NLR unwinding / context switching between supervisor and port code

We should return meaningful values (e.g., pointers into port_mem
regions), even if the upstream consumers aren't all active yet.
`stack_ok()` should check against the 16K limit we set rather than
always returning true.

**Open**: Whether `port_stack_get_top` should point to the pystack
region (our effective "stack") or to the actual WASM C stack.  The
answer depends on what the callers actually do with the value.  Audit
during Phase 3 (chunk 3.2, gccollect.c) and Phase 4 (supervisor/).
