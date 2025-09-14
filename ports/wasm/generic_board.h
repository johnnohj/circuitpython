// Generic Metro-style board configuration for WASM HAL
// Provides a comprehensive fallback board with common peripherals

#ifndef GENERIC_BOARD_H
#define GENERIC_BOARD_H

#include <stdint.h>

// Generic Metro board pin definitions
// Based on common Arduino Uno/Metro layout

typedef struct {
    const char* name;
    const char* mcu_pin;  // Virtual MCU pin name
    uint32_t capabilities;
} generic_pin_def_t;

// Pin capability flags
#define PIN_CAP_DIGITAL     (1 << 0)
#define PIN_CAP_ANALOG_IN   (1 << 1)
#define PIN_CAP_PWM         (1 << 2)
#define PIN_CAP_I2C         (1 << 3)
#define PIN_CAP_SPI         (1 << 4)
#define PIN_CAP_UART        (1 << 5)
#define PIN_CAP_TOUCH       (1 << 6)

// Standard Metro/Arduino pin layout
static const generic_pin_def_t generic_metro_pins[] = {
    // Digital pins (0-13)
    {"D0",  "PA00", PIN_CAP_DIGITAL | PIN_CAP_UART},  // RX
    {"D1",  "PA01", PIN_CAP_DIGITAL | PIN_CAP_UART},  // TX
    {"D2",  "PA02", PIN_CAP_DIGITAL},
    {"D3",  "PA03", PIN_CAP_DIGITAL | PIN_CAP_PWM},
    {"D4",  "PA04", PIN_CAP_DIGITAL},
    {"D5",  "PA05", PIN_CAP_DIGITAL | PIN_CAP_PWM},
    {"D6",  "PA06", PIN_CAP_DIGITAL | PIN_CAP_PWM},
    {"D7",  "PA07", PIN_CAP_DIGITAL},
    {"D8",  "PA08", PIN_CAP_DIGITAL},
    {"D9",  "PA09", PIN_CAP_DIGITAL | PIN_CAP_PWM},
    {"D10", "PA10", PIN_CAP_DIGITAL | PIN_CAP_PWM | PIN_CAP_SPI},  // SPI CS
    {"D11", "PA11", PIN_CAP_DIGITAL | PIN_CAP_PWM | PIN_CAP_SPI},  // SPI MOSI
    {"D12", "PA12", PIN_CAP_DIGITAL | PIN_CAP_SPI},                // SPI MISO
    {"D13", "PA13", PIN_CAP_DIGITAL | PIN_CAP_SPI},                // SPI SCK / LED
    
    // Analog pins (A0-A5)
    {"A0",  "PA14", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN},
    {"A1",  "PA15", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN},
    {"A2",  "PA16", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN},
    {"A3",  "PA17", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN},
    {"A4",  "PA18", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN | PIN_CAP_I2C},  // SDA
    {"A5",  "PA19", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN | PIN_CAP_I2C},  // SCL
    
    // Additional digital pins (14-19, mapped to analog)
    {"D14", "PA14", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN},  // Same as A0
    {"D15", "PA15", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN},  // Same as A1
    {"D16", "PA16", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN},  // Same as A2
    {"D17", "PA17", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN},  // Same as A3
    {"D18", "PA18", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN | PIN_CAP_I2C},  // Same as A4/SDA
    {"D19", "PA19", PIN_CAP_DIGITAL | PIN_CAP_ANALOG_IN | PIN_CAP_I2C},  // Same as A5/SCL
    
    // Special pins
    {"LED", "PA13", PIN_CAP_DIGITAL},     // Built-in LED (same as D13)
    {"BUTTON", "PA20", PIN_CAP_DIGITAL},  // User button
    {"NEOPIXEL", "PA21", PIN_CAP_DIGITAL}, // NeoPixel pin
    
    // Additional common pins
    {"SDA", "PA18", PIN_CAP_DIGITAL | PIN_CAP_I2C},
    {"SCL", "PA19", PIN_CAP_DIGITAL | PIN_CAP_I2C},
    {"MOSI", "PA11", PIN_CAP_DIGITAL | PIN_CAP_SPI},
    {"MISO", "PA12", PIN_CAP_DIGITAL | PIN_CAP_SPI},
    {"SCK", "PA13", PIN_CAP_DIGITAL | PIN_CAP_SPI},
    {"TX", "PA01", PIN_CAP_DIGITAL | PIN_CAP_UART},
    {"RX", "PA00", PIN_CAP_DIGITAL | PIN_CAP_UART},
};

#define GENERIC_METRO_PIN_COUNT (sizeof(generic_metro_pins) / sizeof(generic_metro_pins[0]))

// Board metadata
typedef struct {
    const char* board_name;
    const char* mcu_type;
    uint32_t flash_size;
    uint32_t ram_size;
    float cpu_frequency_mhz;
    float logic_level_v;
} generic_board_info_t;

static const generic_board_info_t generic_metro_info = {
    .board_name = "Generic Metro (WASM Simulator)",
    .mcu_type = "Virtual SAMD21G18",  // Pretend to be SAMD21 for compatibility
    .flash_size = 256 * 1024,         // 256KB flash
    .ram_size = 32 * 1024,            // 32KB RAM
    .cpu_frequency_mhz = 48.0,
    .logic_level_v = 3.3
};

// Peripheral configurations
typedef struct {
    const char* name;
    const char* default_pins[4];  // Up to 4 pins per peripheral
} generic_peripheral_t;

static const generic_peripheral_t generic_metro_peripherals[] = {
    {"I2C", {"SDA", "SCL", NULL, NULL}},
    {"SPI", {"MOSI", "MISO", "SCK", "D10"}},  // D10 as CS
    {"UART", {"TX", "RX", NULL, NULL}},
    {"NEOPIXEL", {"NEOPIXEL", NULL, NULL, NULL}},
};

#define GENERIC_METRO_PERIPHERAL_COUNT (sizeof(generic_metro_peripherals) / sizeof(generic_metro_peripherals[0]))

// Function declarations
void generic_board_init(void);
const char* generic_board_to_json(void);
int generic_board_apply_config(void);

// JavaScript interface functions
int mp_js_generic_pin_set_value(const char* pin_name, int value);
int mp_js_generic_pin_get_value(const char* pin_name);
int mp_js_generic_pin_set_direction(const char* pin_name, int direction);

#endif // GENERIC_BOARD_H