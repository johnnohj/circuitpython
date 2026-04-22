/*
 * supervisor/hal.c — WASM hardware abstraction layer.
 *
 * Hardware state lives at /hal/ WASI fd endpoints, not in C arrays.
 * Common-hal modules read/write these fds directly.  wasi-memfs.js
 * intercepts the WASI calls and manages the state on the JS side.
 *
 * Self-aware behavior:
 *   JS sets dirty bitmasks in port_mem when it writes to /hal/
 *   endpoints.  hal_step() reads and clears these, detecting which
 *   pins changed.  When changes are detected, the supervisor is
 *   alerted via port_wake_main_task() so background callbacks run
 *   and interrupt-driven modules (keypad, countio, etc.) can process.
 */

#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "supervisor/hal.h"
#include "supervisor/semihosting.h"
#include "supervisor/port_memory.h"

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

    /* Clear dirty flags */
    port_mem.hal_gpio_dirty = 0;
    port_mem.hal_analog_dirty = 0;
    port_mem.hal_pwm_dirty = 0;
    port_mem.hal_neopixel_dirty = 0;
    port_mem.hal_change_count = 0;
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
/* Dirty flag implementation                                           */
/* ------------------------------------------------------------------ */

void hal_mark_gpio_dirty(uint8_t pin) {
    if (pin < 64) {
        port_mem.hal_gpio_dirty |= (1ULL << pin);
        port_mem.hal_change_count++;
    }
}

void hal_mark_analog_dirty(uint8_t pin) {
    if (pin < 64) {
        port_mem.hal_analog_dirty |= (1ULL << pin);
        port_mem.hal_change_count++;
    }
}

void hal_mark_pwm_dirty(uint8_t pin) {
    if (pin < 64) {
        port_mem.hal_pwm_dirty |= (1ULL << pin);
        port_mem.hal_change_count++;
    }
}

void hal_mark_neopixel_dirty(void) {
    port_mem.hal_neopixel_dirty = 1;
    port_mem.hal_change_count++;
}

bool hal_gpio_changed(uint8_t pin) {
    if (pin >= 64) return false;
    return (port_mem.hal_gpio_dirty & (1ULL << pin)) != 0;
}

bool hal_analog_changed(uint8_t pin) {
    if (pin >= 64) return false;
    return (port_mem.hal_analog_dirty & (1ULL << pin)) != 0;
}

uint64_t hal_gpio_drain_dirty(void) {
    uint64_t d = port_mem.hal_gpio_dirty;
    port_mem.hal_gpio_dirty = 0;
    return d;
}

uint64_t hal_analog_drain_dirty(void) {
    uint64_t d = port_mem.hal_analog_dirty;
    port_mem.hal_analog_dirty = 0;
    return d;
}

/* ------------------------------------------------------------------ */
/* WASM exports — JS calls these to set dirty flags                    */
/* ------------------------------------------------------------------ */

__attribute__((export_name("hal_mark_gpio_dirty")))
void _hal_mark_gpio_dirty(int pin) {
    hal_mark_gpio_dirty((uint8_t)pin);
}

__attribute__((export_name("hal_mark_analog_dirty")))
void _hal_mark_analog_dirty(int pin) {
    hal_mark_analog_dirty((uint8_t)pin);
}

__attribute__((export_name("hal_mark_pwm_dirty")))
void _hal_mark_pwm_dirty(int pin) {
    hal_mark_pwm_dirty((uint8_t)pin);
}

__attribute__((export_name("hal_mark_neopixel_dirty")))
void _hal_mark_neopixel_dirty(void) {
    hal_mark_neopixel_dirty();
}

__attribute__((export_name("hal_get_change_count")))
uint32_t _hal_get_change_count(void) {
    return port_mem.hal_change_count;
}

/* ------------------------------------------------------------------ */
/* hal_step — called at top of cp_step()                               */
/* ------------------------------------------------------------------ */

/* Forward declaration — port.c */
extern void port_wake_main_task(void);

void hal_step(void) {
    /* Drain events from the shared linear-memory event ring. */
    sh_drain_event_ring();

    /* Check for HAL changes from JS.
     * JS calls hal_mark_*_dirty() when it writes to /hal/ endpoints.
     * If anything changed, wake the supervisor so background callbacks
     * and interrupt-driven modules can process. */
    if (port_mem.hal_gpio_dirty || port_mem.hal_analog_dirty ||
        port_mem.hal_pwm_dirty || port_mem.hal_neopixel_dirty) {
        port_wake_main_task();
    }
}

/* ------------------------------------------------------------------ */
/* hal_export_dirty — called at bottom of cp_step()                    */
/* ------------------------------------------------------------------ */

void hal_export_dirty(void) {
    /* Clear dirty flags after the frame has processed them.
     * Any changes that arrived during the frame will be caught
     * on the next hal_step(). */
    port_mem.hal_gpio_dirty = 0;
    port_mem.hal_analog_dirty = 0;
    port_mem.hal_pwm_dirty = 0;
    port_mem.hal_neopixel_dirty = 0;
}
