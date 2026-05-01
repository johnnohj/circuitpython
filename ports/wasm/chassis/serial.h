/*
 * chassis/serial.h — Serial ring buffer helpers.
 *
 * Read/write serial data through the MEMFS-backed ring buffers.
 * JS writes to /hal/serial/rx (keyboard input), C reads from it.
 * C writes to /hal/serial/tx (output), JS reads from it.
 */

#ifndef CHASSIS_SERIAL_H
#define CHASSIS_SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Write data into the TX ring (C→JS output) */
size_t serial_tx_write(const uint8_t *data, size_t len);

/* Read data from the RX ring (JS→C input) */
size_t serial_rx_read(uint8_t *buf, size_t max_len);

/* Check how many bytes are available to read from RX */
size_t serial_rx_available(void);

/* Check if TX ring has space for len bytes */
bool serial_tx_has_space(size_t len);

/* Write a C string to TX */
void serial_tx_print(const char *str);

#endif /* CHASSIS_SERIAL_H */
