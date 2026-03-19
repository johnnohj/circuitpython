/*
 * binproto.h — Compact binary protocol for hardware events
 *
 * Replaces JSON serialization for hw events flowing through bc_out/bc_in.
 * Each message is a fixed-header + variable-payload structure:
 *
 *   Byte 0:    type tag   (uint8)
 *   Byte 1:    subcommand (uint8)
 *   Byte 2-3:  payload length (uint16 LE, excluding 4-byte header)
 *   Byte 4+:   payload (type/sub specific, fixed layout)
 *
 * Total message size = 4 + payload_length.
 * Maximum payload: 65535 bytes (sufficient for NeoPixel strips, framebuffers).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

// ---- Message type tags ----
#define BP_TYPE_GPIO      0x01
#define BP_TYPE_ANALOG    0x02
#define BP_TYPE_PWM       0x03
#define BP_TYPE_NEOPIXEL  0x04
#define BP_TYPE_I2C       0x05
#define BP_TYPE_SPI       0x06
#define BP_TYPE_DISPLAY   0x07
#define BP_TYPE_SLEEP     0x08
#define BP_TYPE_WORKER    0x09
#define BP_TYPE_CUSTOM    0xFF  // JSON fallback (UTF-8 payload)

// ---- Subcommands ----
// GPIO
#define BP_SUB_INIT       0x01
#define BP_SUB_WRITE      0x02
#define BP_SUB_READ       0x03
#define BP_SUB_DEINIT     0x04
// PWM
#define BP_SUB_UPDATE     0x02
// I2C
#define BP_SUB_SCAN       0x02
#define BP_SUB_WRITE_READ 0x05
// SPI
#define BP_SUB_CONFIGURE  0x02
#define BP_SUB_TRANSFER   0x04
// Display
#define BP_SUB_REFRESH    0x02
// Worker
#define BP_SUB_SPAWN      0x01
// Sleep
#define BP_SUB_REQUEST    0x01

// ---- Header ----
typedef struct {
    uint8_t  type;
    uint8_t  sub;
    uint16_t payload_len;  // little-endian
} __attribute__((packed)) bp_header_t;

#define BP_HEADER_SIZE 4

// ---- Fixed payloads per type ----

// GPIO: init/write/read/deinit
typedef struct {
    uint8_t  pin;       // pin index (0x00-0x15 matching register addresses)
    uint8_t  direction; // 0=input, 1=output
    uint8_t  pull;      // 0=none, 1=up, 2=down
    uint8_t  _pad;
    uint16_t value;     // 0 or 1 for digital
} __attribute__((packed)) bp_gpio_t;

// Analog: init/write/read/deinit
typedef struct {
    uint8_t  pin;
    uint8_t  direction;
    uint16_t value;     // 0-65535
} __attribute__((packed)) bp_analog_t;

// PWM: init/update/deinit
typedef struct {
    uint8_t  pin;
    uint8_t  _pad;
    uint16_t duty_cycle; // 0-65535
    uint32_t frequency;  // Hz
} __attribute__((packed)) bp_pwm_t;

// NeoPixel: init (fixed) or write (variable-length data follows)
typedef struct {
    uint8_t  pin;
    uint8_t  order;     // 0=GRB, 1=RGB, etc.
    uint16_t count;     // number of LEDs
    // For write: pixel data follows (count * bytes_per_pixel)
} __attribute__((packed)) bp_neopixel_t;

// I2C: variable commands
typedef struct {
    uint8_t  id;        // bus ID (0-based)
    uint8_t  addr;      // 7-bit device address
    uint16_t len;       // read/write length
    // For write/write_read: data bytes follow
} __attribute__((packed)) bp_i2c_t;

// SPI: variable commands
typedef struct {
    uint8_t  id;
    uint8_t  _pad;
    uint16_t len;
    // For write/transfer: data bytes follow
} __attribute__((packed)) bp_spi_t;

// Display: refresh
typedef struct {
    uint8_t  id;        // display ID
    uint8_t  _pad;
    uint16_t width;
    uint16_t height;
    uint32_t fb_offset; // offset into framebuffer OPFS region
} __attribute__((packed)) bp_display_t;

// Sleep: request
typedef struct {
    uint32_t ms;
} __attribute__((packed)) bp_sleep_t;

// Worker: spawn
typedef struct {
    uint16_t worker_id;
    uint16_t func_len;  // UTF-8 function name follows
    // func_name bytes follow
} __attribute__((packed)) bp_worker_t;

// ---- Ring buffer header (for OPFS events.bin) ----
typedef struct {
    uint32_t write_head;  // next write position (mod capacity)
    uint32_t read_head;   // next read position (mod capacity)
    uint32_t capacity;    // ring data size in bytes
    uint32_t flags;       // bit 0: overflow
} __attribute__((packed)) bp_ring_header_t;

#define BP_RING_HEADER_SIZE 16
#define BP_RING_FLAG_OVERFLOW 0x01

// ---- Encode/decode API ----

// Write a complete message (header + payload) to a buffer.
// Returns total bytes written (4 + payload_len), or 0 if buf_size too small.
size_t bp_encode(uint8_t *buf, size_t buf_size,
                 uint8_t type, uint8_t sub,
                 const void *payload, uint16_t payload_len);

// Decode a message header from a buffer.
// Returns 1 if valid header decoded, 0 if buffer too small.
int bp_decode_header(const uint8_t *buf, size_t buf_len, bp_header_t *out);

// Get pointer to payload (buf + BP_HEADER_SIZE). Caller must check bounds.
static inline const uint8_t *bp_payload(const uint8_t *buf) {
    return buf + BP_HEADER_SIZE;
}

// Total message size including header.
static inline size_t bp_msg_size(const bp_header_t *h) {
    return BP_HEADER_SIZE + h->payload_len;
}

// ---- Ring buffer operations ----

// Initialize ring buffer header. data_capacity = total size - BP_RING_HEADER_SIZE.
void bp_ring_init(bp_ring_header_t *ring, uint32_t data_capacity);

// Write a message to the ring buffer. Returns 1 on success, 0 if full (sets overflow flag).
int bp_ring_write(uint8_t *ring_buf, size_t ring_size,
                  uint8_t type, uint8_t sub,
                  const void *payload, uint16_t payload_len);

// Read next message from the ring buffer. Returns total message size, or 0 if empty.
// Copies message into out_buf (must be large enough for header + payload).
size_t bp_ring_read(uint8_t *ring_buf, size_t ring_size,
                    uint8_t *out_buf, size_t out_size);

// Check if ring has pending messages.
int bp_ring_pending(const uint8_t *ring_buf);
