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
#include <string.h>
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

    /* Zero pin metadata (categories populated by hal_init_pin_categories) */
    memset(port_mem.pin_meta, 0, sizeof(port_mem.pin_meta));
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
/* Pin category init — weak default, overridden by board_pins.c        */
/* ------------------------------------------------------------------ */

__attribute__((weak))
void hal_init_pin_categories(void) {
    /* No board table in standard variant — categories stay zeroed. */
}

/* ------------------------------------------------------------------ */
/* Pin metadata API                                                    */
/* ------------------------------------------------------------------ */

void hal_set_role(uint8_t pin, uint8_t role) {
    if (pin < 64) port_mem.pin_meta[pin].role = role;
}

void hal_clear_role(uint8_t pin) {
    if (pin < 64) {
        port_mem.pin_meta[pin].role = HAL_ROLE_UNCLAIMED;
        port_mem.pin_meta[pin].flags = 0;
    }
}

void hal_set_flag(uint8_t pin, uint8_t flag) {
    if (pin < 64) port_mem.pin_meta[pin].flags |= flag;
}

void hal_clear_flag(uint8_t pin, uint8_t flag) {
    if (pin < 64) port_mem.pin_meta[pin].flags &= ~flag;
}

uint8_t hal_get_flags(uint8_t pin) {
    if (pin >= 64) return 0;
    return port_mem.pin_meta[pin].flags;
}

void hal_set_category(uint8_t pin, uint8_t category) {
    if (pin < 64) port_mem.pin_meta[pin].category = category;
}

/* WASM exports for JS direct memory access */
__attribute__((export_name("hal_pin_meta_addr")))
uint32_t _hal_pin_meta_addr(void) {
    return (uint32_t)(uintptr_t)&port_mem.pin_meta[0];
}

__attribute__((export_name("hal_pin_meta_stride")))
uint32_t _hal_pin_meta_stride(void) {
    return (uint32_t)sizeof(port_mem.pin_meta[0]);
}

/* JS calls this to set flags (e.g., JS_WROTE) from outside WASM */
__attribute__((export_name("hal_set_pin_flag")))
void _hal_set_pin_flag(int pin, int flag) {
    hal_set_flag((uint8_t)pin, (uint8_t)flag);
}

__attribute__((export_name("hal_clear_pin_flag")))
void _hal_clear_pin_flag(int pin, int flag) {
    hal_clear_flag((uint8_t)pin, (uint8_t)flag);
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

    /* Latch input values from JS — port-level interrupt capture.
     *
     * On real hardware, a GPIO interrupt latches the pin state in a
     * register.  The ISR fires during sleep, and the application reads
     * the latched value when it wakes.  We do the same: when JS writes
     * a new value (JS_WROTE), read the MEMFS value and store it in
     * pin_meta.latched.  get_value() returns the latched value if set,
     * ensuring the VM sees the press even if JS releases (mouseup)
     * before the VM wakes from time.sleep().
     */
    uint64_t gpio_dirty = port_mem.hal_gpio_dirty;
    if (gpio_dirty) {
        int gpio_fd = hal_gpio_fd();
        if (gpio_fd >= 0) {
            while (gpio_dirty) {
                int pin = __builtin_ctzll(gpio_dirty);
                gpio_dirty &= gpio_dirty - 1;  /* clear lowest bit */
                uint8_t flags = port_mem.pin_meta[pin].flags;
                if ((flags & HAL_FLAG_JS_WROTE) &&
                    !(flags & HAL_FLAG_LATCHED) &&
                    port_mem.pin_meta[pin].role == HAL_ROLE_DIGITAL_IN) {
                    /* Read the current value from MEMFS and latch it.
                     * Skip if already latched — first edge wins, like
                     * an edge-triggered IRQ pending until consumed. */
                    uint8_t slot[8];
                    lseek(gpio_fd, pin * 8, SEEK_SET);
                    read(gpio_fd, slot, 8);
                    port_mem.pin_meta[pin].latched = slot[2];
                    port_mem.pin_meta[pin].flags |= HAL_FLAG_LATCHED;
                }
            }
        }
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
