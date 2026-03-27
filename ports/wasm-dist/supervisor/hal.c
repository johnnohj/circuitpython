/*
 * supervisor/hal.c — WASM hardware abstraction layer.
 *
 * Hardware state lives at /hal/ WASI fd endpoints, not in C arrays.
 * Common-hal modules read/write these fds directly.  wasi-memfs.js
 * intercepts the WASI calls and manages the state on the JS side,
 * routing to hardware simulators via onHardwareWrite callbacks.
 *
 * hal_init():
 *   Opens the /hal/ fd endpoints at startup.  Each peripheral type
 *   gets its own path: /hal/gpio, /hal/analog, /hal/pwm, etc.
 *
 * hal_step():
 *   Called at the top of cp_step().  Currently a no-op — JS writes
 *   to /hal/ endpoints are visible immediately via the WASI fd layer.
 *   Future: batch-read updates for efficiency.
 *
 * hal_export_dirty():
 *   Called at the bottom of cp_step().  Currently a no-op — writes
 *   go through fds in real-time.  Future: flush deferred writes.
 */

#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* HAL file descriptors                                                */
/* ------------------------------------------------------------------ */

static int _gpio_fd = -1;
static int _analog_fd = -1;
static int _pwm_fd = -1;
static int _neopixel_fd = -1;
static int _serial_rx_fd = -1;
static int _serial_tx_fd = -1;

static void _ensure_dir(const char *path) {
    mkdir(path, 0755);
}

/* ------------------------------------------------------------------ */
/* hal_init — open /hal/ endpoints at startup                          */
/* ------------------------------------------------------------------ */

void hal_init(void) {
    _ensure_dir("/hal");
    _ensure_dir("/hal/serial");
    _ensure_dir("/hal/i2c");
    _ensure_dir("/hal/i2c/dev");
    _ensure_dir("/hal/spi");
    _ensure_dir("/hal/uart");

    _gpio_fd = open("/hal/gpio", O_RDWR | O_CREAT, 0644);
    _analog_fd = open("/hal/analog", O_RDWR | O_CREAT, 0644);
    _pwm_fd = open("/hal/pwm", O_RDWR | O_CREAT, 0644);
    _neopixel_fd = open("/hal/neopixel", O_RDWR | O_CREAT, 0644);
    _serial_rx_fd = open("/hal/serial/rx", O_RDONLY | O_CREAT, 0644);
    _serial_tx_fd = open("/hal/serial/tx", O_WRONLY | O_CREAT, 0644);
}

/* ------------------------------------------------------------------ */
/* HAL fd accessors — common-hal modules call these                    */
/* ------------------------------------------------------------------ */

int hal_gpio_fd(void) { return _gpio_fd; }
int hal_analog_fd(void) { return _analog_fd; }
int hal_pwm_fd(void) { return _pwm_fd; }
int hal_neopixel_fd(void) { return _neopixel_fd; }
int hal_serial_rx_fd(void) { return _serial_rx_fd; }
int hal_serial_tx_fd(void) { return _serial_tx_fd; }

/* ------------------------------------------------------------------ */
/* hal_step — called at top of cp_step()                               */
/* ------------------------------------------------------------------ */

void hal_step(void) {
    /* JS writes to /hal/ endpoints are visible immediately via WASI.
     * No explicit poll needed — common-hal reads the fd on demand.
     * Future: batch-read for efficiency if per-read overhead matters. */
}

/* ------------------------------------------------------------------ */
/* hal_export_dirty — called at bottom of cp_step()                    */
/* ------------------------------------------------------------------ */

void hal_export_dirty(void) {
    /* Writes go through fds in real-time — no deferred flush needed.
     * Future: if we batch writes, flush them here. */
}
