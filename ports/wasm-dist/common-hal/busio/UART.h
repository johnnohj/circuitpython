/*
 * UART.h — Virtual UART backed by MEMFS ring files.
 *
 * TX/RX data flows through /hal/uart/{port}/tx and /hal/uart/{port}/rx.
 * User code writes to TX; the worker or JS writes incoming data to RX.
 * This enables serial device simulation (GPS, BLE modules, etc.).
 */
#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *tx;
    const mcu_pin_obj_t *rx;
    uint32_t baudrate;
    uint32_t timeout_ms;
    uint8_t port_id;
    bool has_lock;
    bool deinited;
} busio_uart_obj_t;
