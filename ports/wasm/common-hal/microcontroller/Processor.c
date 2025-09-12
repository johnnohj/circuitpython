// Stub implementation for WebAssembly microcontroller processor
#include "shared-bindings/microcontroller/Processor.h"

// Processor instance
const mcu_processor_obj_t common_hal_mcu_processor_obj = {
    .base = { &mcu_processor_type },
};

// Stub functions for processor operations
float common_hal_mcu_processor_get_temperature(void) {
    return 25.0f; // Return nominal temperature for WebAssembly
}

uint32_t common_hal_mcu_processor_get_frequency(void) {
    return 1000000; // 1MHz nominal frequency for WebAssembly
}

void common_hal_mcu_processor_get_uid(uint8_t raw_id[]) {
    // Set a fixed UID for WebAssembly
    for (int i = 0; i < 16; i++) {
        raw_id[i] = 0xAB;
    }
}

void common_hal_mcu_processor_set_frequency(uint32_t frequency) {
    // No-op for WebAssembly
}

mcu_reset_reason_t common_hal_mcu_processor_get_reset_reason(void) {
    return RESET_REASON_UNKNOWN; // Always unknown for WebAssembly
}