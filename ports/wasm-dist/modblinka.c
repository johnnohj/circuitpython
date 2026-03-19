/*
 * modblinka.c — _blinka builtin Python module for WASM-dist
 *
 * Thin Python wrappers over the HAL (mphalport.c) register file and the
 * bc_out event bus.  This is the Blinka bridge: Python ↔ browser hardware.
 *
 * Python API:
 *   _blinka.send(json_str)       → append to /dev/bc_out (Python→JS event bus)
 *   _blinka.read_reg(addr)       → read 16-bit hardware register (from HAL)
 *   _blinka.write_reg(addr, val) → write 16-bit hardware register (in HAL)
 *   _blinka.sync_registers()     → explicit bc_in sync (HAL does this auto every 64 bytecodes)
 *   _blinka.pin_to_reg(name)     → map pin name string to register address
 *   _blinka.ticks_ms()           → wall-clock milliseconds
 *   _blinka.REG_LED, etc.        → register address constants
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "binproto.h"
#include "opfs_regions.h"

/* ── _blinka.send(json_str) — bc_out transport ──────────────────────────── */

/* libpybroadcast.js: enqueue JSON string to ring buffer (no MEMFS I/O) */
extern void mp_bc_out_enqueue(const char *json, size_t len);

static mp_obj_t blinka_send(mp_obj_t msg_obj) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(msg_obj, &bufinfo, MP_BUFFER_READ);

    mp_bc_out_enqueue((const char *)bufinfo.buf, bufinfo.len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(blinka_send_obj, blinka_send);

/* ── _blinka.send_bin(type, sub, payload_bytes) — binary bc_out ─────────── */
/* Writes a binary-protocol message to the OPFS events ring buffer.
 * Also enqueues JSON on the legacy bc_out path for backward compatibility. */

uint8_t _events_ring[BP_RING_HEADER_SIZE + 8192];
int _events_ring_inited = 0;

static mp_obj_t blinka_send_bin(mp_obj_t type_obj, mp_obj_t sub_obj, mp_obj_t payload_obj) {
    uint8_t type = (uint8_t)mp_obj_get_int(type_obj);
    uint8_t sub  = (uint8_t)mp_obj_get_int(sub_obj);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(payload_obj, &bufinfo, MP_BUFFER_READ);

    // Initialize ring on first use
    if (!_events_ring_inited) {
        bp_ring_init((bp_ring_header_t *)_events_ring, 8192);
        _events_ring_inited = 1;
    }

    // Write to binary ring buffer (local; flushed to OPFS by drain task)
    bp_ring_write(_events_ring, sizeof(_events_ring),
                  type, sub, bufinfo.buf, (uint16_t)bufinfo.len);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(blinka_send_bin_obj, blinka_send_bin);

/* ── _blinka.read_sensor_response(max_len) — read I2C/SPI response from OPFS ── */
/* Returns bytes from the OPFS sensors region (cross-worker mailbox).
 * Layout: [0:1] = response length (u16 LE), [2:2+len] = response data.
 * Falls back to empty bytes if OPFS not initialized or no response pending. */

static mp_obj_t blinka_read_sensor_response(mp_obj_t max_len_obj) {
    int max_len = mp_obj_get_int(max_len_obj);
    if (max_len <= 0) max_len = 256;

    // Read length header from OPFS sensors region
    uint8_t hdr[2];
    int n = opfs_read(OPFS_REGION_SENSORS, 0, hdr, 2);
    if (n < 2) {
        return mp_obj_new_bytes(NULL, 0);
    }
    uint16_t resp_len = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
    if (resp_len == 0 || resp_len > (uint16_t)max_len) {
        if (resp_len > (uint16_t)max_len) resp_len = (uint16_t)max_len;
        if (resp_len == 0) return mp_obj_new_bytes(NULL, 0);
    }

    // Read response data
    uint8_t buf[256];
    uint16_t read_len = resp_len > 256 ? 256 : resp_len;
    n = opfs_read(OPFS_REGION_SENSORS, 2, buf, read_len);
    return mp_obj_new_bytes(buf, n > 0 ? (size_t)n : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(blinka_read_sensor_response_obj, blinka_read_sensor_response);

/* ── C-callable ring buffer write (for JS interceptor layer) ────────────── */
/* JS calls this during mp_bc_out_flush to encode I2C events into binary
 * without adding Python call depth. */
void bp_events_write(uint8_t type, uint8_t sub,
                     const uint8_t *payload, uint16_t payload_len) {
    if (!_events_ring_inited) {
        bp_ring_init((bp_ring_header_t *)_events_ring, 8192);
        _events_ring_inited = 1;
    }
    bp_ring_write(_events_ring, sizeof(_events_ring),
                  type, sub, payload, payload_len);
}

/* Read next message from events ring. Returns msg size or 0. */
size_t bp_events_read(uint8_t *out_buf, size_t out_size) {
    if (!_events_ring_inited) return 0;
    return bp_ring_read(_events_ring, sizeof(_events_ring), out_buf, out_size);
}

/* Check if events ring has pending messages. */
int bp_events_pending(void) {
    if (!_events_ring_inited) return 0;
    return bp_ring_pending(_events_ring);
}

/* Flush events ring to OPFS events region for cross-worker visibility. */
void bp_events_sync_to_opfs(void) {
    if (!_events_ring_inited) return;
    // Write the entire ring (header + data) to OPFS events region
    opfs_write(OPFS_REGION_EVENTS, 0, _events_ring, sizeof(_events_ring));
}

/* ── _blinka.read_reg(addr) — wrapper over HAL ─────────────────────────── */

static mp_obj_t blinka_read_reg(mp_obj_t addr_obj) {
    return mp_obj_new_int(hw_reg_read(mp_obj_get_int(addr_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(blinka_read_reg_obj, blinka_read_reg);

/* ── _blinka.write_reg(addr, val) — wrapper over HAL ───────────────────── */

static mp_obj_t blinka_write_reg(mp_obj_t addr_obj, mp_obj_t val_obj) {
    hw_reg_write(mp_obj_get_int(addr_obj), (uint16_t)mp_obj_get_int(val_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(blinka_write_reg_obj, blinka_write_reg);

/* ── _blinka.sync_registers() — flush bc_out + sync bc_in ─────────────── */

/* libpybroadcast.js: flush outgoing ring buffer → BroadcastChannel */
extern int mp_bc_out_flush(void);
/* libpytasks.js: run all background tasks (includes interceptors) */
extern int mp_tasks_poll(void);

static mp_obj_t blinka_sync_registers(void) {
    /* Flush bc_out first so interceptors (sensor simulators) can process
     * outgoing I2C/SPI events and write responses to MEMFS mailbox files
     * BEFORE we return to Python which will read those mailbox files. */
    mp_tasks_poll();
    /* Then sync incoming register state from bc_in */
    hw_reg_sync_from_bc_in();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(blinka_sync_registers_obj, blinka_sync_registers);

/* ── _blinka.pin_to_reg(name) — pin name → register address ────────────── */

static mp_obj_t blinka_pin_to_reg(mp_obj_t pin_obj) {
    size_t len;
    const char *pin = mp_obj_str_get_data(pin_obj, &len);

    if (len == 3 && pin[0] == 'L' && pin[1] == 'E' && pin[2] == 'D') {
        return mp_obj_new_int(HW_REG_LED);
    }
    if (len >= 2 && pin[0] == 'D') {
        int n = (int)strtol(pin + 1, NULL, 10);
        if (n >= 0 && n <= 13) {
            return mp_obj_new_int(HW_REG_D0 + n);
        }
    }
    if (len >= 2 && pin[0] == 'A') {
        int n = (int)strtol(pin + 1, NULL, 10);
        if (n >= 0 && n <= 5) {
            return mp_obj_new_int(HW_REG_A0 + n);
        }
    }
    if (len == 6 && memcmp(pin, "BUTTON", 6) == 0) {
        return mp_obj_new_int(HW_REG_BUTTON);
    }
    return mp_obj_new_int(-1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(blinka_pin_to_reg_obj, blinka_pin_to_reg);

/* ── _blinka.ticks_ms() — wall-clock milliseconds ──────────────────────── */

extern int mp_js_ticks_ms(void);

static mp_obj_t blinka_ticks_ms(void) {
    return mp_obj_new_int(mp_js_ticks_ms());
}
static MP_DEFINE_CONST_FUN_OBJ_0(blinka_ticks_ms_obj, blinka_ticks_ms);

/* ── _blinka.async_sleep(ms) — request JS event loop yield ──────────────
 * Called by asyncio core and time.sleep to request a real async delay.
 * Sets the delay in libpyasync.js and requests a VM yield so the JS
 * stepping driver (runStepped) can honour it with setTimeout.          */

extern void mp_async_request_delay(int ms);

static mp_obj_t blinka_async_sleep(mp_obj_t ms_obj) {
    int ms = mp_obj_get_int(ms_obj);
    if (ms < 0) { ms = 0; }
    mp_async_request_delay(ms);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(blinka_async_sleep_obj, blinka_async_sleep);

/* ── Module table ───────────────────────────────────────────────────────── */

static const mp_rom_map_elem_t blinka_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),        MP_ROM_QSTR(MP_QSTR__blinka) },
    /* bc_out transport */
    { MP_ROM_QSTR(MP_QSTR_send),            MP_ROM_PTR(&blinka_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_send_bin),        MP_ROM_PTR(&blinka_send_bin_obj) },
    /* Binary protocol type constants */
    { MP_ROM_QSTR(MP_QSTR_BP_GPIO),         MP_ROM_INT(BP_TYPE_GPIO) },
    { MP_ROM_QSTR(MP_QSTR_BP_ANALOG),       MP_ROM_INT(BP_TYPE_ANALOG) },
    { MP_ROM_QSTR(MP_QSTR_BP_PWM),          MP_ROM_INT(BP_TYPE_PWM) },
    { MP_ROM_QSTR(MP_QSTR_BP_NEOPIXEL),     MP_ROM_INT(BP_TYPE_NEOPIXEL) },
    { MP_ROM_QSTR(MP_QSTR_BP_I2C),          MP_ROM_INT(BP_TYPE_I2C) },
    { MP_ROM_QSTR(MP_QSTR_BP_SPI),          MP_ROM_INT(BP_TYPE_SPI) },
    { MP_ROM_QSTR(MP_QSTR_BP_DISPLAY),      MP_ROM_INT(BP_TYPE_DISPLAY) },
    { MP_ROM_QSTR(MP_QSTR_BP_SLEEP),        MP_ROM_INT(BP_TYPE_SLEEP) },
    { MP_ROM_QSTR(MP_QSTR_BP_INIT),         MP_ROM_INT(BP_SUB_INIT) },
    { MP_ROM_QSTR(MP_QSTR_BP_WRITE),        MP_ROM_INT(BP_SUB_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_BP_READ),         MP_ROM_INT(BP_SUB_READ) },
    { MP_ROM_QSTR(MP_QSTR_BP_DEINIT),       MP_ROM_INT(BP_SUB_DEINIT) },
    /* HAL register wrappers */
    { MP_ROM_QSTR(MP_QSTR_read_reg),        MP_ROM_PTR(&blinka_read_reg_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_reg),       MP_ROM_PTR(&blinka_write_reg_obj) },
    { MP_ROM_QSTR(MP_QSTR_sync_registers),  MP_ROM_PTR(&blinka_sync_registers_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_sensor_response), MP_ROM_PTR(&blinka_read_sensor_response_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin_to_reg),      MP_ROM_PTR(&blinka_pin_to_reg_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms),        MP_ROM_PTR(&blinka_ticks_ms_obj) },
    /* Async delay: request JS event loop yield */
    { MP_ROM_QSTR(MP_QSTR_async_sleep),    MP_ROM_PTR(&blinka_async_sleep_obj) },
    /* Register address constants (from mphalport.h) */
    { MP_ROM_QSTR(MP_QSTR_REG_LED),    MP_ROM_INT(HW_REG_LED) },
    { MP_ROM_QSTR(MP_QSTR_REG_BUTTON), MP_ROM_INT(HW_REG_BUTTON) },
    { MP_ROM_QSTR(MP_QSTR_REG_FLAGS),  MP_ROM_INT(HW_REG_FLAGS) },
};
static MP_DEFINE_CONST_DICT(blinka_module_globals, blinka_module_globals_table);

const mp_obj_module_t mp_module_blinka = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&blinka_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__blinka, mp_module_blinka);
