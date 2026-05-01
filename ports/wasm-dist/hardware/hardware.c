// hardware.c — standalone hardware simulation layer
//
// Owns GPIO, NeoPixel, and analog state. The VM writes hardware state
// through JS bridge functions. The UI reads directly from this binary's
// memory. No MicroPython, no libc, no WASI.
//
// Build:
//   clang --target=wasm32 -nostdlib -O2 \
//     -Wl,--no-entry -Wl,--export-dynamic \
//     -o hardware.wasm hardware.c

#include <stdint.h>

// ── GPIO ──

#define GPIO_SLOT_SIZE 12
#define GPIO_MAX_PINS  32

// Slot layout (matches ports/wasm common-hal convention):
//   [0] enabled   [1] direction  [2] value   [3] pull
//   [4] flags     [5] category   [6] role    [7] latched
//   [8..11] reserved
static uint8_t gpio[GPIO_MAX_PINS * GPIO_SLOT_SIZE];
static uint32_t gpio_change_count;

// ── NeoPixel ──

#define NEOPIXEL_HEADER   4    // [pin:u8][count:u8][bpp:u8][pad:u8]
#define NEOPIXEL_MAX_PIX  64
#define NEOPIXEL_BUF_SIZE (NEOPIXEL_HEADER + NEOPIXEL_MAX_PIX * 4)

static uint8_t neopixel[NEOPIXEL_BUF_SIZE];
static uint32_t neopixel_change_count;

// ── Analog ──

#define ANALOG_SLOT_SIZE 4     // [value_hi:u8][value_lo:u8][enabled:u8][pad:u8]
#define ANALOG_MAX_PINS  8

static uint8_t analog[ANALOG_MAX_PINS * ANALOG_SLOT_SIZE];

// ── Address exports (JS creates typed array views over these) ──

__attribute__((export_name("hw_gpio_addr")))
uintptr_t hw_gpio_addr(void) { return (uintptr_t)gpio; }

__attribute__((export_name("hw_gpio_size")))
uint32_t hw_gpio_size(void) { return sizeof(gpio); }

__attribute__((export_name("hw_neopixel_addr")))
uintptr_t hw_neopixel_addr(void) { return (uintptr_t)neopixel; }

__attribute__((export_name("hw_neopixel_size")))
uint32_t hw_neopixel_size(void) { return sizeof(neopixel); }

__attribute__((export_name("hw_analog_addr")))
uintptr_t hw_analog_addr(void) { return (uintptr_t)analog; }

__attribute__((export_name("hw_analog_size")))
uint32_t hw_analog_size(void) { return sizeof(analog); }

// ── Change counters (UI polls these to decide whether to repaint) ──

__attribute__((export_name("hw_gpio_changes")))
uint32_t hw_gpio_changes(void) { return gpio_change_count; }

__attribute__((export_name("hw_neopixel_changes")))
uint32_t hw_neopixel_changes(void) { return neopixel_change_count; }

// ── GPIO operations ──

__attribute__((export_name("hw_gpio_claim")))
void hw_gpio_claim(uint32_t pin, uint32_t direction, uint32_t pull, uint32_t category) {
    uint8_t *s = &gpio[pin * GPIO_SLOT_SIZE];
    s[0] = 1;           // enabled
    s[1] = direction;
    s[3] = pull;
    s[5] = category;
    gpio_change_count++;
}

__attribute__((export_name("hw_gpio_release")))
void hw_gpio_release(uint32_t pin) {
    uint8_t *s = &gpio[pin * GPIO_SLOT_SIZE];
    for (int i = 0; i < GPIO_SLOT_SIZE; i++) s[i] = 0;
    gpio_change_count++;
}

__attribute__((export_name("hw_gpio_set_value")))
void hw_gpio_set_value(uint32_t pin, uint32_t val) {
    gpio[pin * GPIO_SLOT_SIZE + 2] = val;
    gpio_change_count++;
}

__attribute__((export_name("hw_gpio_get_value")))
uint32_t hw_gpio_get_value(uint32_t pin) {
    return gpio[pin * GPIO_SLOT_SIZE + 2];
}

// Bulk copy — JS writes a full GPIO snapshot from vm.wasm's port_mem
__attribute__((export_name("hw_gpio_write_all")))
void hw_gpio_write_all(const uint8_t *src, uint32_t len) {
    uint32_t n = len < sizeof(gpio) ? len : sizeof(gpio);
    for (uint32_t i = 0; i < n; i++) gpio[i] = src[i];
    gpio_change_count++;
}

// ── NeoPixel operations ──

// Bulk copy — JS writes neopixel buffer from vm.wasm's port_mem
__attribute__((export_name("hw_neopixel_write")))
void hw_neopixel_write(const uint8_t *src, uint32_t len) {
    uint32_t n = len < sizeof(neopixel) ? len : sizeof(neopixel);
    for (uint32_t i = 0; i < n; i++) neopixel[i] = src[i];
    neopixel_change_count++;
}

// ── Reset ──

__attribute__((export_name("hw_reset")))
void hw_reset(void) {
    for (uint32_t i = 0; i < sizeof(gpio); i++) gpio[i] = 0;
    for (uint32_t i = 0; i < sizeof(neopixel); i++) neopixel[i] = 0;
    for (uint32_t i = 0; i < sizeof(analog); i++) analog[i] = 0;
    gpio_change_count = 0;
    neopixel_change_count = 0;
}
