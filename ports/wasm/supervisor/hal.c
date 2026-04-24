/*
 * supervisor/hal.c — WASM hardware abstraction layer.
 *
 * All hardware state lives in MEMFS at /hal/ WASI fd endpoints.
 * MEMFS is the substrate — off-stack persistent state that survives
 * C stack unwinding on VM yield/suspend.
 *
 * Pin metadata (role, flags, category, latched) is stored in the
 * GPIO MEMFS slot alongside electrical state (enabled, direction,
 * value, pull).  The hal_set/get API does read-modify-write on the
 * MEMFS fd — no C-side shadow state.
 *
 * JS→C input changes arrive via the semihosting event ring
 * (SH_EVT_HW_CHANGE).  Dirty flags in port_mem track which pins
 * changed; hal_step() checks them and wakes the supervisor.
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
/* GPIO slot helpers — read/write 12-byte slot from MEMFS              */
/* ------------------------------------------------------------------ */

static void _gpio_read_slot(uint8_t pin, uint8_t slot[HAL_GPIO_SLOT_SIZE]) {
    if (_gpio_fd < 0 || pin >= 64) {
        memset(slot, 0, HAL_GPIO_SLOT_SIZE);
        return;
    }
    lseek(_gpio_fd, pin * HAL_GPIO_SLOT_SIZE, SEEK_SET);
    ssize_t n = read(_gpio_fd, slot, HAL_GPIO_SLOT_SIZE);
    if (n < HAL_GPIO_SLOT_SIZE) {
        memset(slot + (n > 0 ? n : 0), 0, HAL_GPIO_SLOT_SIZE - (n > 0 ? n : 0));
    }
}

static void _gpio_write_slot(uint8_t pin, const uint8_t slot[HAL_GPIO_SLOT_SIZE]) {
    if (_gpio_fd < 0 || pin >= 64) return;
    lseek(_gpio_fd, pin * HAL_GPIO_SLOT_SIZE, SEEK_SET);
    write(_gpio_fd, slot, HAL_GPIO_SLOT_SIZE);
}

/* ------------------------------------------------------------------ */
/* Pin metadata API — read-modify-write through MEMFS                  */
/*                                                                     */
/* These are called infrequently (pin construct/deinit/reset), so the  */
/* cost of 2 WASI fd calls per operation is acceptable.                */
/* ------------------------------------------------------------------ */

void hal_set_role(uint8_t pin, uint8_t role) {
    if (pin >= 64) return;
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _gpio_read_slot(pin, slot);
    slot[HAL_GPIO_OFF_ROLE] = role;
    _gpio_write_slot(pin, slot);
}

void hal_clear_role(uint8_t pin) {
    if (pin >= 64) return;
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _gpio_read_slot(pin, slot);
    slot[HAL_GPIO_OFF_ROLE] = HAL_ROLE_UNCLAIMED;
    slot[HAL_GPIO_OFF_FLAGS] = 0;
    _gpio_write_slot(pin, slot);
}

void hal_set_flag(uint8_t pin, uint8_t flag) {
    if (pin >= 64) return;
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _gpio_read_slot(pin, slot);
    slot[HAL_GPIO_OFF_FLAGS] |= flag;
    _gpio_write_slot(pin, slot);
}

void hal_clear_flag(uint8_t pin, uint8_t flag) {
    if (pin >= 64) return;
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _gpio_read_slot(pin, slot);
    slot[HAL_GPIO_OFF_FLAGS] &= ~flag;
    _gpio_write_slot(pin, slot);
}

uint8_t hal_get_flags(uint8_t pin) {
    if (pin >= 64) return 0;
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _gpio_read_slot(pin, slot);
    return slot[HAL_GPIO_OFF_FLAGS];
}

void hal_set_category(uint8_t pin, uint8_t category) {
    if (pin >= 64) return;
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _gpio_read_slot(pin, slot);
    slot[HAL_GPIO_OFF_CATEGORY] = category;
    _gpio_write_slot(pin, slot);
}

uint8_t hal_get_category(uint8_t pin) {
    if (pin >= 64) return HAL_CAT_NONE;
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _gpio_read_slot(pin, slot);
    return slot[HAL_GPIO_OFF_CATEGORY];
}

/* ------------------------------------------------------------------ */
/* hal_step — called at top of cp_hw_step()                            */
/*                                                                     */
/* Drains the event ring and checks dirty flags.  Input latching is    */
/* handled per-event in sh_on_event(SH_EVT_HW_CHANGE), not here —     */
/* each event is processed individually so button press + release      */
/* are two separate events, not one collapsed dirty flag.              */
/* ------------------------------------------------------------------ */

/* Forward declaration — port.c */
extern void port_wake_main_task(void);

void hal_step(void) {
    /* Drain events from the shared linear-memory event ring. */
    sh_drain_event_ring();

    /* Check for HAL changes (dirty flags set by SH_EVT_HW_CHANGE
     * handler or by common-hal writes).  Wake the supervisor so
     * background callbacks and interrupt-driven modules can process. */
    if (port_mem.hal_gpio_dirty || port_mem.hal_analog_dirty ||
        port_mem.hal_pwm_dirty || port_mem.hal_neopixel_dirty) {
        port_wake_main_task();
    }
}

/* ------------------------------------------------------------------ */
/* hal_export_dirty — called at bottom of cp_hw_step()                 */
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
