/*
 * modfwip.c — Firmware package installer for CircuitPython WASM.
 *
 * Python has pip, MicroPython has mip, we have fwip.
 *
 * fwip.install("neopixel") writes the request to a shared buffer
 * and signals the supervisor to enter SUP_FWIP_BUSY state.  The
 * supervisor polls the buffer each frame (pure C, no VM) and prints
 * status updates.  JS does the async fetch and writes results back.
 * When done, the supervisor returns to SUP_REPL.
 */

#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"

#include "supervisor/semihosting.h"

/* Defined in semihosting.c */
extern uintptr_t sh_fwip_addr(void);

/* Flag checked by cp_step() — declared in supervisor.c */
extern volatile bool fwip_request_pending;

/* ------------------------------------------------------------------ */
/* fwip.install(name)                                                  */
/* ------------------------------------------------------------------ */

static mp_obj_t fwip_install(mp_obj_t name_obj) {
    const char *name = mp_obj_str_get_str(name_obj);
    fwip_buf_t *buf = (fwip_buf_t *)sh_fwip_addr();

    /* Check if a previous install is still running */
    if (fwip_request_pending) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("fwip: install already in progress"));
    }

    /* Write request — JS picks this up in pollFwip() */
    memset(buf, 0, sizeof(*buf));
    strncpy(buf->name, name, FWIP_NAME_MAX - 1);
    buf->name[FWIP_NAME_MAX - 1] = '\0';
    buf->state = FWIP_STATE_PENDING;
    buf->command = FWIP_CMD_INSTALL;

    /* Signal the supervisor to enter SUP_FWIP_BUSY after this
     * REPL expression returns.  The expression completes normally
     * (returns None), then the supervisor takes over polling. */
    fwip_request_pending = true;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(fwip_install_obj, fwip_install);

/* ------------------------------------------------------------------ */
/* fwip.remove(name)                                                   */
/* ------------------------------------------------------------------ */

static mp_obj_t fwip_remove(mp_obj_t name_obj) {
    const char *name = mp_obj_str_get_str(name_obj);
    fwip_buf_t *buf = (fwip_buf_t *)sh_fwip_addr();

    if (fwip_request_pending) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("fwip: operation already in progress"));
    }

    memset(buf, 0, sizeof(*buf));
    strncpy(buf->name, name, FWIP_NAME_MAX - 1);
    buf->name[FWIP_NAME_MAX - 1] = '\0';
    buf->state = FWIP_STATE_PENDING;
    buf->command = FWIP_CMD_REMOVE;

    fwip_request_pending = true;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(fwip_remove_obj, fwip_remove);

/* ------------------------------------------------------------------ */
/* Module definition                                                   */
/* ------------------------------------------------------------------ */

static const mp_rom_map_elem_t fwip_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_fwip) },
    { MP_ROM_QSTR(MP_QSTR_install),  MP_ROM_PTR(&fwip_install_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove),   MP_ROM_PTR(&fwip_remove_obj) },
};
static MP_DEFINE_CONST_DICT(fwip_module_globals, fwip_module_globals_table);

const mp_obj_module_t mp_module_fwip = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fwip_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_fwip, mp_module_fwip);
