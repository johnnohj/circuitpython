# HAL GPIO Endpoint Design — Per-Pin Files + Template/Active Split

## Status: Future direction (design only)

Captures ideas from 2026-04-24 discussion. Not yet implemented.
Current commit consolidates pin_meta into the flat `/hal/gpio` blob
and fixes SH_EVT_HW_CHANGE routing.

---

## Current model

```
/hal/gpio          flat binary blob, 16 bytes × 64 pins
                   lseek(fd, pin * 16, SEEK_SET) + read/write
                   one fd shared by all common-hal modules
```

All pins share one file. Pin state is addressed by offset arithmetic.
JS reads the whole blob; C seeks to the right offset.

## Proposed model: per-pin files

```
/hal/gpio/0        pin 0 template (16 bytes)
/hal/gpio/1        pin 1 template
...
/hal/gpio/active/0 pin 0 live state (claimed by Python)
/hal/gpio/active/1 pin 1 live state
```

### Template files (`/hal/gpio/N`)

Populated at boot by `hal_init_pin_categories()` from the board table.
Contain the board's intended configuration for each pin:

```
enabled=1, direction=input, pull=up, category=BUTTON, role=UNCLAIMED
```

These are the power-on defaults. They survive soft reboot. JS can read
them to know what the board designer intended (render a button widget
for BUTTON_A, a slider for A0, etc.) without needing a separate
`definition.json`.

### Active files (`/hal/gpio/active/N`)

Created when Python claims a pin (`digitalio.DigitalInOut(board.D5)`).
The port copies from `/hal/gpio/5` to `/hal/gpio/active/5` as the
starting point, then Python's configuration diverges:

```
enabled=1, direction=output, pull=none, role=DIGITAL_OUT
```

Common-hal opens the active file during construct and stores the fd
in `digitalinout_obj_t`. All subsequent reads/writes use that fd —
no seeking, no shared fd contention.

### Reset

On `cp_cleanup()` / soft reboot:
- Delete all `/hal/gpio/active/*` files (or copy templates back)
- Board defaults are restored without re-running init

On `never_reset` pins: the active file persists across reset.

### Claiming model

The fd *is* the claim. `hal/gpio/active/5` existing means pin 5 is
claimed. `claim_pin()` creates the file; `reset_pin_number()` deletes
it. No separate claim tracking needed.

Common-hal construct:
```c
// Open or create active file for this pin
char path[32];
snprintf(path, sizeof(path), "/hal/gpio/active/%d", pin->number);
self->fd = open(path, O_RDWR | O_CREAT, 0644);

// Copy template if fresh claim
char tpl_path[32];
snprintf(tpl_path, sizeof(tpl_path), "/hal/gpio/%d", pin->number);
int tpl_fd = open(tpl_path, O_RDONLY);
if (tpl_fd >= 0) {
    uint8_t slot[16];
    read(tpl_fd, slot, 16);
    close(tpl_fd);
    // Modify for Python's configuration
    slot[HAL_GPIO_OFF_ROLE] = HAL_ROLE_DIGITAL_IN;
    write(self->fd, slot, 16);
}
```

Subsequent reads/writes:
```c
// No lseek needed — file is exactly 16 bytes
lseek(self->fd, 0, SEEK_SET);
read(self->fd, slot, 16);
```

### JS observation

JS reads individual pin files instead of parsing a blob:
```js
const data = memfs.readFile('/hal/gpio/active/5');
// data is a 16-byte Uint8Array — the complete pin state
```

For UI rendering, JS can list `/hal/gpio/active/` to find claimed pins
and `/hal/gpio/` to find all board pins. The template tells JS "this
pin is BUTTON_A" even before Python claims it.

---

## Slot compression ideas

These are independent of per-pin files and can be applied to either
the flat blob or per-pin model.

### Compress enabled + never_reset → tri-state

```
enabled: int8_t
  -1 = never_reset (persists across soft reboot)
   0 = disabled
   1 = enabled
```

Saves 1 byte. `never_reset` is a *kind* of enabled, not independent.

### Compress open_drain into direction

```
direction: uint8_t
  0 = input
  1 = output (push-pull)
  2 = output (open-drain)
```

Saves 1 byte. Open drain is a drive mode, not independent of direction.
Matches how real hardware models it (STM32 GPIO_MODE_OUTPUT_OD etc.)

### Collapse role + category?

Category = "board says this is BUTTON_A" (static, survives reset).
Role = "Python is using it as DIGITAL_IN" (runtime, cleared on deinit).

They differ when Python uses a pin for something other than its designed
purpose (LED pin as general output, analog pin as digital).

Options:
- **Keep both** (current plan): role is runtime, category is static.
  6 bytes of metadata (role + flags + category + latched + 2 reserved).
- **Category in template only**: template file has category, active file
  doesn't need it. Role is the only runtime field. Saves 1 byte in
  active state.
- **Merge into one field with "designed" bit**: `role | 0x80` means
  "using as designed." Awkward and loses the independent semantics.

Recommendation: keep both for now. Category in the template file,
role in the active file. When per-pin files land, category naturally
lives in the template and doesn't need to be in the active slot.

### Compressed slot layout (12 bytes, from 16)

```
[0] enabled     int8: -1=never_reset, 0=disabled, 1=enabled
[1] direction   uint8: 0=input, 1=output, 2=output_open_drain
[2] value       uint8: 0/1
[3] pull        uint8: 0=none, 1=up, 2=down
[4] role        uint8: HAL_ROLE_*
[5] flags       uint8: HAL_FLAG_*
[6] category    uint8: HAL_CAT_*
[7] latched     uint8: captured input value
[8-11] reserved
```

12 bytes vs. current 8 (with pin_meta separate) or 16 (uncompressed).
Power of 2 isn't required — MEMFS doesn't care about alignment.

---

## Event ring interaction

Per-pin files don't change the event ring model. SH_EVT_HW_CHANGE
still carries `(pin, hal_type, value)`. The C event handler writes to
`/hal/gpio/active/N` instead of seeking into a flat blob. The active
file might not exist yet (pin not claimed) — handler checks and skips.

For unclaimed pins, the handler could write to the template file
instead, updating the "external input" state even before Python claims
the pin. When Python does claim it, the template already has the
latest value. This matches real hardware: a button is being pressed
before the firmware configures the GPIO.

---

## Implementation sequence

1. **Current commit**: consolidate pin_meta into flat `/hal/gpio` blob
   (16 bytes/pin), fix SH_EVT_HW_CHANGE routing, apply compressions.

2. **Next commit**: split flat blob into per-pin template files.
   Change common-hal to use per-pin fds. Update JS readers.

3. **Follow-up**: add `/hal/gpio/active/` claiming model.
   Change construct/deinit to create/delete active files.
   Update JS to observe active directory for live pins.
