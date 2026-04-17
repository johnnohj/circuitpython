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
/* pyexec.c calls mp_hal_set_interrupt_char which is in wasi_mphal.c.  */
/* These are for functions not yet wired up.                           */
/* ------------------------------------------------------------------ */

/* Status LED stubs — referenced by pyexec via supervisor/shared */
void toggle_rx_led(void) {}
void toggle_tx_led(void) {}

/* Autoreload stub — displayio_background checks this */
bool autoreload_ready(void) { return false; }

/* ------------------------------------------------------------------ */
/* OnDiskBitmap stubs                                                  */
/* OnDiskBitmap reads BMP files via FatFS (pyb_file_obj_t wraps FIL). */
/* We use VFS POSIX, not FatFS. TileGrid and displayio/__init__       */
/* reference the type for runtime type checks — stub so they link.    */
/* Future: adapt OnDiskBitmap for POSIX file objects or provide a      */
/* frozen Python BMP loader.                                           */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* VfsFat stub — storage module references mp_fat_vfs_type             */
/* We use VfsPosix, not FatFS. Provide the type so storage.VfsFat      */
/* exists (as a placeholder) and mount() type validation can be        */
/* relaxed.                                                            */
/* ------------------------------------------------------------------ */

#if CIRCUITPY_STORAGE
#include "py/obj.h"

const mp_obj_type_t mp_fat_vfs_type = {
    .base = { .type = &mp_type_type },
    .name = MP_QSTR_VfsFat,
};
#endif
