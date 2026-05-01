# Stage 1-3: Hardware Init, Pin Reset, Heap Init

Back to [README](README.md)

---

## 1. Hardware init (`port_init`)

| Upstream | Our port |
|----------|----------|
| Initialize CPU clocks, peripherals, DMA | No real hardware |
| Detect hardware faults -> safe mode | No faults possible |
| Mark never-reset pins | N/A |

**Our behavior**: Initialize port memory (`port_mem`).  Register MEMFS
regions so JS can see port state.  Initialize the event ring.  This is
the equivalent of "powering on the board."

**Deviation**: Structural.  No hardware to initialize.

### Pin initialization model (decided)

The port compiles with 32 pin slots (indices 0-31), allocated as MEMFS
regions at fixed offsets in `port_mem`.  The C code supplies only the
position/index.  `definition.json` (loaded by JS before any C code
runs) provides:

- Pin name aliases (e.g., index 5 = "D5" = "SCK")
- Bus defaults (which pins form SPI, I2C groups — fidelity TBD)
- Which pins are "enabled" / "available" on this board

JS populates the MEMFS pin slots after WASM instantiation but before
`chassis_init()`, so C boots into a fully-configured pin table.  Users
can provide their own `definition.json` to define custom board pinouts.

**Responsibility split**:

| Concern | Owner | Why |
|---------|-------|-----|
| Direction (input/output) | C (common-hal) | Electrical logic, part of digitalio contract |
| Value (high/low, True/False) | C (common-hal) | Same |
| Pull up / pull down | JS | Simpler toggle event from UI; JS notifies C via event ring |
| Pin naming / aliases | JS (definition.json) | Runtime-configurable, not compiled in |
| MEMFS slot layout | C (port_memory.h) | Fixed at compile time, 32 slots x 12 bytes |

**Compile-time**: MAX_PINS=32, slot size, MEMFS offsets.
**Runtime**: Which pins are active, their names, bus assignments.

---

## 2. Pin reset (`reset_all_pins`)

| Upstream | Our port |
|----------|----------|
| Set all GPIO to input/floating | Clear all MEMFS pin slots to defaults |
| Respect never-reset pins | No never-reset pins (all simulated) |

**Our behavior**: Zero all `/hal/gpio` slots.  Every pin starts as
unclaimed, no role, no category.

**Deviation**: None in spirit.  The substrate differs but the effect
(clean pin state) is the same.

`never_reset_pin` API exists upstream (microcontroller module) but as
a simulator we don't need this level of detail.  No pins are connected
to buses or displays that would be disrupted by reset.  Present
behavior stands.

---

## 3. Heap init (`port_heap_init`)

| Upstream | Our port |
|----------|----------|
| Configure port heap regions (PSRAM, SRAM) | N/A -- GC heap is a fixed region in port_mem |

**Our behavior**: No-op.  The GC heap is a static array in `port_mem`,
sized at compile time.  `gc_init()` is called during `start_mp()`.

**Deviation**: Structural.  WASM linear memory is flat -- no need for
runtime heap discovery.

`port_heap_init()` is optional upstream (provided by the supervisor
shared layer, ports can override).  We have no reason to separate
heap creation from `port_init` -- verify it's optional upstream, then
fold it into our init sequence or leave as no-op.
