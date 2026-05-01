/*
 * chassis/serial.c — Serial ring buffer read/write helpers.
 */

#include "serial.h"
#include "port_memory.h"
#include <string.h>

#define RING_DATA_SIZE (PORT_SERIAL_BUF_SIZE - 8)

/* ------------------------------------------------------------------ */
/* TX — C writes, JS reads                                             */
/* ------------------------------------------------------------------ */

size_t serial_tx_write(const uint8_t *data, size_t len) {
    serial_ring_t *tx = &port_mem.serial_tx;
    size_t written = 0;

    while (written < len) {
        uint32_t next_head = (tx->write_head + 1) % RING_DATA_SIZE;
        if (next_head == tx->read_head) {
            break;  /* ring full */
        }
        tx->data[tx->write_head] = data[written];
        tx->write_head = next_head;
        written++;
    }

    return written;
}

bool serial_tx_has_space(size_t len) {
    serial_ring_t *tx = &port_mem.serial_tx;
    uint32_t used;
    if (tx->write_head >= tx->read_head) {
        used = tx->write_head - tx->read_head;
    } else {
        used = RING_DATA_SIZE - tx->read_head + tx->write_head;
    }
    return (RING_DATA_SIZE - 1 - used) >= len;
}

void serial_tx_print(const char *str) {
    serial_tx_write((const uint8_t *)str, strlen(str));
}

/* ------------------------------------------------------------------ */
/* RX — JS writes, C reads                                             */
/* ------------------------------------------------------------------ */

size_t serial_rx_read(uint8_t *buf, size_t max_len) {
    serial_ring_t *rx = &port_mem.serial_rx;
    size_t read_count = 0;

    while (read_count < max_len && rx->read_head != rx->write_head) {
        buf[read_count] = rx->data[rx->read_head];
        rx->read_head = (rx->read_head + 1) % RING_DATA_SIZE;
        read_count++;
    }

    return read_count;
}

size_t serial_rx_available(void) {
    serial_ring_t *rx = &port_mem.serial_rx;
    if (rx->write_head >= rx->read_head) {
        return rx->write_head - rx->read_head;
    }
    return RING_DATA_SIZE - rx->read_head + rx->write_head;
}
