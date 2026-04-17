/*
 * I2C.c — Virtual I2C backed by MEMFS register files.
 *
 * Each I2C device is a file at /hal/i2c/dev/{addr_dec}.
 * The file is the device's register space. A write of [reg, data...]
 * seeks to `reg` and writes `data`. A read reads from the current
 * file position. write_read does both in sequence.
 *
 * To simulate a BMP280 at address 0x76:
 *   echo -n '\x58' | dd of=/hal/i2c/dev/118 bs=1 seek=208  # chip ID at 0xD0
 *   # Now i2c.readfrom_mem(0x76, 0xD0, 1) returns b'\x58'
 */

#include "common-hal/busio/I2C.h"
#include "shared-bindings/busio/I2C.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "py/mperrno.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define I2C_DEV_DIR "/hal/i2c/dev"

/* Build the device file path for a given address. */
static void _dev_path(char *buf, size_t buflen, uint16_t addr) {
    snprintf(buf, buflen, I2C_DEV_DIR "/%u", (unsigned)addr);
}

/* Ensure /hal/i2c/dev/ directory exists. */
static void _ensure_dirs(void) {
    mkdir("/hal/i2c", 0777);
    mkdir(I2C_DEV_DIR, 0777);
}

void common_hal_busio_i2c_construct(busio_i2c_obj_t *self,
    const mcu_pin_obj_t *scl, const mcu_pin_obj_t *sda,
    uint32_t frequency, uint32_t timeout_ms) {
    self->scl = scl;
    self->sda = sda;
    self->frequency = frequency;
    self->has_lock = false;
    self->deinited = false;

    claim_pin(scl);
    claim_pin(sda);
    _ensure_dirs();
}

void common_hal_busio_i2c_deinit(busio_i2c_obj_t *self) {
    if (self->deinited) {
        return;
    }
    self->deinited = true;
    if (self->scl) {
        reset_pin_number(self->scl->number);
    }
    if (self->sda) {
        reset_pin_number(self->sda->number);
    }
    self->scl = NULL;
    self->sda = NULL;
}

bool common_hal_busio_i2c_deinited(busio_i2c_obj_t *self) {
    return self->deinited;
}

void common_hal_busio_i2c_mark_deinit(busio_i2c_obj_t *self) {
    self->deinited = true;
}

bool common_hal_busio_i2c_try_lock(busio_i2c_obj_t *self) {
    if (self->has_lock) {
        return false;
    }
    self->has_lock = true;
    return true;
}

bool common_hal_busio_i2c_has_lock(busio_i2c_obj_t *self) {
    return self->has_lock;
}

void common_hal_busio_i2c_unlock(busio_i2c_obj_t *self) {
    self->has_lock = false;
}

bool common_hal_busio_i2c_probe(busio_i2c_obj_t *self, uint8_t addr) {
    /* Device exists if its register file exists in MEMFS. */
    char path[64];
    _dev_path(path, sizeof(path), addr);
    struct stat st;
    return stat(path, &st) == 0;
}

mp_negative_errno_t common_hal_busio_i2c_write(busio_i2c_obj_t *self,
    uint16_t address, const uint8_t *data, size_t len) {
    if (len == 0) {
        return 0;
    }
    char path[64];
    _dev_path(path, sizeof(path), address);

    /* First byte is the register address; remaining bytes are data.
     * If only 1 byte, it's just setting the register pointer (no data). */
    int fd = open(path, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
        return -MP_EIO;
    }

    uint8_t reg = data[0];
    lseek(fd, reg, SEEK_SET);

    if (len > 1) {
        ssize_t n = write(fd, data + 1, len - 1);
        if (n < 0) {
            close(fd);
            return -MP_EIO;
        }
    }
    close(fd);
    return 0;
}

mp_negative_errno_t common_hal_busio_i2c_read(busio_i2c_obj_t *self,
    uint16_t address, uint8_t *data, size_t len) {
    char path[64];
    _dev_path(path, sizeof(path), address);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -MP_ENODEV;
    }

    /* Read from current position (set by previous write's register byte). */
    ssize_t n = read(fd, data, len);
    close(fd);

    if (n < 0) {
        return -MP_EIO;
    }
    /* Pad with 0xFF if file is shorter than requested (uninitialized regs). */
    if ((size_t)n < len) {
        memset(data + n, 0xFF, len - n);
    }
    return 0;
}

mp_negative_errno_t common_hal_busio_i2c_write_read(busio_i2c_obj_t *self,
    uint16_t address,
    uint8_t *out_data, size_t out_len,
    uint8_t *in_data, size_t in_len) {
    /* Combined write-then-read in one "transaction".
     * The write sets the register pointer; the read returns data from there. */
    char path[64];
    _dev_path(path, sizeof(path), address);

    int fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        return -MP_EIO;
    }

    /* Write phase: first byte is register address. */
    if (out_len > 0) {
        uint8_t reg = out_data[0];
        lseek(fd, reg, SEEK_SET);
        if (out_len > 1) {
            write(fd, out_data + 1, out_len - 1);
            /* Re-seek to register address for read phase. */
            lseek(fd, reg, SEEK_SET);
        }
    }

    /* Read phase. */
    ssize_t n = read(fd, in_data, in_len);
    close(fd);

    if (n < 0) {
        return -MP_EIO;
    }
    if ((size_t)n < in_len) {
        memset(in_data + n, 0xFF, in_len - n);
    }
    return 0;
}

void common_hal_busio_i2c_never_reset(busio_i2c_obj_t *self) {
    if (self->scl) {
        never_reset_pin_number(self->scl->number);
    }
    if (self->sda) {
        never_reset_pin_number(self->sda->number);
    }
}
