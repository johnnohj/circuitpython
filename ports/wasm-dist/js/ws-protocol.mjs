/**
 * ws-protocol.mjs — Wippersnapper-aligned message protocol
 *
 * Defines message types and helpers for sync bus communication.
 * Field names and semantics match Wippersnapper Protobuf definitions
 * (https://github.com/adafruit/Wippersnapper_Protobuf) so bridging
 * to real Protobuf/MQTT is a direct mapping.
 *
 * This is JSON over postMessage for now.  The same message shapes
 * work over:
 *   - postMessage (iframe ↔ parent, Worker ↔ parent)
 *   - SharedArrayBuffer (sync bus regions)
 *   - WebSerial/WebUSB (to physical boards)
 *   - MQTT (to Adafruit IO / Wippersnapper cloud)
 */

// ── Signal types (top-level routing, matches signal.proto oneof) ──

export const SignalType = {
    PIN_CONFIG:     'pin_config',      // ConfigurePinRequest
    PIN_EVENT:      'pin_event',       // PinEvent
    PIXELS_CREATE:  'pixels_create',   // PixelsCreateRequest
    PIXELS_WRITE:   'pixels_write',    // PixelsWriteRequest
    PIXELS_DELETE:  'pixels_delete',   // PixelsDeleteRequest
    I2C_INIT:       'i2c_init',        // I2CBusInitRequest
    I2C_SCAN:       'i2c_scan',        // I2CBusScanRequest
    I2C_DEVICE:     'i2c_device',      // I2CDeviceInitRequest
    I2C_EVENT:      'i2c_event',       // I2CDeviceEvent
    DISPLAY_WRITE:  'display_write',   // framebuffer update
    SERIAL_DATA:    'serial_data',     // serial ring data
    BOARD_RESET:    'board_reset',     // full reset
};

// ── Pin enums (match pin.proto) ──

export const PinMode = {
    DIGITAL: 0,
    ANALOG:  1,
};

export const PinDirection = {
    INPUT:  0,
    OUTPUT: 1,
};

export const PinPull = {
    NONE: 0,
    UP:   1,
    DOWN: 2,
};

export const RequestType = {
    CREATE: 0,
    UPDATE: 1,
    DELETE: 2,
};

// ── Pixel enums (match pixels.proto) ──

export const PixelsType = {
    NEOPIXEL: 0,
    DOTSTAR:  1,
};

export const PixelsOrder = {
    GRB:  0,   // NeoPixel default
    RGB:  1,
    GRBW: 2,
    RGBW: 3,
    BRG:  4,   // DotStar default
};

// ── Sensor types (match i2c.proto SensorType, subset) ──

export const SensorType = {
    TEMPERATURE:    0,
    HUMIDITY:       1,
    PRESSURE:       2,
    ALTITUDE:       3,
    LIGHT:          4,
    ACCELERATION_X: 5,
    ACCELERATION_Y: 6,
    ACCELERATION_Z: 7,
    VOLTAGE:        8,
    CURRENT:        9,
};

// ── Message builders ──

/** Configure a GPIO pin (matches ConfigurePinRequest) */
export function pinConfig(pinName, pinId, opts = {}) {
    return {
        signal: SignalType.PIN_CONFIG,
        request_type: opts.requestType ?? RequestType.CREATE,
        pin_name: pinName,
        pin_id: pinId,
        mode: opts.mode ?? PinMode.DIGITAL,
        direction: opts.direction ?? PinDirection.INPUT,
        pull: opts.pull ?? PinPull.NONE,
        period: opts.period ?? 0,
    };
}

/** Pin value change (matches PinEvent) */
export function pinEvent(pinName, pinId, value) {
    return {
        signal: SignalType.PIN_EVENT,
        pin_name: pinName,
        pin_id: pinId,
        pin_value: typeof value === 'boolean' ? (value ? 1 : 0) : value,
    };
}

/** Create a NeoPixel strip (matches PixelsCreateRequest) */
export function pixelsCreate(pinName, opts = {}) {
    return {
        signal: SignalType.PIXELS_CREATE,
        pixels_pin_neopixel: pinName,
        pixels_type: opts.type ?? PixelsType.NEOPIXEL,
        pixels_num: opts.count ?? 10,
        pixels_ordering: opts.order ?? PixelsOrder.GRB,
        pixels_brightness: opts.brightness ?? 255,
    };
}

/** Write pixel colors (matches PixelsWriteRequest) */
export function pixelsWrite(pinName, colors) {
    return {
        signal: SignalType.PIXELS_WRITE,
        pixels_pin_neopixel: pinName,
        pixels_color: colors,  // array of uint32 (0xWWRRGGBB)
    };
}

/** I2C bus init (matches I2CBusInitRequest) */
export function i2cInit(busId, opts = {}) {
    return {
        signal: SignalType.I2C_INIT,
        bus_id: busId,
        pin_sda: opts.sda,
        pin_scl: opts.scl,
        frequency: opts.frequency ?? 100000,
    };
}

/** I2C sensor event (matches I2CDeviceEvent) */
export function i2cEvent(address, sensorType, value) {
    return {
        signal: SignalType.I2C_EVENT,
        i2c_device_address: address,
        sensor_event: [{ type: sensorType, value }],
    };
}

/** Display framebuffer update */
export function displayWrite(width, height, data) {
    return {
        signal: SignalType.DISPLAY_WRITE,
        width,
        height,
        data,  // Uint8Array of RGB565
    };
}

/** Serial data (bytes in a direction) */
export function serialData(direction, bytes) {
    return {
        signal: SignalType.SERIAL_DATA,
        direction,  // 'tx' or 'rx'
        data: bytes, // Uint8Array
    };
}

/** Full board reset */
export function boardReset() {
    return { signal: SignalType.BOARD_RESET };
}


// ── Sync bus ↔ protocol helpers ──

/**
 * Convert a GPIO slot (12 bytes) to a WS PinEvent.
 * @param {number} pinId
 * @param {Uint8Array} slot — 12-byte GPIO slot
 * @param {string} [pinName]
 * @returns {object} PinEvent message
 */
export function gpioSlotToEvent(pinId, slot, pinName) {
    return {
        signal: SignalType.PIN_EVENT,
        pin_name: pinName || `P${pinId}`,
        pin_id: pinId,
        pin_value: slot[2],
        enabled: slot[0],
        direction: slot[1],
        pull: slot[3],
        category: slot[5],
    };
}

/**
 * Convert a WS PinEvent to a GPIO slot write.
 * @param {object} event — PinEvent message
 * @param {Uint8Array} slot — 12-byte slot to modify
 */
export function eventToGpioSlot(event, slot) {
    if (event.enabled !== undefined) slot[0] = event.enabled;
    if (event.direction !== undefined) slot[1] = event.direction;
    if (event.pin_value !== undefined) slot[2] = event.pin_value;
    if (event.pull !== undefined) slot[3] = event.pull;
}

/**
 * Convert neopixel slab data to a WS PixelsWriteRequest.
 * @param {Uint8Array} data — header(4) + GRB bytes
 * @returns {object} PixelsWriteRequest with uint32 color array
 */
export function neopixelSlabToWrite(data) {
    const enabled = data[1];
    const numBytes = data[2] | (data[3] << 8);
    const bpp = 3;
    const count = enabled ? Math.floor(numBytes / bpp) : 0;
    const colors = [];
    for (let i = 0; i < count; i++) {
        const base = 4 + i * bpp;
        const g = data[base], r = data[base + 1], b = data[base + 2];
        colors.push((r << 16) | (g << 8) | b);
    }
    return pixelsWrite('NEOPIXEL', colors);
}
