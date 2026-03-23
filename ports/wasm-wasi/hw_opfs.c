/*
 * hw_opfs.c — OPFS read/write-through for hardware state arrays.
 *
 * Binary file formats (all little-endian, wasm32 native layout):
 *
 *   /hw/gpio/state    — 64 × gpio_pin_state_t (6 bytes each = 384 bytes)
 *     Per pin: [value:u8][direction:u8][pull:u8][open_drain:u8][enabled:u8][never_reset:u8]
 *
 *   /hw/analog/state  — 64 × analog_pin_state_t (4 bytes each = 256 bytes)
 *     Per pin: [value:u16le][is_output:u8][enabled:u8]
 *
 *   /hw/pwm/state     — 64 × pwm_opfs_entry_t (8 bytes each = 512 bytes)
 *     Per pin: [duty_cycle:u16le][frequency_lo:u16le][frequency_hi:u16le][flags:u8][_pad:u8]
 *     flags: bit0=variable_freq, bit1=enabled, bit2=never_reset
 *
 *   /hw/neopixel/data — header + pixel data for active pins
 *     [count:u8] followed by count × [pin:u8][num_bytes:u16le][pixels...]
 *
 * Python struct.pack equivalents documented in each section.
 */

#include "hw_opfs.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include "common-hal/digitalio/DigitalInOut.h"
#include "common-hal/analogio/AnalogIn.h"
#include "common-hal/pwmio/PWMOut.h"

/* ---- Dirty flags ---- */
bool hw_opfs_gpio_dirty    = false;
bool hw_opfs_analog_dirty  = false;
bool hw_opfs_pwm_dirty     = false;
bool hw_opfs_neopixel_dirty = false;

/* ---- Mtime tracking (skip reads when file hasn't changed) ---- */
static time_t _gpio_mtime    = 0;
static time_t _analog_mtime  = 0;
static time_t _pwm_mtime     = 0;

/* ---- Helpers ---- */

static void _ensure_dir(const char *path) {
    mkdir(path, 0777);  /* ignore EEXIST */
}

static int _write_file(const char *path, const void *data, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    write(fd, data, len);
    close(fd);
    return 0;
}

static int _read_file(const char *path, void *buf, size_t len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len);
    close(fd);
    return (int)n;
}

static time_t _file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

/* ------------------------------------------------------------------ */
/* GPIO                                                                */
/* ------------------------------------------------------------------ */
/*
 * gpio_pin_state_t is 6 bytes with no padding (all uint8/bool).
 * Python: struct.pack('6B', value, direction, pull, open_drain, enabled, never_reset)
 */

void hw_opfs_gpio_read(void) {
    time_t mt = _file_mtime(HW_GPIO_STATE_PATH);
    if (mt == _gpio_mtime) return;
    _gpio_mtime = mt;

    /* Read into a temp buffer, then merge.
     * Preserve never_reset pins — don't let external writes override them. */
    gpio_pin_state_t tmp[64];
    int n = _read_file(HW_GPIO_STATE_PATH, tmp, sizeof(tmp));
    if (n < (int)sizeof(gpio_pin_state_t)) return;

    int count = n / (int)sizeof(gpio_pin_state_t);
    if (count > 64) count = 64;

    for (int i = 0; i < count; i++) {
        if (gpio_state[i].never_reset) continue;
        gpio_state[i] = tmp[i];
    }
}

void hw_opfs_gpio_write(void) {
    _write_file(HW_GPIO_STATE_PATH, gpio_state, sizeof(gpio_state));
    _gpio_mtime = _file_mtime(HW_GPIO_STATE_PATH);
    hw_opfs_gpio_dirty = false;
}

/* ------------------------------------------------------------------ */
/* Analog                                                              */
/* ------------------------------------------------------------------ */
/*
 * analog_pin_state_t: [value:u16le][is_output:u8][enabled:u8] = 4 bytes
 * Python: struct.pack('<HBB', value, is_output, enabled)
 */

void hw_opfs_analog_read(void) {
    time_t mt = _file_mtime(HW_ANALOG_STATE_PATH);
    if (mt == _analog_mtime) return;
    _analog_mtime = mt;

    analog_pin_state_t tmp[64];
    int n = _read_file(HW_ANALOG_STATE_PATH, tmp, sizeof(tmp));
    if (n < (int)sizeof(analog_pin_state_t)) return;

    int count = n / (int)sizeof(analog_pin_state_t);
    if (count > 64) count = 64;

    for (int i = 0; i < count; i++) {
        analog_state[i] = tmp[i];
    }
}

void hw_opfs_analog_write(void) {
    _write_file(HW_ANALOG_STATE_PATH, analog_state, sizeof(analog_state));
    _analog_mtime = _file_mtime(HW_ANALOG_STATE_PATH);
    hw_opfs_analog_dirty = false;
}

/* ------------------------------------------------------------------ */
/* PWM                                                                 */
/* ------------------------------------------------------------------ */
/*
 * pwm_state_t has uint32_t frequency which introduces compiler padding.
 * Use a packed wire format instead of raw struct dump.
 *
 * Wire format per pin (8 bytes):
 *   [duty_cycle:u16le][frequency:u32le][flags:u8][_pad:u8]
 *   flags: bit0=variable_freq, bit1=enabled, bit2=never_reset
 *
 * Python: struct.pack('<HIBx', duty_cycle, frequency, flags)
 */

typedef struct __attribute__((packed)) {
    uint16_t duty_cycle;
    uint32_t frequency;
    uint8_t flags;
    uint8_t _pad;
} pwm_opfs_entry_t;

_Static_assert(sizeof(pwm_opfs_entry_t) == 8, "pwm wire format must be 8 bytes");

void hw_opfs_pwm_read(void) {
    time_t mt = _file_mtime(HW_PWM_STATE_PATH);
    if (mt == _pwm_mtime) return;
    _pwm_mtime = mt;

    pwm_opfs_entry_t tmp[64];
    int n = _read_file(HW_PWM_STATE_PATH, tmp, sizeof(tmp));
    if (n < (int)sizeof(pwm_opfs_entry_t)) return;

    int count = n / (int)sizeof(pwm_opfs_entry_t);
    if (count > 64) count = 64;

    for (int i = 0; i < count; i++) {
        if (pwm_state[i].never_reset) continue;
        pwm_state[i].duty_cycle    = tmp[i].duty_cycle;
        pwm_state[i].frequency     = tmp[i].frequency;
        pwm_state[i].variable_freq = (tmp[i].flags & 0x01) != 0;
        pwm_state[i].enabled       = (tmp[i].flags & 0x02) != 0;
        pwm_state[i].never_reset   = (tmp[i].flags & 0x04) != 0;
    }
}

void hw_opfs_pwm_write(void) {
    pwm_opfs_entry_t tmp[64];
    for (int i = 0; i < 64; i++) {
        tmp[i].duty_cycle = pwm_state[i].duty_cycle;
        tmp[i].frequency  = pwm_state[i].frequency;
        tmp[i].flags = (pwm_state[i].variable_freq ? 0x01 : 0)
                     | (pwm_state[i].enabled       ? 0x02 : 0)
                     | (pwm_state[i].never_reset   ? 0x04 : 0);
        tmp[i]._pad = 0;
    }
    _write_file(HW_PWM_STATE_PATH, tmp, sizeof(tmp));
    _pwm_mtime = _file_mtime(HW_PWM_STATE_PATH);
    hw_opfs_pwm_dirty = false;
}

/* ------------------------------------------------------------------ */
/* NeoPixel                                                            */
/* ------------------------------------------------------------------ */
/*
 * NeoPixel data is variable-length, so we use a different format:
 *   [count:u8] — number of active pins
 *   For each active pin:
 *     [pin:u8][num_bytes:u16le][pixels: num_bytes bytes]
 *
 * Python: struct.pack('<BHB', pin, num_bytes) + pixel_bytes
 *
 * Only writes active (enabled) pins. Reading back is not implemented
 * yet — the reactor writes pixel data, the worker reads it.
 */

/* Defined in neopixel_write/__init__.c */
typedef struct {
    uint8_t pixels[256 * 4];
    uint32_t num_bytes;
    bool enabled;
} neopixel_pin_state_t;
extern neopixel_pin_state_t neopixel_state[64];

void hw_opfs_neopixel_write(void) {
    /* Count active pins */
    int active = 0;
    for (int i = 0; i < 64; i++) {
        if (neopixel_state[i].enabled && neopixel_state[i].num_bytes > 0) {
            active++;
        }
    }

    /* Build output buffer: header + per-pin data */
    /* Max: 1 + 64*(1+2+1024) = ~65KB. In practice, 1-2 pins active. */
    int fd = open(HW_NEOPIXEL_DATA_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;

    uint8_t count = (uint8_t)active;
    write(fd, &count, 1);

    for (int i = 0; i < 64; i++) {
        if (!neopixel_state[i].enabled || neopixel_state[i].num_bytes == 0) {
            continue;
        }
        uint8_t pin = (uint8_t)i;
        uint16_t nbytes = (uint16_t)neopixel_state[i].num_bytes;
        write(fd, &pin, 1);
        write(fd, &nbytes, 2);  /* LE on wasm32 */
        write(fd, neopixel_state[i].pixels, nbytes);
    }

    close(fd);
    hw_opfs_neopixel_dirty = false;
}

/* ------------------------------------------------------------------ */
/* Bulk operations                                                     */
/* ------------------------------------------------------------------ */

void hw_opfs_init(void) {
    _ensure_dir("/hw");
    _ensure_dir("/hw/gpio");
    _ensure_dir("/hw/analog");
    _ensure_dir("/hw/pwm");
    _ensure_dir("/hw/neopixel");

    /* Seed files with initial state */
    hw_opfs_gpio_write();
    hw_opfs_analog_write();
    hw_opfs_pwm_write();
}

void hw_opfs_read_all(void) {
    hw_opfs_gpio_read();
    hw_opfs_analog_read();
    hw_opfs_pwm_read();
    /* neopixel: read not implemented yet (reactor → worker direction) */
}

void hw_opfs_flush_all(void) {
    if (hw_opfs_gpio_dirty)     hw_opfs_gpio_write();
    if (hw_opfs_analog_dirty)   hw_opfs_analog_write();
    if (hw_opfs_pwm_dirty)      hw_opfs_pwm_write();
    if (hw_opfs_neopixel_dirty) hw_opfs_neopixel_write();
}
