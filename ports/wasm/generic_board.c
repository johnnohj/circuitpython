// Generic board implementation for CircuitPython WASM HAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <emscripten.h>

#include "generic_board.h"
#include "hal_provider.h"
#include "py/runtime.h"
#include "py/obj.h"

// Virtual pin states for simulation
typedef struct {
    const char* name;
    uint32_t capabilities;
    int value;
    int direction;  // 0=input, 1=output
    int pull;       // 0=none, 1=up, 2=down
    float analog_value;  // For analog pins
} virtual_pin_state_t;

static virtual_pin_state_t* virtual_pins = NULL;
static int virtual_pin_count = 0;
static int board_initialized = 0;

// Initialize the generic board
void generic_board_init(void) {
    if (board_initialized) {
        return;
    }
    
    // Allocate virtual pins
    virtual_pin_count = GENERIC_BOARD_PIN_COUNT;
    virtual_pins = calloc(virtual_pin_count, sizeof(virtual_pin_state_t));
    
    // Initialize each pin
    for (int i = 0; i < GENERIC_BOARD_PIN_COUNT; i++) {
        virtual_pins[i].name = generic_board_pins[i].name;
        virtual_pins[i].capabilities = generic_board_pins[i].capabilities;
        virtual_pins[i].value = 0;
        virtual_pins[i].direction = 0;  // Input by default
        virtual_pins[i].pull = 0;
        virtual_pins[i].analog_value = 0.0;
        
        // Special initializations
        if (strcmp(generic_board_pins[i].name, "BUTTON") == 0) {
            virtual_pins[i].value = 1;  // Button not pressed (pull-up)
            virtual_pins[i].pull = 1;   // Internal pull-up
        }
    }
    
    board_initialized = 1;
    
    // printf("Generic board initialized with %d pins\n", virtual_pin_count);
}

// Generate JSON representation of the board
const char* generic_board_to_json(void) {
    static char* json_buffer = NULL;
    
    if (json_buffer) {
        free(json_buffer);
    }
    
    // Allocate a large buffer for JSON
    json_buffer = malloc(8192);
    
    // Start JSON
    strcpy(json_buffer, "{");
    
    // Board info
    sprintf(json_buffer + strlen(json_buffer),
        "\"board_name\":\"%s\",", generic_board_info.board_name);
    sprintf(json_buffer + strlen(json_buffer),
        "\"mcu_type\":\"%s\",", generic_board_info.mcu_type);
    sprintf(json_buffer + strlen(json_buffer),
        "\"flash_size\":%u,", generic_board_info.flash_size);
    sprintf(json_buffer + strlen(json_buffer),
        "\"ram_size\":%u,", generic_board_info.ram_size);
    sprintf(json_buffer + strlen(json_buffer),
        "\"cpu_frequency_mhz\":%.1f,", generic_board_info.cpu_frequency_mhz);
    sprintf(json_buffer + strlen(json_buffer),
        "\"logic_level_v\":%.1f,", generic_board_info.logic_level_v);
    
    // Pins array
    strcat(json_buffer, "\"pins\":[");
    for (int i = 0; i < GENERIC_BOARD_PIN_COUNT; i++) {
        if (i > 0) strcat(json_buffer, ",");
        sprintf(json_buffer + strlen(json_buffer),
            "{\"name\":\"%s\",\"mcu_pin\":\"%s\",\"capabilities\":%u}",
            generic_board_pins[i].name,
            generic_board_pins[i].mcu_pin,
            generic_board_pins[i].capabilities
        );
    }
    strcat(json_buffer, "],");
    
    // Peripherals array
    strcat(json_buffer, "\"peripherals\":[");
    for (int i = 0; i < GENERIC_BOARD_PERIPHERAL_COUNT; i++) {
        if (i > 0) strcat(json_buffer, ",");
        sprintf(json_buffer + strlen(json_buffer),
            "{\"name\":\"%s\",\"pins\":[",
            generic_board_peripherals[i].name
        );
        
        // Add peripheral pins
        for (int j = 0; j < 4 && generic_board_peripherals[i].default_pins[j]; j++) {
            if (j > 0) strcat(json_buffer, ",");
            sprintf(json_buffer + strlen(json_buffer),
                "\"%s\"", generic_board_peripherals[i].default_pins[j]
            );
        }
        strcat(json_buffer, "]}");
    }
    strcat(json_buffer, "]}");
    
    return json_buffer;
}

// Apply the generic board configuration to the HAL
int generic_board_apply_config(void) {
    if (!board_initialized) {
        generic_board_init();
    }
    
    // Register pins with HAL provider
    for (int i = 0; i < virtual_pin_count; i++) {
        // Here we would register each pin with the HAL system
        // For now, just log it
        printf("Registering pin %s with capabilities 0x%x\n",
               virtual_pins[i].name, virtual_pins[i].capabilities);
    }
    
    return 0;
}

// JavaScript-callable functions
EMSCRIPTEN_KEEPALIVE
int mp_js_init_generic_board(void) {
    generic_board_init();
    return generic_board_apply_config();
}

EMSCRIPTEN_KEEPALIVE
const char* mp_js_get_generic_board_json(void) {
    if (!board_initialized) {
        generic_board_init();
    }
    return generic_board_to_json();
}

// Virtual pin operations for simulation
EMSCRIPTEN_KEEPALIVE
int mp_js_generic_pin_set_value(const char* pin_name, int value) {
    if (!board_initialized) return -1;
    
    for (int i = 0; i < virtual_pin_count; i++) {
        if (strcmp(virtual_pins[i].name, pin_name) == 0) {
            virtual_pins[i].value = value;
            
            // Special handling for LED
            if (strcmp(pin_name, "LED") == 0 || strcmp(pin_name, "D13") == 0) {
                EM_ASM({
                    if (Module.onLEDChange) {
                        Module.onLEDChange($0);
                    }
                }, value);
            }
            
            return 0;
        }
    }
    return -1;  // Pin not found
}

EMSCRIPTEN_KEEPALIVE
int mp_js_generic_pin_get_value(const char* pin_name) {
    if (!board_initialized) return -1;
    
    for (int i = 0; i < virtual_pin_count; i++) {
        if (strcmp(virtual_pins[i].name, pin_name) == 0) {
            // Special handling for button
            if (strcmp(pin_name, "BUTTON") == 0) {
                // Allow JavaScript to override button state
                EM_ASM({
                    if (Module.getButtonState) {
                        return Module.getButtonState();
                    }
                });
            }
            return virtual_pins[i].value;
        }
    }
    return -1;  // Pin not found
}

EMSCRIPTEN_KEEPALIVE
int mp_js_generic_pin_set_direction(const char* pin_name, int direction) {
    if (!board_initialized) return -1;
    
    for (int i = 0; i < virtual_pin_count; i++) {
        if (strcmp(virtual_pins[i].name, pin_name) == 0) {
            virtual_pins[i].direction = direction;
            return 0;
        }
    }
    return -1;
}

// Generate Python board module dynamically
EMSCRIPTEN_KEEPALIVE
const char* mp_js_generate_board_module(void) {
    static char* module_source = NULL;
    
    if (module_source) {
        free(module_source);
    }
    
    module_source = malloc(4096);
    strcpy(module_source, "# Auto-generated board module for Generic Board\n");
    strcat(module_source, "# This module provides pin definitions for the simulated board\n\n");
    
    // Add pin constants
    for (int i = 0; i < GENERIC_BOARD_PIN_COUNT; i++) {
        sprintf(module_source + strlen(module_source),
                "%s = '%s'\n", 
                generic_board_pins[i].name,
                generic_board_pins[i].name);
    }
    
    // Add helper information
    strcat(module_source, "\n# Board information\n");
    sprintf(module_source + strlen(module_source),
            "board_id = '%s'\n", generic_board_info.board_name);
    
    // Add peripheral groupings
    strcat(module_source, "\n# Peripheral pin groups\n");
    strcat(module_source, "I2C_PINS = (SDA, SCL)\n");
    strcat(module_source, "SPI_PINS = (MOSI, MISO, SCK)\n");
    strcat(module_source, "UART_PINS = (TX, RX)\n");
    
    return module_source;
}