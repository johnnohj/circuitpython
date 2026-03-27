/*
 * UART.c — Virtual UART backed by OPFS files.
 *
 * TX goes to /hw/uart/{port}/tx — the worker or JS reads it.
 * RX comes from /hw/uart/{port}/rx — simulated device or JS writes it.
 *
 * Supports up to 8 UART ports. Port ID is auto-assigned on construct.
 */

#include "common-hal/busio/UART.h"
#include "shared-bindings/busio/UART.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/stream.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define UART_MAX_PORTS 8

static uint8_t _next_port_id = 0;

static void _uart_path(char *buf, size_t buflen, uint8_t port, const char *ep) {
    snprintf(buf, buflen, "/hw/uart/%u/%s", (unsigned)port, ep);
}

static void _ensure_dirs(uint8_t port) {
    mkdir("/hw/uart", 0777);
    char dir[32];
    snprintf(dir, sizeof(dir), "/hw/uart/%u", (unsigned)port);
    mkdir(dir, 0777);
}

void common_hal_busio_uart_construct(busio_uart_obj_t *self,
    const mcu_pin_obj_t *tx, const mcu_pin_obj_t *rx,
    const mcu_pin_obj_t *rts, const mcu_pin_obj_t *cts,
    const mcu_pin_obj_t *rs485_dir, bool rs485_invert,
    uint32_t baudrate, uint8_t bits, busio_uart_parity_t parity, uint8_t stop,
    mp_float_t timeout, uint16_t receiver_buffer_size, byte *receiver_buffer,
    bool sigint_enabled) {

    if (_next_port_id >= UART_MAX_PORTS) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("All UART ports in use"));
    }

    self->tx = tx;
    self->rx = rx;
    self->baudrate = baudrate;
    self->timeout_ms = (uint32_t)(timeout * 1000);
    self->port_id = _next_port_id++;
    self->has_lock = false;
    self->deinited = false;

    if (tx) claim_pin(tx);
    if (rx) claim_pin(rx);
    /* rts, cts, rs485_dir ignored — no flow control in simulation */

    _ensure_dirs(self->port_id);

    /* Create empty rx/tx files */
    char path[64];
    _uart_path(path, sizeof(path), self->port_id, "rx");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) close(fd);
    _uart_path(path, sizeof(path), self->port_id, "tx");
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) close(fd);
}

void common_hal_busio_uart_deinit(busio_uart_obj_t *self) {
    if (self->deinited) return;
    self->deinited = true;
    if (self->tx) reset_pin_number(self->tx->number);
    if (self->rx) reset_pin_number(self->rx->number);
    self->tx = NULL;
    self->rx = NULL;
}

bool common_hal_busio_uart_deinited(busio_uart_obj_t *self) {
    return self->deinited;
}

size_t common_hal_busio_uart_read(busio_uart_obj_t *self,
    uint8_t *data, size_t len, int *errcode) {
    char path[64];
    _uart_path(path, sizeof(path), self->port_id, "rx");

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        *errcode = MP_EAGAIN;
        return 0;
    }
    ssize_t n = read(fd, data, len);
    close(fd);

    if (n <= 0) {
        *errcode = MP_EAGAIN;
        return 0;
    }

    /* Consume: truncate the file by rewriting without the bytes we read.
     * Simple approach: read remainder, rewrite file. */
    /* For now, just truncate — assumes single consumer. */
    fd = open(path, O_WRONLY | O_TRUNC, 0666);
    if (fd >= 0) close(fd);

    return (size_t)n;
}

size_t common_hal_busio_uart_write(busio_uart_obj_t *self,
    const uint8_t *data, size_t len, int *errcode) {
    char path[64];
    _uart_path(path, sizeof(path), self->port_id, "tx");

    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd < 0) {
        *errcode = MP_EIO;
        return 0;
    }
    ssize_t n = write(fd, data, len);
    close(fd);

    if (n < 0) {
        *errcode = MP_EIO;
        return 0;
    }
    return (size_t)n;
}

uint32_t common_hal_busio_uart_get_baudrate(busio_uart_obj_t *self) {
    return self->baudrate;
}

void common_hal_busio_uart_set_baudrate(busio_uart_obj_t *self, uint32_t baudrate) {
    self->baudrate = baudrate;
}

mp_float_t common_hal_busio_uart_get_timeout(busio_uart_obj_t *self) {
    return (mp_float_t)self->timeout_ms / 1000.0f;
}

void common_hal_busio_uart_set_timeout(busio_uart_obj_t *self, mp_float_t timeout) {
    self->timeout_ms = (uint32_t)(timeout * 1000);
}

uint32_t common_hal_busio_uart_rx_characters_available(busio_uart_obj_t *self) {
    char path[64];
    _uart_path(path, sizeof(path), self->port_id, "rx");
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint32_t)st.st_size;
}

void common_hal_busio_uart_clear_rx_buffer(busio_uart_obj_t *self) {
    char path[64];
    _uart_path(path, sizeof(path), self->port_id, "rx");
    int fd = open(path, O_WRONLY | O_TRUNC, 0666);
    if (fd >= 0) close(fd);
}

bool common_hal_busio_uart_ready_to_tx(busio_uart_obj_t *self) {
    return true;  /* always ready — file I/O doesn't block */
}

void common_hal_busio_uart_never_reset(busio_uart_obj_t *self) {
    if (self->tx) never_reset_pin_number(self->tx->number);
    if (self->rx) never_reset_pin_number(self->rx->number);
}
