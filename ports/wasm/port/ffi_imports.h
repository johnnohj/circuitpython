// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/ffi_imports.h by CircuitPython contributors
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/port_imports.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/ffi_imports.h — All WASM imports (C calls into JS).
//
// These functions are provided by JS at WASM instantiation time.
// They let C communicate with JS without going through WASI fd I/O.
// All data exchange uses WASM linear memory — imports are notifications
// or synchronous queries, actual state lives in MEMFS regions.
//
// Import modules:
//   "ffi"  — frame scheduling and notifications
//   "port" — synchronous queries for common-hal (CPU info, time)
//
// JS instantiation:
//   const imports = {
//       ffi:  { request_frame, notify },
//       port: { getCpuTemperature, getCpuVoltage, getMonotonicMs },
//       memfs: { register },
//       wasi_snapshot_preview1: { ... },
//   };
//
// Design refs:
//   design/wasm-layer.md  (wasm layer, JS↔C communication)

#ifndef PORT_FFI_IMPORTS_H
#define PORT_FFI_IMPORTS_H

#include <stdint.h>

// ── ffi.request_frame ──
// Ask JS to schedule the next chassis_frame() call.
//
// hint values (matching ports/wasm convention):
//   0          = ASAP (requestAnimationFrame)
//   1..N       = delay N ms (setTimeout)
//   0xFFFFFFFF = idle, don't schedule (_kick restarts on user event)

__attribute__((import_module("ffi"), import_name("request_frame")))
void ffi_request_frame(uint32_t hint_ms);

// ── ffi.notify ──
// C→JS notification (no response expected).
// JS reads actual state from MEMFS — this is just a nudge.
//
//   type: NOTIFY_* constant from constants.h
//   pin:  pin/channel index (0 if not pin-specific)
//   arg:  type-specific argument
//   data: type-specific data

__attribute__((import_module("ffi"), import_name("notify")))
void ffi_notify(uint32_t type, uint32_t pin, uint32_t arg, uint32_t data);

// ── port.getCpuTemperature ──
// Returns simulated CPU temperature in millidegrees C (e.g. 25000 = 25.0°C).
// Returns INT32_MIN if not available.

__attribute__((import_module("port"), import_name("getCpuTemperature")))
extern int32_t port_get_cpu_temperature(void);

// ── port.getCpuVoltage ──
// Returns simulated CPU voltage in millivolts (e.g. 3300 = 3.3V).
// Returns 0 if not available.

__attribute__((import_module("port"), import_name("getCpuVoltage")))
extern int32_t port_get_cpu_voltage(void);

// ── port.getMonotonicMs ──
// Returns monotonic time in milliseconds.
// JS implements via performance.now().
// More precise than port_mem.js_now_ms (which only updates once per frame).

__attribute__((import_module("port"), import_name("getMonotonicMs")))
extern uint32_t port_get_monotonic_ms(void);

#endif // PORT_FFI_IMPORTS_H
