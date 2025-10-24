# Extending the Virtual Hardware Pattern

## Overview

The virtual hardware pattern we've established for GPIO and analog I/O can be extended to all CircuitPython peripherals. This document shows how to apply the same architecture to displayio, I2C sensors, SPI devices, and more.

## Core Pattern

The pattern has three layers:

```
┌─────────────────────────────────────────────────────────┐
│ 1. Python API (unchanged from real CircuitPython)      │
│    import displayio, board                              │
│    display = board.DISPLAY                              │
│    display.show(group)                                  │
└────────────────┬────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────┐
│ 2. common-hal/*.c (synchronous C implementation)        │
│    - Calls virtual_*.c functions directly               │
│    - No yielding, no message queue                      │
│    - Instant operations                                 │
└────────────────┬────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────┐
│ 3. virtual_*.c (internal state + JS interface)         │
│    - Stores peripheral state in C structures            │
│    - Exports functions for JS to observe/inject         │
│    - EMSCRIPTEN_KEEPALIVE for JS access                 │
└─────────────────────────────────────────────────────────┘
                 ↕
┌─────────────────────────────────────────────────────────┐
│ JavaScript (External World - Optional)                  │
│ - Read outputs: framebuffer, I2C writes, SPI data       │
│ - Inject inputs: sensor readings, device responses      │
│ - Visualize: render display, show I2C traffic           │
└─────────────────────────────────────────────────────────┘
```

## Example 1: DisplayIO

### Use Case
Render CircuitPython graphics in a browser canvas.

### Implementation

#### virtual_display.h
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Display configuration
#define VIRTUAL_DISPLAY_WIDTH 320
#define VIRTUAL_DISPLAY_HEIGHT 240
#define VIRTUAL_DISPLAY_BPP 16  // RGB565

// Display state
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t *framebuffer;  // RGB565 framebuffer
    bool enabled;
    bool needs_refresh;  // Dirty flag for JS
} virtual_display_t;

// Initialize display
void virtual_display_init(void);

// CircuitPython calls these (from common-hal)
void virtual_display_set_brightness(uint8_t brightness);
void virtual_display_refresh(void);
void virtual_display_fill_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void virtual_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels);

// JavaScript interface
EMSCRIPTEN_KEEPALIVE
const uint16_t *virtual_display_get_framebuffer(void);

EMSCRIPTEN_KEEPALIVE
bool virtual_display_needs_refresh(void);

EMSCRIPTEN_KEEPALIVE
void virtual_display_clear_refresh_flag(void);
```

#### virtual_display.c
```c
#include "virtual_display.h"
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

static virtual_display_t display;

void virtual_display_init(void) {
    display.width = VIRTUAL_DISPLAY_WIDTH;
    display.height = VIRTUAL_DISPLAY_HEIGHT;
    display.framebuffer = malloc(VIRTUAL_DISPLAY_WIDTH * VIRTUAL_DISPLAY_HEIGHT * sizeof(uint16_t));
    display.enabled = true;
    display.needs_refresh = false;

    // Clear to black
    memset(display.framebuffer, 0, VIRTUAL_DISPLAY_WIDTH * VIRTUAL_DISPLAY_HEIGHT * sizeof(uint16_t));
}

void virtual_display_fill_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    for (uint16_t row = y; row < y + h && row < display.height; row++) {
        for (uint16_t col = x; col < x + w && col < display.width; col++) {
            display.framebuffer[row * display.width + col] = color;
        }
    }
    display.needs_refresh = true;
}

void virtual_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels) {
    for (uint16_t row = 0; row < h && (y + row) < display.height; row++) {
        for (uint16_t col = 0; col < w && (x + col) < display.width; col++) {
            display.framebuffer[(y + row) * display.width + (x + col)] = pixels[row * w + col];
        }
    }
    display.needs_refresh = true;
}

void virtual_display_refresh(void) {
    // Just set the dirty flag - JavaScript will poll for updates
    display.needs_refresh = true;
}

// JavaScript interface
EMSCRIPTEN_KEEPALIVE
const uint16_t *virtual_display_get_framebuffer(void) {
    return display.framebuffer;
}

EMSCRIPTEN_KEEPALIVE
bool virtual_display_needs_refresh(void) {
    return display.needs_refresh;
}

EMSCRIPTEN_KEEPALIVE
void virtual_display_clear_refresh_flag(void) {
    display.needs_refresh = false;
}
```

#### JavaScript Integration
```javascript
// In api.js, expose display functions
_virtual_display_get_framebuffer: Module._virtual_display_get_framebuffer,
_virtual_display_needs_refresh: Module._virtual_display_needs_refresh,
_virtual_display_clear_refresh_flag: Module._virtual_display_clear_refresh_flag,

// In HTML page
const canvas = document.getElementById('display');
const ctx = canvas.getContext('2d');
const imageData = ctx.createImageData(320, 240);

function updateDisplay() {
    if (ctpy._virtual_display_needs_refresh()) {
        // Get framebuffer pointer
        const fbPtr = ctpy._virtual_display_get_framebuffer();

        // Read RGB565 data from WASM memory
        const fb = new Uint16Array(
            ctpy._module.HEAPU8.buffer,
            fbPtr,
            320 * 240
        );

        // Convert RGB565 to RGBA8888
        for (let i = 0; i < fb.length; i++) {
            const rgb565 = fb[i];
            const r = ((rgb565 >> 11) & 0x1F) << 3;
            const g = ((rgb565 >> 5) & 0x3F) << 2;
            const b = (rgb565 & 0x1F) << 3;

            const idx = i * 4;
            imageData.data[idx + 0] = r;
            imageData.data[idx + 1] = g;
            imageData.data[idx + 2] = b;
            imageData.data[idx + 3] = 255;
        }

        ctx.putImageData(imageData, 0, 0);
        ctpy._virtual_display_clear_refresh_flag();
    }

    requestAnimationFrame(updateDisplay);
}

updateDisplay();
```

## Example 2: I2C Devices

### Use Case
Simulate I2C sensors (temperature, accelerometer, etc.) with JavaScript providing readings.

### Implementation

#### virtual_i2c.h
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_I2C_DEVICES 16
#define MAX_I2C_BUFFER 256

typedef struct {
    uint8_t address;
    bool registered;
    uint8_t read_buffer[MAX_I2C_BUFFER];
    uint16_t read_buffer_len;
    uint8_t write_buffer[MAX_I2C_BUFFER];
    uint16_t write_buffer_len;
} i2c_device_t;

// Initialize I2C subsystem
void virtual_i2c_init(void);

// CircuitPython calls these (from common-hal/busio/I2C.c)
bool virtual_i2c_probe(uint8_t address);
bool virtual_i2c_write(uint8_t address, const uint8_t *data, uint16_t len);
bool virtual_i2c_read(uint8_t address, uint8_t *data, uint16_t len);
bool virtual_i2c_write_read(uint8_t address, const uint8_t *write_data, uint16_t write_len,
                              uint8_t *read_data, uint16_t read_len);

// JavaScript interface - Register simulated devices
EMSCRIPTEN_KEEPALIVE
void virtual_i2c_register_device(uint8_t address);

EMSCRIPTEN_KEEPALIVE
void virtual_i2c_set_read_data(uint8_t address, const uint8_t *data, uint16_t len);

EMSCRIPTEN_KEEPALIVE
const uint8_t *virtual_i2c_get_last_write(uint8_t address, uint16_t *len_out);
```

#### virtual_i2c.c
```c
#include "virtual_i2c.h"
#include <string.h>
#include <emscripten.h>

static i2c_device_t devices[MAX_I2C_DEVICES];

void virtual_i2c_init(void) {
    memset(devices, 0, sizeof(devices));
}

static i2c_device_t *find_device(uint8_t address) {
    for (int i = 0; i < MAX_I2C_DEVICES; i++) {
        if (devices[i].registered && devices[i].address == address) {
            return &devices[i];
        }
    }
    return NULL;
}

bool virtual_i2c_probe(uint8_t address) {
    return find_device(address) != NULL;
}

bool virtual_i2c_write(uint8_t address, const uint8_t *data, uint16_t len) {
    i2c_device_t *dev = find_device(address);
    if (!dev || len > MAX_I2C_BUFFER) return false;

    memcpy(dev->write_buffer, data, len);
    dev->write_buffer_len = len;
    return true;
}

bool virtual_i2c_read(uint8_t address, uint8_t *data, uint16_t len) {
    i2c_device_t *dev = find_device(address);
    if (!dev || len > dev->read_buffer_len) return false;

    memcpy(data, dev->read_buffer, len);
    return true;
}

// JavaScript interface
EMSCRIPTEN_KEEPALIVE
void virtual_i2c_register_device(uint8_t address) {
    for (int i = 0; i < MAX_I2C_DEVICES; i++) {
        if (!devices[i].registered) {
            devices[i].address = address;
            devices[i].registered = true;
            devices[i].read_buffer_len = 0;
            devices[i].write_buffer_len = 0;
            return;
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void virtual_i2c_set_read_data(uint8_t address, const uint8_t *data, uint16_t len) {
    i2c_device_t *dev = find_device(address);
    if (dev && len <= MAX_I2C_BUFFER) {
        memcpy(dev->read_buffer, data, len);
        dev->read_buffer_len = len;
    }
}

EMSCRIPTEN_KEEPALIVE
const uint8_t *virtual_i2c_get_last_write(uint8_t address, uint16_t *len_out) {
    i2c_device_t *dev = find_device(address);
    if (dev) {
        *len_out = dev->write_buffer_len;
        return dev->write_buffer;
    }
    *len_out = 0;
    return NULL;
}
```

#### JavaScript Integration
```javascript
// Simulate a BME280 temperature/humidity sensor at address 0x76
ctpy._virtual_i2c_register_device(0x76);

// Simulate sensor readings
setInterval(() => {
    // BME280 returns 20 bytes of raw sensor data
    const tempHumData = new Uint8Array(20);

    // Fake temperature: 25°C
    const tempRaw = Math.floor((25 + Math.random() * 2) * 100);
    tempHumData[3] = (tempRaw >> 12) & 0xFF;
    tempHumData[4] = (tempRaw >> 4) & 0xFF;
    tempHumData[5] = (tempRaw << 4) & 0xFF;

    // Fake humidity: 50%
    const humRaw = Math.floor((50 + Math.random() * 5) * 100);
    tempHumData[6] = (humRaw >> 8) & 0xFF;
    tempHumData[7] = humRaw & 0xFF;

    // Copy to WASM memory and set as read data
    const ptr = ctpy._malloc(20);
    ctpy._module.HEAPU8.set(tempHumData, ptr);
    ctpy._virtual_i2c_set_read_data(0x76, ptr, 20);
    ctpy._free(ptr);
}, 1000);
```

## Example 3: NeoPixel/WS2812 LEDs

### Use Case
Visualize addressable RGB LEDs in browser.

#### virtual_neopixel.h
```c
#pragma once
#include <stdint.h>

#define MAX_NEOPIXELS 256

typedef struct {
    uint8_t r, g, b;
} rgb_t;

void virtual_neopixel_init(void);
void virtual_neopixel_write(uint8_t pin, const rgb_t *pixels, uint16_t count);

// JavaScript interface
EMSCRIPTEN_KEEPALIVE
const rgb_t *virtual_neopixel_get_pixels(uint8_t pin, uint16_t *count_out);
```

#### JavaScript Integration
```javascript
// Render NeoPixels as colored divs
function updateNeoPixels() {
    const container = document.getElementById('neopixels');
    const countPtr = ctpy._malloc(2);
    const pixelsPtr = ctpy._virtual_neopixel_get_pixels(board.NEOPIXEL, countPtr);
    const count = new Uint16Array(ctpy._module.HEAPU8.buffer, countPtr, 1)[0];
    ctpy._free(countPtr);

    const pixels = new Uint8Array(ctpy._module.HEAPU8.buffer, pixelsPtr, count * 3);

    container.innerHTML = '';
    for (let i = 0; i < count; i++) {
        const div = document.createElement('div');
        div.className = 'neopixel';
        const r = pixels[i * 3];
        const g = pixels[i * 3 + 1];
        const b = pixels[i * 3 + 2];
        div.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
        container.appendChild(div);
    }

    requestAnimationFrame(updateNeoPixels);
}
```

## Adding New Virtual Hardware - Checklist

1. **Create virtual_*.h and virtual_*.c**
   - Define state structures
   - Implement initialization
   - Implement operations called by common-hal
   - Add EMSCRIPTEN_KEEPALIVE exports for JS

2. **Update common-hal/*.c**
   - Replace message queue calls with direct virtual_* calls
   - Make all operations synchronous
   - Remove WAIT_FOR_REQUEST_COMPLETION macros

3. **Update Makefile**
   - Add virtual_*.c to SRC_C
   - Add exports to EXPORTED_FUNCTIONS_EXTRA

4. **Update api.js**
   - Expose JS interface functions on returned object

5. **Document the Interface**
   - Add section to JS_HARDWARE_INTERFACE.md
   - Create demo HTML/JS if applicable

## Performance Considerations

- **Polling vs Events**: Current pattern uses polling (JS reads state in loop). For high-frequency updates, consider shared memory or callbacks.

- **Memory Access**: Direct WASM memory access is fast (<1μs). Safe to poll at 60fps.

- **Large Data**: For framebuffers/large arrays, expose pointers rather than copying data.

## Future Enhancements

1. **Change Notifications**
   - Add callback system for peripheral changes
   - Reduce polling overhead

2. **WebGL Rendering**
   - Use WebGL for faster display rendering
   - Hardware-accelerated graphics

3. **Web Serial/WebUSB**
   - Connect real hardware to virtual peripherals
   - Bridge physical sensors to WASM

4. **Network Peripherals**
   - WebSocket-based I2C/SPI devices
   - Cloud-connected sensors

---

**Key Principle**: Keep hardware self-contained in WASM. JavaScript observes and injects, but doesn't control the core operation!
