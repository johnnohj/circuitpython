/*
 * supervisor/stubs.c — Functions referenced by shared code but not
 * applicable to the WASM port.
 *
 * Absorbs:
 *   supervisor/stub/filesystem.c — no FatFS, VFS POSIX instead
 *   supervisor/stub/safe_mode.c  — no hardware safe mode
 *   supervisor/stub/stack.c      — WASM runtime manages stack
 *
 * Also provides stubs for functions referenced by pyexec.c and
 * other shared code that assume a full CircuitPython supervisor.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Filesystem stubs (from supervisor/stub/filesystem.c)                */
/* WASM uses VFS POSIX over WASI — no FatFS block device.             */
/* ------------------------------------------------------------------ */

#include "supervisor/filesystem.h"

void filesystem_background(void) {}
void filesystem_tick(void) {}

bool filesystem_init(bool create_allowed, bool force_create) {
    (void)create_allowed;
    (void)force_create;
    return true;
}

void filesystem_flush(void) {}

void filesystem_set_internal_writable_by_usb(bool writable) {
    (void)writable;
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
}

void filesystem_set_concurrent_write_protection(fs_user_mount_t *vfs, bool concurrent_write_protection) {
    (void)vfs;
    (void)concurrent_write_protection;
}

bool filesystem_present(void) {
    return false;
}

/* ------------------------------------------------------------------ */
/* Safe mode stubs (from supervisor/stub/safe_mode.c)                  */
/* No hardware safe mode on WASM.                                      */
/* ------------------------------------------------------------------ */

#include "supervisor/shared/safe_mode.h"

safe_mode_t wait_for_safe_mode_reset(void) {
    return SAFE_MODE_NONE;
}

void reset_into_safe_mode(safe_mode_t reason) {
    (void)reason;
    abort();
}

void print_safe_mode_message(safe_mode_t reason) {
    (void)reason;
}

/* ------------------------------------------------------------------ */
/* Stack stubs (from supervisor/stub/stack.c)                          */
/* WASM runtime manages stack — we trust it.                           */
/* ------------------------------------------------------------------ */

#include "supervisor/shared/stack.h"

bool stack_ok(void) {
    return true;
}

void assert_heap_ok(void) {}

void stack_init(void) {}

/* ------------------------------------------------------------------ */
/* Serial stubs                                                        */
/* pyexec.c calls mp_hal_set_interrupt_char which is in unix_mphal.c.  */
/* These are for functions not yet wired up.                           */
/* ------------------------------------------------------------------ */

/* Status LED stubs — referenced by pyexec via supervisor/shared */
void toggle_rx_led(void) {}
void toggle_tx_led(void) {}
