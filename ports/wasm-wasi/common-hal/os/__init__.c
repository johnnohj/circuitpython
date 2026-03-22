/*
 * common-hal/os — WASI port-specific functions
 *
 * Only the functions NOT provided by shared-module/os/__init__.c.
 * The shared-module handles chdir, getcwd, listdir, mkdir, remove,
 * rename, rmdir, stat via mp_vfs_* calls.
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "py/runtime.h"
#include "shared-bindings/os/__init__.h"

bool common_hal_os_urandom(uint8_t *buffer, mp_uint_t length) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        memset(buffer, 0, length);
        return false;
    }
    ssize_t n = read(fd, buffer, length);
    close(fd);
    return n == (ssize_t)length;
}

// ---- Stubs for functions that need FatFS or settings.toml ----

// disk_ioctl is called by os.sync() for FAT block devices.
// We use VFS POSIX (no block device), so this is a no-op.
#include "lib/oofatfs/ff.h"
#include "lib/oofatfs/diskio.h"
DRESULT disk_ioctl(void *pdrv, BYTE cmd, void *buff) {
    (void)pdrv;
    (void)cmd;
    (void)buff;
    return RES_OK;
}

// settings_get_raw_vstr reads settings.toml.
// TODO: implement proper settings.toml parsing from OPFS.
#include "supervisor/shared/settings.h"
settings_err_t settings_get_raw_vstr(const char *key, vstr_t *vstr) {
    (void)key;
    (void)vstr;
    return SETTINGS_ERR_NOT_FOUND;
}
