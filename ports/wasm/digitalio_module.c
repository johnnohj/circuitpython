#include "py/obj.h"
#include "py/runtime.h"
#include "hal_provider.h"

// CircuitPython-style digitalio module implementation

// DigitalInOut class
typedef struct {
    mp_obj_base_t base;
    hal_pin_t *pin;
    bool output_mode;
    bool current_value;
} digitalio_digitalinout_obj_t;

// Forward declaration for type
static const mp_obj_type_t digitalio_digitalinout_type;

// Direction enum values
typedef enum {
    DIRECTION_INPUT = 0,
    DIRECTION_OUTPUT = 1,
} digitalio_direction_t;

// Pull enum values  
typedef enum {
    PULL_NONE = 0,
    PULL_UP = 1,
    PULL_DOWN = 2,
} digitalio_pull_t;


static mp_obj_t digitalio_digitalinout_set_direction(mp_obj_t self_in, mp_obj_t value) {
    digitalio_digitalinout_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const hal_provider_t *provider = hal_get_provider();
    
    digitalio_direction_t dir = mp_obj_get_int(value);
    bool output = (dir == DIRECTION_OUTPUT);
    
    if (provider && provider->pin_ops && provider->pin_ops->digital_set_direction) {
        provider->pin_ops->digital_set_direction(self->pin, output);
    }
    
    self->output_mode = output;
    return mp_const_none;
}

MP_DEFINE_CONST_FUN_OBJ_2(digitalio_digitalinout_set_direction_obj, digitalio_digitalinout_set_direction);


static mp_obj_t digitalio_digitalinout_set_value(mp_obj_t self_in, mp_obj_t value) {
    digitalio_digitalinout_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const hal_provider_t *provider = hal_get_provider();
    
    if (!self->output_mode) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pin not configured as output"));
    }
    
    bool val = mp_obj_is_true(value);
    self->current_value = val;
    
    if (provider && provider->pin_ops && provider->pin_ops->digital_set_value) {
        provider->pin_ops->digital_set_value(self->pin, val);
    }
    
    return mp_const_none;
}

MP_DEFINE_CONST_FUN_OBJ_2(digitalio_digitalinout_set_value_obj, digitalio_digitalinout_set_value);


// Simplified module - removed unused functions


// Complete DigitalInOut type definition
static const mp_obj_type_t digitalio_digitalinout_type = {
    { &mp_type_type },
    .name = MP_QSTR_DigitalInOut,
};


const mp_obj_type_t digitalio_direction_type = {
    { &mp_type_type },
    .name = MP_QSTR_Direction,
};


const mp_obj_type_t digitalio_pull_type = {
    { &mp_type_type },
    .name = MP_QSTR_Pull,
};

// digitalio module globals
static const mp_rom_map_elem_t digitalio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_digitalio) },
    { MP_ROM_QSTR(MP_QSTR_DigitalInOut), MP_ROM_PTR(&digitalio_digitalinout_type) },
    { MP_ROM_QSTR(MP_QSTR_Direction), MP_ROM_PTR(&digitalio_direction_type) },
    { MP_ROM_QSTR(MP_QSTR_Pull), MP_ROM_PTR(&digitalio_pull_type) },
};
static MP_DEFINE_CONST_DICT(digitalio_module_globals, digitalio_module_globals_table);

const mp_obj_module_t digitalio_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&digitalio_module_globals,
};

// Register the digitalio module
MP_REGISTER_MODULE(MP_QSTR_digitalio, digitalio_module);