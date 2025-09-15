#include "hal_provider.h"
#include "py/runtime.h"
#include "py/gc.h"
#include <string.h>

#define MAX_PROVIDERS 4
#define MAX_PINS 64

// Global provider registry
static const hal_provider_t *providers[MAX_PROVIDERS];
static size_t provider_count = 0;
static const hal_provider_t *active_provider = NULL;

// Pin registry
static hal_pin_t *pins[MAX_PINS];
static size_t pin_count = 0;

// Provider management
bool hal_register_provider(const hal_provider_t *provider) {
    if (provider_count >= MAX_PROVIDERS) {
        return false;
    }
    
    providers[provider_count] = provider;
    provider_count++;
    
    // Set as active provider if it's the first one registered
    if (active_provider == NULL) {
        active_provider = provider;
        if (provider->init) {
            provider->init();
        }
    }
    
    return true;
}

const hal_provider_t *hal_get_provider(void) {
    return active_provider;
}

const hal_provider_t *hal_get_provider_by_name(const char *name) {
    for (size_t i = 0; i < provider_count; i++) {
        if (strcmp(providers[i]->name, name) == 0) {
            return providers[i];
        }
    }
    return NULL;
}

bool hal_has_capability(hal_capability_t capability) {
    if (active_provider == NULL) {
        return false;
    }
    return (active_provider->capabilities & capability) != 0;
}

void hal_provider_init(void) {
    provider_count = 0;
    active_provider = NULL;
    pin_count = 0;
    
    // Clear provider and pin arrays
    memset(providers, 0, sizeof(providers));
    memset(pins, 0, sizeof(pins));
    
    // Initialize generic board as fallback
    // This will be available even if no UF2 is loaded
    extern void generic_board_init(void);
    generic_board_init();
}

void hal_provider_deinit(void) {
    // Deinitialize all providers
    for (size_t i = 0; i < provider_count; i++) {
        if (providers[i]->deinit) {
            providers[i]->deinit();
        }
    }
    
    // Clean up all pins
    for (size_t i = 0; i < pin_count; i++) {
        if (pins[i] && pins[i]->provider && pins[i]->provider->pin_ops && pins[i]->provider->pin_ops->pin_deinit) {
            pins[i]->provider->pin_ops->pin_deinit(pins[i]);
        }
    }
    
    hal_provider_init();
}

// Pin management
hal_pin_t *hal_pin_create(uint16_t number, const char *name, hal_capability_t caps) {
    if (pin_count >= MAX_PINS) {
        return NULL;
    }
    
    hal_pin_t *pin = m_new_obj(hal_pin_t);
    pin->base.type = &hal_pin_type;
    pin->number = number;
    pin->name = name;  // Assume name is static string
    pin->capabilities = caps;
    pin->provider_data = NULL;
    pin->provider = active_provider;
    
    pins[pin_count] = pin;
    pin_count++;
    
    return pin;
}

hal_pin_t *hal_pin_find_by_name(const char *name) {
    for (size_t i = 0; i < pin_count; i++) {
        if (pins[i] && strcmp(pins[i]->name, name) == 0) {
            return pins[i];
        }
    }
    return NULL;
}

hal_pin_t *hal_pin_find_by_number(uint16_t number) {
    for (size_t i = 0; i < pin_count; i++) {
        if (pins[i] && pins[i]->number == number) {
            return pins[i];
        }
    }
    return NULL;
}

// Pin capability checking
bool hal_pin_supports_digital(const hal_pin_t *pin) {
    return (pin->capabilities & HAL_CAP_DIGITAL_IO) != 0;
}

bool hal_pin_supports_analog_in(const hal_pin_t *pin) {
    return (pin->capabilities & HAL_CAP_ANALOG_IN) != 0;
}

bool hal_pin_supports_analog_out(const hal_pin_t *pin) {
    return (pin->capabilities & HAL_CAP_ANALOG_OUT) != 0;
}

// MicroPython pin object implementation
// hal_pin_print removed - not used in simplified type structure

mp_obj_t hal_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    
    // For now, assume args[0] is pin number
    mp_int_t pin_num = mp_obj_get_int(args[0]);
    
    // Look for existing pin
    hal_pin_t *pin = hal_pin_find_by_number(pin_num);
    if (pin != NULL) {
        return MP_OBJ_FROM_PTR(pin);
    }
    
    // Create new pin with basic capabilities
    pin = hal_pin_create(pin_num, "Pin", HAL_CAP_DIGITAL_IO | HAL_CAP_ANALOG_IN | HAL_CAP_ANALOG_OUT);
    if (pin == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("Cannot create pin"));
    }
    
    return MP_OBJ_FROM_PTR(pin);
}

const mp_obj_type_t hal_pin_type = {
    { &mp_type_type },
    .name = MP_QSTR_Pin,
};