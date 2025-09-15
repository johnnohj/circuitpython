#ifndef HAL_PROVIDER_H
#define HAL_PROVIDER_H

#include "py/obj.h"

// Hardware provider capability flags
typedef enum {
    HAL_CAP_DIGITAL_IO = (1 << 0),
    HAL_CAP_ANALOG_IN = (1 << 1),
    HAL_CAP_ANALOG_OUT = (1 << 2),
    HAL_CAP_I2C = (1 << 3),
    HAL_CAP_SPI = (1 << 4),
    HAL_CAP_UART = (1 << 5),
    HAL_CAP_PWM = (1 << 6),
} hal_capability_t;

// Forward declarations
typedef struct hal_pin hal_pin_t;
typedef struct hal_provider hal_provider_t;

// Pin operations interface
typedef struct {
    // Digital I/O operations
    void (*digital_set_direction)(hal_pin_t *pin, bool output);
    void (*digital_set_value)(hal_pin_t *pin, bool value);
    bool (*digital_get_value)(hal_pin_t *pin);
    void (*digital_set_pull)(hal_pin_t *pin, int pull_mode);
    
    // Analog operations
    uint16_t (*analog_read)(hal_pin_t *pin);
    void (*analog_write)(hal_pin_t *pin, uint16_t value);
    
    // Cleanup
    void (*pin_deinit)(hal_pin_t *pin);
} hal_pin_ops_t;

// I2C operations interface
typedef struct {
    mp_obj_t (*i2c_create)(mp_obj_t scl_pin, mp_obj_t sda_pin, uint32_t frequency);
    bool (*i2c_try_lock)(mp_obj_t i2c_obj);
    void (*i2c_unlock)(mp_obj_t i2c_obj);
    void (*i2c_scan)(mp_obj_t i2c_obj, uint8_t *addresses, size_t *count);
    void (*i2c_writeto)(mp_obj_t i2c_obj, uint8_t addr, const uint8_t *data, size_t len);
    void (*i2c_readfrom)(mp_obj_t i2c_obj, uint8_t addr, uint8_t *data, size_t len);
    void (*i2c_deinit)(mp_obj_t i2c_obj);
} hal_i2c_ops_t;

// SPI operations interface
typedef struct {
    mp_obj_t (*spi_create)(mp_obj_t clk_pin, mp_obj_t mosi_pin, mp_obj_t miso_pin);
    void (*spi_configure)(mp_obj_t spi_obj, uint32_t baudrate, uint8_t polarity, uint8_t phase);
    bool (*spi_try_lock)(mp_obj_t spi_obj);
    void (*spi_unlock)(mp_obj_t spi_obj);
    void (*spi_write)(mp_obj_t spi_obj, const uint8_t *data, size_t len);
    void (*spi_readinto)(mp_obj_t spi_obj, uint8_t *buffer, size_t len);
    void (*spi_deinit)(mp_obj_t spi_obj);
} hal_spi_ops_t;

// Pin structure
struct hal_pin {
    mp_obj_base_t base;
    uint16_t number;                     // Pin number
    const char *name;                    // Pin name (e.g., "GP25", "D0")
    hal_capability_t capabilities;       // What this pin supports
    void *provider_data;                 // Provider-specific data
    const hal_provider_t *provider;      // Hardware provider
};

// Hardware provider structure
struct hal_provider {
    const char *name;                    // Provider name (e.g., "javascript", "simulation")
    hal_capability_t capabilities;       // Supported hardware capabilities
    
    // Operation interfaces
    const hal_pin_ops_t *pin_ops;       // Pin operations
    const hal_i2c_ops_t *i2c_ops;       // I2C operations  
    const hal_spi_ops_t *spi_ops;       // SPI operations
    
    // Provider lifecycle
    bool (*init)(void);                  // Initialize provider
    void (*deinit)(void);               // Cleanup provider
    
    // Board configuration
    mp_obj_t (*get_board_module)(void); // Return board pin definitions
};

// Provider registration and management
bool hal_register_provider(const hal_provider_t *provider);
const hal_provider_t *hal_get_provider(void);
const hal_provider_t *hal_get_provider_by_name(const char *name);
bool hal_has_capability(hal_capability_t capability);

// Provider initialization
void hal_provider_init(void);
void hal_provider_deinit(void);

// Pin management
hal_pin_t *hal_pin_create(uint16_t number, const char *name, hal_capability_t caps);
hal_pin_t *hal_pin_find_by_name(const char *name);
hal_pin_t *hal_pin_find_by_number(uint16_t number);

// Pin capability checking
bool hal_pin_supports_digital(const hal_pin_t *pin);
bool hal_pin_supports_analog_in(const hal_pin_t *pin);
bool hal_pin_supports_analog_out(const hal_pin_t *pin);

// MicroPython object interface
extern const mp_obj_type_t hal_pin_type;
mp_obj_t hal_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args);

#endif // HAL_PROVIDER_H