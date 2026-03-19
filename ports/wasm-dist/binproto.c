/*
 * binproto.c — Binary protocol encode/decode + ring buffer operations
 */

#include "binproto.h"
#include <string.h>

// ---- Encode ----

size_t bp_encode(uint8_t *buf, size_t buf_size,
                 uint8_t type, uint8_t sub,
                 const void *payload, uint16_t payload_len) {
    size_t total = BP_HEADER_SIZE + payload_len;
    if (total > buf_size) {
        return 0;
    }
    buf[0] = type;
    buf[1] = sub;
    buf[2] = (uint8_t)(payload_len & 0xFF);
    buf[3] = (uint8_t)((payload_len >> 8) & 0xFF);
    if (payload_len > 0 && payload != NULL) {
        memcpy(buf + BP_HEADER_SIZE, payload, payload_len);
    }
    return total;
}

// ---- Decode ----

int bp_decode_header(const uint8_t *buf, size_t buf_len, bp_header_t *out) {
    if (buf_len < BP_HEADER_SIZE) {
        return 0;
    }
    out->type = buf[0];
    out->sub = buf[1];
    out->payload_len = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    return 1;
}

// ---- Ring buffer ----

void bp_ring_init(bp_ring_header_t *ring, uint32_t data_capacity) {
    ring->write_head = 0;
    ring->read_head = 0;
    ring->capacity = data_capacity;
    ring->flags = 0;
}

// Helper: get pointer to ring data (after header)
static inline uint8_t *ring_data(uint8_t *ring_buf) {
    return ring_buf + BP_RING_HEADER_SIZE;
}
static inline const uint8_t *ring_data_const(const uint8_t *ring_buf) {
    return ring_buf + BP_RING_HEADER_SIZE;
}

// Helper: bytes available for reading
static inline uint32_t ring_readable(const bp_ring_header_t *h) {
    return (h->write_head - h->read_head + h->capacity) % h->capacity;
}

// Helper: bytes available for writing
static inline uint32_t ring_writable(const bp_ring_header_t *h) {
    // Reserve 1 byte to distinguish full from empty
    return h->capacity - 1 - ring_readable(h);
}

int bp_ring_write(uint8_t *ring_buf, size_t ring_size,
                  uint8_t type, uint8_t sub,
                  const void *payload, uint16_t payload_len) {
    bp_ring_header_t *h = (bp_ring_header_t *)ring_buf;
    uint32_t msg_size = BP_HEADER_SIZE + payload_len;

    if (msg_size > ring_writable(h)) {
        h->flags |= BP_RING_FLAG_OVERFLOW;
        return 0;
    }

    uint8_t *data = ring_data(ring_buf);
    uint32_t cap = h->capacity;
    uint32_t wh = h->write_head;

    // Write header
    uint8_t hdr[BP_HEADER_SIZE];
    hdr[0] = type;
    hdr[1] = sub;
    hdr[2] = (uint8_t)(payload_len & 0xFF);
    hdr[3] = (uint8_t)((payload_len >> 8) & 0xFF);

    for (uint32_t i = 0; i < BP_HEADER_SIZE; i++) {
        data[wh] = hdr[i];
        wh = (wh + 1) % cap;
    }

    // Write payload
    if (payload_len > 0 && payload != NULL) {
        const uint8_t *src = (const uint8_t *)payload;
        for (uint32_t i = 0; i < payload_len; i++) {
            data[wh] = src[i];
            wh = (wh + 1) % cap;
        }
    }

    h->write_head = wh;
    return 1;
}

size_t bp_ring_read(uint8_t *ring_buf, size_t ring_size,
                    uint8_t *out_buf, size_t out_size) {
    bp_ring_header_t *h = (bp_ring_header_t *)ring_buf;

    if (ring_readable(h) < BP_HEADER_SIZE) {
        return 0; // empty or corrupted
    }

    uint8_t *data = ring_data(ring_buf);
    uint32_t cap = h->capacity;
    uint32_t rh = h->read_head;

    // Peek header
    uint8_t hdr[BP_HEADER_SIZE];
    uint32_t peek_rh = rh;
    for (uint32_t i = 0; i < BP_HEADER_SIZE; i++) {
        hdr[i] = data[peek_rh];
        peek_rh = (peek_rh + 1) % cap;
    }

    uint16_t payload_len = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    uint32_t msg_size = BP_HEADER_SIZE + payload_len;

    if (ring_readable(h) < msg_size) {
        return 0; // incomplete message
    }
    if (msg_size > out_size) {
        return 0; // output buffer too small
    }

    // Copy full message
    for (uint32_t i = 0; i < msg_size; i++) {
        out_buf[i] = data[rh];
        rh = (rh + 1) % cap;
    }

    h->read_head = rh;
    return msg_size;
}

int bp_ring_pending(const uint8_t *ring_buf) {
    const bp_ring_header_t *h = (const bp_ring_header_t *)ring_buf;
    return ring_readable(h) >= BP_HEADER_SIZE ? 1 : 0;
}
