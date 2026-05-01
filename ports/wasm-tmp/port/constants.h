// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/chassis_constants.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/constants.h — Shared constants for C/JS FFI.
//
// This is the CANONICAL source.  The JS mirror (js/constants.mjs)
// must match exactly.  When adding constants, update both files.
//
// Convention: all constants are plain #defines (no enums) so the
// values are trivially portable to JS.
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer model)
//   design/behavior/01-hardware-init.md     (GPIO layout)
//   design/behavior/04-script-execution.md  (phase machine)

#ifndef PORT_CONSTANTS_H
#define PORT_CONSTANTS_H

// ── Frame return codes ──
// Returned by the frame function to tell JS what happened.
#define RC_DONE         0   // frame complete, no more work
#define RC_YIELD        1   // budget exhausted, more work pending
#define RC_EVENTS       2   // processed events, may need repaint
#define RC_ERROR        3   // something went wrong

// ── Port flags (port_state_t.flags) ──
// Bitfield in the port state struct, readable by both C and JS.
#define PF_INITIALIZED  (1 << 0)
#define PF_HAS_EVENTS   (1 << 1)
#define PF_HAL_DIRTY    (1 << 2)

// ── Port state struct offsets (port_state_t) ──
// Hard-coded byte offsets so JS can read the struct directly from
// linear memory without parsing.  Must match port_memory.h layout.
#define PS_PHASE         0
#define PS_SUB_PHASE     4
#define PS_FRAME_COUNT   8
#define PS_NOW_US       12
#define PS_BUDGET_US    16
#define PS_ELAPSED_US   20
#define PS_STATUS       24
#define PS_FLAGS        28

// ── VM execution state flags ──
// Track whether the VM is active, was interrupted by budget/abort,
// or has finished its current unit of work.
//
// With Option A (upstream supervisor owns mp_hal_delay_ms), the
// abort-resume yield goes through port_idle_until_interrupt().
// These flags track the VM's state as seen by the frame loop,
// NOT the delay loop (which the supervisor manages internally).
#define SF_ACTIVE       (1 << 0)
#define SF_YIELDED      (1 << 1)  // budget exhausted, resume next frame
#define SF_COMPLETE     (1 << 2)  // script/REPL line finished

// ── Event types ──
// Events delivered from JS to C via the event ring.
#define EVT_NONE            0x00
#define EVT_GPIO_CHANGE     0x01
#define EVT_ANALOG_CHANGE   0x02
#define EVT_KEY_DOWN        0x10  // keystroke → background task → readline
#define EVT_KEY_UP          0x11
#define EVT_WAKE            0x20  // generic wake from idle

// ── Event ring layout ──
#define EVENT_SIZE          8   // sizeof(port_event_t)
#define RING_HEADER_SIZE    8   // sizeof(event_ring_header_t)

// ── GPIO slot layout (12 bytes per pin) ──
// 32 pins total (0-31).  definition.json provides aliases and bus
// defaults; this layer only supplies the index/position.
// See design/behavior/01-hardware-init.md.
#define GPIO_SLOT_SIZE      12
#define GPIO_MAX_PINS       32

#define GPIO_ENABLED         0
#define GPIO_DIRECTION       1  // C owns: 0=input, 1=output
#define GPIO_VALUE           2  // C owns: 0=low/False, 1=high/True
#define GPIO_PULL            3  // JS owns: pull up/down toggle
#define GPIO_ROLE            4
#define GPIO_FLAGS           5
#define GPIO_CATEGORY        6
#define GPIO_LATCHED         7
// Bytes 8-11 reserved

// ── GPIO roles ──
#define ROLE_UNCLAIMED      0x00
#define ROLE_DIGITAL_IN     0x01
#define ROLE_DIGITAL_OUT    0x02
#define ROLE_ADC            0x03
#define ROLE_DAC            0x04
#define ROLE_PWM            0x05
#define ROLE_NEOPIXEL       0x06
#define ROLE_I2C            0x07
#define ROLE_SPI            0x08
#define ROLE_UART           0x09

// ── GPIO flags ──
// Per-pin dirty tracking for JS/C coordination.
#define GF_JS_WROTE         0x01  // JS wrote this slot since last C read
#define GF_C_WROTE          0x02  // C wrote this slot since last JS read
#define GF_C_READ           0x04  // C has read this slot at least once
#define GF_LATCHED          0x08  // value is latched (edge-triggered)

// ── C→JS notification types (ffi.notify) ──
// Lightweight notifications from C to JS during a frame.
#define NOTIFY_PIN_CHANGED   1   // C wrote a pin value
#define NOTIFY_TX_DATA       2   // serial TX has data
#define NOTIFY_WORK_DONE     3   // work completed
#define NOTIFY_ERROR         4   // error occurred

#endif // PORT_CONSTANTS_H
