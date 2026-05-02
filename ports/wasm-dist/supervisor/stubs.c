// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/stubs.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// supervisor/stubs.c — Functions referenced by shared code but not
// applicable to the WASM port.
//
// Every stub documents:
//   WHO calls it (upstream caller)
//   WHAT it does on real hardware
//   WHY it's a no-op here (or what it should eventually do)
//
// This is the most important file to review in the migration — it
// defines the boundary between "CircuitPython features we support"
// and "CircuitPython features we intentionally skip."
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer contracts)
//   design/behavior/07-deviations.md        (documented deviations)

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// ── Filesystem stubs ──
// WHO: supervisor/shared/filesystem.c, supervisor main loop
// WHAT: On real boards, manages the FatFS filesystem on SPI flash
// WHY STUB: We use VFS POSIX over WASI — no FatFS block device.
//   The filesystem "just works" through WASI's fd operations.
//   JS-side persistence (IndexedDB) is transparent to C.

#include "supervisor/filesystem.h"

void filesystem_background(void) {}
void filesystem_tick(void) {}

bool filesystem_init(bool create_allowed, bool force_create) {
    (void)create_allowed;
    (void)force_create;
    // VFS POSIX is mounted in main.c (CLI) or by the supervisor (browser).
    // No block device to initialize.
    return true;
}

void filesystem_flush(void) {}

void filesystem_set_internal_writable_by_usb(bool writable) {
    (void)writable;
    // No USB mass storage — filesystem is always writable by both
    // Python and the host (JS/CLI).
}

void filesystem_set_writable_by_usb(fs_user_mount_t *vfs, bool usb_writable) {
    (void)vfs;
    (void)usb_writable;
}

bool filesystem_is_writable_by_python(fs_user_mount_t *vfs) {
    (void)vfs;
    return true;
}

bool filesystem_is_writable_by_usb(fs_user_mount_t *vfs) {
    (void)vfs;
    return true;
}

void filesystem_set_internal_concurrent_write_protection(bool concurrent_write_protection) {
    (void)concurrent_write_protection;
    // No concurrent access concerns — single-threaded WASM.
}

void filesystem_set_concurrent_write_protection(fs_user_mount_t *vfs, bool concurrent_write_protection) {
    (void)vfs;
    (void)concurrent_write_protection;
}

bool filesystem_present(void) {
    // Returns false because we don't have a FatFS filesystem.
    // The VFS POSIX mount is handled separately.
    return false;
}

// ── Safe mode stubs ──
// WHO: supervisor main loop, reset handling
// WHAT: On real boards, checks for button-held-during-boot safe mode,
//   displays safe mode message, allows recovery
// WHY STUB: No hardware buttons for safe mode entry.  No hardware
//   faults that would trigger safe mode.  If we ever add simulated
//   safe mode (e.g., JS-triggered), this is where it would go.
// DEVIATION: Environmental — documented in design/behavior/07-deviations.md

#include "supervisor/shared/safe_mode.h"

safe_mode_t wait_for_safe_mode_reset(void) {
    return SAFE_MODE_NONE;
}

void reset_into_safe_mode(safe_mode_t reason) {
    (void)reason;
    // On real hardware, this resets the CPU into safe mode.
    // On WASM, we abort — this should not be reachable in normal
    // operation.  If it is, something is seriously wrong.
    abort();
}

void print_safe_mode_message(safe_mode_t reason) {
    (void)reason;
}

// ── Stack stubs ──
// WHO: supervisor main loop, pyexec
// WHAT: On real boards, checks stack pointer against a limit to
//   detect stack overflow before it corrupts memory
// WHY STUB: WASM runtime manages the stack and traps on overflow.
//   We trust the runtime — no manual checking needed.
// DEVIATION: Environmental — WASM provides this guarantee.

#include "supervisor/shared/stack.h"

bool stack_ok(void) {
    return true;
}

void assert_heap_ok(void) {}

void stack_init(void) {}

// ── Supervisor execution status ──
// WHO: supervisor/status_bar.c
// WHAT: Renders the current execution state into the status bar
// Reads from port_step_phase() (the lifecycle state machine).

#include "supervisor/shared/serial.h"
#include "port/port_step.h"

void supervisor_execution_status(void) {
    switch (port_step_phase()) {
        case PHASE_INIT:     serial_write("init");    break;
        case PHASE_IDLE:     serial_write("idle");    break;
        case PHASE_BOOT:     serial_write("boot.py"); break;
        case PHASE_CODE:     serial_write("code.py"); break;
        case PHASE_REPL:     serial_write("REPL");    break;
        case PHASE_SAFE_MODE:serial_write("safe");    break;
        case PHASE_SHUTDOWN: serial_write("shutdown"); break;
        default:             serial_write("???");     break;
    }
}

// ── LED stubs ──
// WHO: pyexec (serial activity indicators)
// WHAT: On real boards, toggles RX/TX LEDs on serial activity
// WHY STUB: No physical LEDs for serial activity.  The browser UI
//   handles visual feedback for serial I/O.

void toggle_rx_led(void) {}
void toggle_tx_led(void) {}

// ── Autoreload stub ──
// WHO: displayio_background, supervisor main loop
// WHAT: Checks if autoreload has been triggered and is ready to execute
// WHY STUB: Autoreload is managed by JS (file watcher) and delivered
//   via the event ring.  The C side doesn't need to poll for it.
// TODO(Phase 4.6): implement if the supervisor lifecycle needs it

bool autoreload_ready(void) { return false; }

// ── NLR jump fail ──
// WHO: py/nlrsetjmp.c (NLR exception mechanism)
// WHAT: Called when nlr_jump has no handler — fatal error
// WHY HERE: Normally in main.c (CLI entry), but main.c isn't compiled
//   for the browser board.  Every build needs this symbol.

#include <stdio.h>

void nlr_jump_fail(void *val) {
    fprintf(stderr, "FATAL: uncaught NLR %p\n", val);
    abort();
}

// ── OnDiskBitmap stub ──
// WHO: displayio TileGrid, displayio/__init__
// WHAT: Loads BMP files from FatFS via FIL objects
// WHY STUB: We use VFS POSIX, not FatFS.  OnDiskBitmap can't read
//   POSIX file objects with the current implementation.
// FUTURE: Adapt OnDiskBitmap for POSIX file objects, or provide a
//   frozen Python BMP loader.

#if CIRCUITPY_DISPLAYIO
#include "shared-module/displayio/OnDiskBitmap.h"

const mp_obj_type_t displayio_ondiskbitmap_type = {
    .base = { .type = &mp_type_type },
    .name = MP_QSTR_OnDiskBitmap,
};

uint32_t common_hal_displayio_ondiskbitmap_get_pixel(displayio_ondiskbitmap_t *self,
    int16_t x, int16_t y) {
    (void)self; (void)x; (void)y;
    return 0;
}
#endif

// ── VfsFat stub ──
// WHO: storage module (shared-bindings/storage)
// WHAT: The FatFS VFS type used for USB mass storage
// WHY STUB: We use VfsPosix, not FatFS.  Provide the type so
//   storage.VfsFat exists as a placeholder and type checks compile.
// DEVIATION: Intentional — FAT storage is inappropriate for our
//   platform.  See design/behavior/07-deviations.md.

#if CIRCUITPY_STORAGE
#include "py/obj.h"

const mp_obj_type_t mp_fat_vfs_type = {
    .base = { .type = &mp_type_type },
    .name = MP_QSTR_VfsFat,
};
#endif

// ── Supervisor lock (used by keypad scanning) ──
// Single-threaded WASM — no contention, locks are no-ops.
bool supervisor_acquire_lock(void) { return true; }
void supervisor_release_lock(void) { }
