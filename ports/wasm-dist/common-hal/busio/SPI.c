/*
 * SPI.c — Virtual SPI backed by OPFS transfer files.
 *
 * SPI is simpler than I2C: full-duplex byte transfers. We use a
 * transfer file at /hal/spi/xfer. Write puts data there; read pulls
 * from it. A sensor simulator writes response bytes before the
 * driver reads.
 *
 * For CS-addressed devices, the reactor's Python shim handles chip
 * select via digitalio and routes to per-device files if needed.
 */

#include "common-hal/busio/SPI.h"
#include "shared-bindings/busio/SPI.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define SPI_XFER_PATH "/hal/spi/xfer"

static void _ensure_dirs(void) {
    mkdir("/hal/spi", 0777);
}

void common_hal_busio_spi_construct(busio_spi_obj_t *self,
    const mcu_pin_obj_t *clock, const mcu_pin_obj_t *mosi,
    const mcu_pin_obj_t *miso, bool half_duplex) {
    self->clock = clock;
    self->mosi = mosi;
    self->miso = miso;
    self->baudrate = 1000000;
    self->polarity = 0;
    self->phase = 0;
    self->bits = 8;
    self->has_lock = false;
    self->deinited = false;

    claim_pin(clock);
    if (mosi) claim_pin(mosi);
    if (miso) claim_pin(miso);
    _ensure_dirs();
}

void common_hal_busio_spi_deinit(busio_spi_obj_t *self) {
    if (self->deinited) return;
    self->deinited = true;
    if (self->clock) reset_pin_number(self->clock->number);
    if (self->mosi) reset_pin_number(self->mosi->number);
    if (self->miso) reset_pin_number(self->miso->number);
    self->clock = NULL;
    self->mosi = NULL;
    self->miso = NULL;
}

bool common_hal_busio_spi_deinited(busio_spi_obj_t *self) {
    return self->deinited;
}

void common_hal_busio_spi_mark_deinit(busio_spi_obj_t *self) {
    self->deinited = true;
}

bool common_hal_busio_spi_configure(busio_spi_obj_t *self,
    uint32_t baudrate, uint8_t polarity, uint8_t phase, uint8_t bits) {
    self->baudrate = baudrate;
    self->polarity = polarity;
    self->phase = phase;
    self->bits = bits;
    return true;
}

bool common_hal_busio_spi_try_lock(busio_spi_obj_t *self) {
    if (self->has_lock) return false;
    self->has_lock = true;
    return true;
}

bool common_hal_busio_spi_has_lock(busio_spi_obj_t *self) {
    return self->has_lock;
}

void common_hal_busio_spi_unlock(busio_spi_obj_t *self) {
    self->has_lock = false;
}

bool common_hal_busio_spi_write(busio_spi_obj_t *self,
    const uint8_t *data, size_t len) {
    int fd = open(SPI_XFER_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;
    write(fd, data, len);
    close(fd);
    return true;
}

bool common_hal_busio_spi_read(busio_spi_obj_t *self,
    uint8_t *data, size_t len, uint8_t write_value) {
    int fd = open(SPI_XFER_PATH, O_RDONLY);
    if (fd < 0) {
        /* No response file — fill with write_value (loopback). */
        memset(data, write_value, len);
        return true;
    }
    ssize_t n = read(fd, data, len);
    close(fd);
    if ((size_t)n < len) {
        memset(data + n, write_value, len - n);
    }
    return true;
}

bool common_hal_busio_spi_transfer(busio_spi_obj_t *self,
    const uint8_t *data_out, uint8_t *data_in, size_t len) {
    /* Write out data, then read response. */
    if (data_out) {
        int fd = open(SPI_XFER_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) {
            write(fd, data_out, len);
            close(fd);
        }
    }
    if (data_in) {
        int fd = open(SPI_XFER_PATH, O_RDONLY);
        if (fd >= 0) {
            ssize_t n = read(fd, data_in, len);
            close(fd);
            if ((size_t)n < len) {
                memset(data_in + n, 0x00, len - n);
            }
        } else {
            memset(data_in, 0x00, len);
        }
    }
    return true;
}

uint32_t common_hal_busio_spi_get_frequency(busio_spi_obj_t *self) {
    return self->baudrate;
}

uint8_t common_hal_busio_spi_get_phase(busio_spi_obj_t *self) {
    return self->phase;
}

uint8_t common_hal_busio_spi_get_polarity(busio_spi_obj_t *self) {
    return self->polarity;
}

void common_hal_busio_spi_never_reset(busio_spi_obj_t *self) {
    if (self->clock) never_reset_pin_number(self->clock->number);
    if (self->mosi) never_reset_pin_number(self->mosi->number);
    if (self->miso) never_reset_pin_number(self->miso->number);
}
