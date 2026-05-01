/*
 * chassis/ffi_imports.h — WASM imports: C calls into JS.
 *
 * These functions are provided by JS at instantiation time under the
 * "ffi" import module.  They let C communicate with JS without going
 * through WASI fd_write or any file-based I/O.
 *
 * All data exchange uses WASM linear memory.  The imports are
 * notifications or requests — actual state is in MEMFS regions.
 *
 *   const imports = {
 *       ffi: {
 *           request_frame: () => { ... },
 *           notify: (type, pin, arg, data) => { ... },
 *       },
 *       memfs: { register: ... },
 *       wasi_snapshot_preview1: { ... },
 *   };
 */

#ifndef CHASSIS_FFI_IMPORTS_H
#define CHASSIS_FFI_IMPORTS_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* request_frame — ask JS to schedule a rAF callback                   */
/*                                                                     */
/* Called when C knows there's more work (e.g., after yielding).       */
/* JS should call chassis_frame() on the next animation frame.         */
/* Idempotent: calling twice before the frame fires is fine.           */
/* ------------------------------------------------------------------ */

__attribute__((import_module("ffi"), import_name("request_frame")))
void ffi_request_frame(void);

/* ------------------------------------------------------------------ */
/* notify — C→JS notification (no response expected)                   */
/*                                                                     */
/* type: NOTIFY_* constant from chassis_constants.h                    */
/* pin:  pin/channel index (0 if not pin-specific)                     */
/* arg:  type-specific argument                                        */
/* data: type-specific data                                            */
/*                                                                     */
/* JS reads the actual state from MEMFS — this is just a nudge.       */
/* ------------------------------------------------------------------ */

__attribute__((import_module("ffi"), import_name("notify")))
void ffi_notify(uint32_t type, uint32_t pin, uint32_t arg, uint32_t data);

#endif /* CHASSIS_FFI_IMPORTS_H */
