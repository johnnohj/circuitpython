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
    /* HAL register wrappers */
    { MP_ROM_QSTR(MP_QSTR_read_reg),        MP_ROM_PTR(&blinka_read_reg_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_reg),       MP_ROM_PTR(&blinka_write_reg_obj) },
    { MP_ROM_QSTR(MP_QSTR_sync_registers),  MP_ROM_PTR(&blinka_sync_registers_obj) },
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
