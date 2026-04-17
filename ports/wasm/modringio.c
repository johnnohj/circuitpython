/*
 * RingIO stream type for micropython module.
 *
 * Ported from py/objringio.c to use CircuitPython's ringbuf API
 * (ringbuf_num_filled/ringbuf_num_empty/ringbuf_get_n/ringbuf_put_n)
 * instead of MicroPython upstream's (ringbuf_avail/ringbuf_free/
 * ringbuf_memcpy_get_internal/ringbuf_memcpy_put_internal).
 *
 * Original: Copyright (c) 2024 Andrew Leech, MIT License
 */

#include "py/ringbuf.h"
#include "py/mpconfig.h"

#if MICROPY_PY_MICROPYTHON_RINGIO

#include "py/runtime.h"
#include "py/stream.h"

typedef struct _micropython_ringio_obj_t {
    mp_obj_base_t base;
    ringbuf_t ringbuffer;
} micropython_ringio_obj_t;

static mp_obj_t micropython_ringio_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    mp_int_t buff_size = -1;
    mp_buffer_info_t bufinfo = {NULL, 0, 0};

    if (!mp_get_buffer(args[0], &bufinfo, MP_BUFFER_RW)) {
        buff_size = mp_obj_get_int(args[0]);
    }
    micropython_ringio_obj_t *self = mp_obj_malloc(micropython_ringio_obj_t, type);
    if (bufinfo.buf != NULL) {
        // Buffer passed in, use it directly for ringbuffer.
        ringbuf_init(&(self->ringbuffer), bufinfo.buf, bufinfo.len);
    } else {
        ringbuf_alloc(&(self->ringbuffer), buff_size);
    }
    return MP_OBJ_FROM_PTR(self);
}

static mp_uint_t micropython_ringio_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    micropython_ringio_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size = MIN(size, ringbuf_num_filled(&self->ringbuffer));
    ringbuf_get_n(&self->ringbuffer, buf_in, size);
    *errcode = 0;
    return size;
}

static mp_uint_t micropython_ringio_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    micropython_ringio_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size = MIN(size, ringbuf_num_empty(&self->ringbuffer));
    ringbuf_put_n(&self->ringbuffer, buf_in, size);
    *errcode = 0;
    return size;
}

static mp_uint_t micropython_ringio_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    micropython_ringio_obj_t *self = MP_OBJ_TO_PTR(self_in);
    switch (request) {
        case MP_STREAM_POLL: {
            mp_uint_t ret = 0;
            if ((arg & MP_STREAM_POLL_RD) && ringbuf_num_filled(&self->ringbuffer) > 0) {
                ret |= MP_STREAM_POLL_RD;
            }
            if ((arg & MP_STREAM_POLL_WR) && ringbuf_num_empty(&self->ringbuffer) > 0) {
                ret |= MP_STREAM_POLL_WR;
            }
            return ret;
        }
        case MP_STREAM_CLOSE:
            return 0;
    }
    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}

static mp_obj_t micropython_ringio_any(mp_obj_t self_in) {
    micropython_ringio_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(ringbuf_num_filled(&self->ringbuffer));
}
static MP_DEFINE_CONST_FUN_OBJ_1(micropython_ringio_any_obj, micropython_ringio_any);

static const mp_rom_map_elem_t micropython_ringio_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&micropython_ringio_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
};
static MP_DEFINE_CONST_DICT(micropython_ringio_locals_dict, micropython_ringio_locals_dict_table);

static const mp_stream_p_t ringio_stream_p = {
    .read = micropython_ringio_read,
    .write = micropython_ringio_write,
    .ioctl = micropython_ringio_ioctl,
    .is_text = false,
};

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_ringio,
    MP_QSTR_RingIO,
    MP_TYPE_FLAG_NONE,
    make_new, micropython_ringio_make_new,
    protocol, &ringio_stream_p,
    locals_dict, &micropython_ringio_locals_dict
    );

#endif // MICROPY_PY_MICROPYTHON_RINGIO
