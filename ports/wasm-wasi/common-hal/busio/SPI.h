/*
 * SPI.h — Virtual SPI backed by OPFS transfer files.
 *
 * SPI transfers read/write through /hw/spi/xfer — a file where
 * the last write becomes the next read (loopback by default).
 * A sensor simulator can intercept by writing response data.
 */
#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *clock;
    const mcu_pin_obj_t *mosi;
    const mcu_pin_obj_t *miso;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    bool has_lock;
    bool deinited;
} busio_spi_obj_t;
