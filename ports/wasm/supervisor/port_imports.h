/*
 * supervisor/port_imports.h — Synchronous WASM imports for common-hal.
 *
 * Common-hal modules call these to get data from JS synchronously.
 * From Python's perspective, the call chain is:
 *   Python → shared-bindings → common-hal → WASM import → JS → return
 * The entire chain is synchronous — no SUSPEND needed.
 *
 * JS implements these in the "port" import module.  If JS doesn't
 * provide an import, the C-side fallback is used (NaN, zeros, etc.).
 */
#pragma once

#include <stdint.h>

/* ── CPU info ── */

/* Returns simulated CPU temperature in millidegrees C (e.g. 25000 = 25.0°C).
 * Returns INT32_MIN if not available. */
__attribute__((import_module("port"), import_name("getCpuTemperature")))
extern int32_t port_get_cpu_temperature(void);

/* Returns simulated CPU voltage in millivolts (e.g. 3300 = 3.3V).
 * Returns 0 if not available. */
__attribute__((import_module("port"), import_name("getCpuVoltage")))
extern int32_t port_get_cpu_voltage(void);

/* ── Random ── */

/* ── Time ── */

/* Returns monotonic time in milliseconds (more precise than per-frame
 * wasm_js_now_ms which only updates once per frame).
 * JS implements via performance.now(). */
__attribute__((import_module("port"), import_name("getMonotonicMs")))
extern uint32_t port_get_monotonic_ms(void);
