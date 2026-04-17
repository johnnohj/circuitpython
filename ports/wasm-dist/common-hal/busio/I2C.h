/*
 * I2C.h — Virtual I2C backed by MEMFS register files.
 *
 * Each I2C device address maps to a file at /hal/i2c/dev/{addr_hex}.
 * The file contains the device's register space (up to 256 bytes).
 * Reads/writes go through POSIX lseek + read/write on these files.
 *
 * To simulate a sensor: write register values to the device file.
 * An Adafruit driver reading register 0xF7 reads byte 0xF7 from the file.
 */
#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *scl;
    const mcu_pin_obj_t *sda;
    uint32_t frequency;
    bool has_lock;
    bool deinited;
} busio_i2c_obj_t;
