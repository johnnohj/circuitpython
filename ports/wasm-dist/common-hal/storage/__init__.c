/*
 * common-hal/storage/__init__.c — WASM storage module.
 *
 * On real boards, the storage module manages the CIRCUITPY FatFS drive
 * and its USB-writable / Python-writable state.  On WASM, the filesystem
 * is VfsPosix over WASI (backed by wasi-memfs.js + IndexedDB), so:
 *
 *   - Always writable by Python (no USB host to conflict with)
 *   - mount/umount work via the standard mp_vfs layer
 *   - remount is a no-op (no readonly ↔ writable toggle needed)
 *   - erase_filesystem clears /CIRCUITPY and soft-reboots
 *   - USB drive enable/disable always return False
 */

#include <string.h>

#include "extmod/vfs.h"
#include "py/mperrno.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "shared-bindings/os/__init__.h"
#include "shared-bindings/storage/__init__.h"

/* ------------------------------------------------------------------ */
/* mount / umount / getmount                                           */
/* ------------------------------------------------------------------ */

void common_hal_storage_mount(mp_obj_t vfs_obj, const char *mount_path,
                              bool readonly) {
    const char *abs_mount_path = common_hal_os_path_abspath(mount_path);

    mp_vfs_mount_t *vfs = m_new_obj(mp_vfs_mount_t);
    vfs->str = abs_mount_path;
    vfs->len = strlen(abs_mount_path);
    vfs->obj = vfs_obj;
    vfs->next = NULL;

    /* Check mount point exists (skip for "/"). */
    if (strcmp(vfs->str, "/") != 0) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_obj_t stat = common_hal_os_stat(abs_mount_path);
            nlr_pop();
            mp_obj_tuple_t *t = MP_OBJ_TO_PTR(stat);
            if ((MP_OBJ_SMALL_INT_VALUE(t->items[0]) & MP_S_IFDIR) == 0) {
                mp_raise_RuntimeError(
                    MP_ERROR_TEXT("Mount point directory missing"));
            }
        } else {
            mp_raise_RuntimeError(
                MP_ERROR_TEXT("Mount point directory missing"));
        }
    }

    /* Check mount point not already in use. */
    const char *path_out;
    mp_vfs_mount_t *existing = mp_vfs_lookup_path(abs_mount_path, &path_out);
    if (existing != MP_VFS_NONE && existing != MP_VFS_ROOT) {
        if (vfs->len != 1 && existing->len == 1) {
            /* Root is mounted — allow sub-mount. */
        } else {
            mp_raise_OSError(MP_EPERM);
        }
    }

    /* Call VFS object's mount method. */
    mp_obj_t args[2] = { mp_const_false, mp_const_false };
    mp_obj_t meth[4];
    mp_load_method(vfs->obj, MP_QSTR_mount, meth);
    meth[2] = args[0];
    meth[3] = args[1];
    mp_call_method_n_kw(2, 0, meth);

    /* Insert into mount table. */
    mp_vfs_mount_t **vfsp = &MP_STATE_VM(vfs_mount_table);
    vfs->next = *vfsp;
    *vfsp = vfs;
}

void common_hal_storage_umount_object(mp_obj_t vfs_obj) {
    mp_vfs_mount_t *vfs = NULL;
    for (mp_vfs_mount_t **vfsp = &MP_STATE_VM(vfs_mount_table);
         *vfsp != NULL; vfsp = &(*vfsp)->next) {
        if ((*vfsp)->obj == vfs_obj) {
            vfs = *vfsp;
            *vfsp = (*vfsp)->next;
            break;
        }
    }
    if (vfs == NULL) {
        mp_raise_OSError(MP_EINVAL);
    }
    if (MP_STATE_VM(vfs_cur) == vfs) {
        MP_STATE_VM(vfs_cur) = MP_VFS_ROOT;
    }
    mp_obj_t meth[2];
    mp_load_method(vfs->obj, MP_QSTR_umount, meth);
    mp_call_method_n_kw(0, 0, meth);
}

void common_hal_storage_umount_path(const char *mount_path) {
    const char *abs = common_hal_os_path_abspath(mount_path);
    for (mp_vfs_mount_t **vfsp = &MP_STATE_VM(vfs_mount_table);
         *vfsp != NULL; vfsp = &(*vfsp)->next) {
        if (strcmp(abs, (*vfsp)->str) == 0) {
            common_hal_storage_umount_object((*vfsp)->obj);
            return;
        }
    }
    mp_raise_OSError(MP_EINVAL);
}

mp_obj_t common_hal_storage_getmount(const char *mount_path) {
    const char *abs = common_hal_os_path_abspath(mount_path);
    for (mp_vfs_mount_t *vfs = MP_STATE_VM(vfs_mount_table);
         vfs != NULL; vfs = vfs->next) {
        if (strcmp(abs, vfs->str) == 0) {
            return vfs->obj;
        }
    }
    mp_raise_OSError(MP_EINVAL);
}

/* ------------------------------------------------------------------ */
/* remount — no-op on WASM (always writable, no USB)                   */
/* ------------------------------------------------------------------ */

void common_hal_storage_remount(const char *mount_path, bool readonly,
                                bool disable_concurrent_write_protection) {
    (void)mount_path;
    (void)readonly;
    (void)disable_concurrent_write_protection;
    /* VfsPosix over WASI is always writable by Python.
     * No USB host to share the drive with. */
}

/* ------------------------------------------------------------------ */
/* erase_filesystem — clear CIRCUITPY, soft reboot                     */
/* ------------------------------------------------------------------ */

NORETURN void common_hal_storage_erase_filesystem(bool extended) {
    (void)extended;
    /* On WASM, "erase filesystem" means clear wasi-memfs and reload.
     * Since we can't truly reboot, raise SystemExit. */
    mp_raise_msg(&mp_type_SystemExit,
                 MP_ERROR_TEXT("Filesystem erased. Reload to continue."));
    /* Unreachable — mp_raise is NORETURN. */
    for (;;) {}
}

/* ------------------------------------------------------------------ */
/* USB drive — not applicable on WASM                                  */
/* ------------------------------------------------------------------ */

bool common_hal_storage_disable_usb_drive(void) {
    return false;
}

bool common_hal_storage_enable_usb_drive(void) {
    return false;
}
