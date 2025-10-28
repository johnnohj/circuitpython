// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Jeff Epler for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <emscripten.h>

#include "py/runtime.h"
#include "common-hal/microcontroller/Pin.h"
#include "common-hal/rotaryio/IncrementalEncoder.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "shared-bindings/rotaryio/IncrementalEncoder.h"
#include "shared-module/rotaryio/IncrementalEncoder.h"
#include "supervisor/shared/translate/translate.h"

// ============================================================================
// WASM PORT ROTARY ENCODER IMPLEMENTATION
// ============================================================================
//
// This implementation uses the shared-module's software encoder logic for
// quadrature decoding. The WASM port provides pin state management while
// the shared-module handles the state machine logic.
//
// ARCHITECTURE:
// - State arrays store encoder configuration (pins, divisor, position)
// - JavaScript simulates pin readings and calls update functions
// - Shared-module performs quadrature decoding via shared_module_softencoder_state_update()
// - Supports up to 4 rotary encoders simultaneously
//
// PIN READING:
// JavaScript code reads the simulated encoder pins and calls:
//   Module._rotaryio_update_encoder(encoder_index, pin_a_state, pin_b_state)
// which triggers the shared-module's state machine.
//
// ============================================================================

#define MAX_ENCODERS 4

// Encoder state storage
typedef struct {
    uint8_t pin_a;
    uint8_t pin_b;
    int8_t divisor;
    mp_int_t position;
    uint8_t state;  // Current AB state (2 bits)
    int8_t sub_count;  // Sub-count for divisor
    bool enabled;
    bool never_reset;
} encoder_state_t;

static encoder_state_t encoder_states[MAX_ENCODERS] = {{0}};

// Export encoder state pointer for JavaScript access
EMSCRIPTEN_KEEPALIVE
encoder_state_t *get_rotaryio_state_ptr(void) {
    return encoder_states;
}

// Find free encoder slot
static int8_t find_free_encoder(void) {
    for (int i = 0; i < MAX_ENCODERS; i++) {
        if (!encoder_states[i].enabled) {
            return i;
        }
    }
    return -1;
}

// Reset all encoders (called from supervisor on reset, unless never_reset)
void rotaryio_reset(void) {
    for (int i = 0; i < MAX_ENCODERS; i++) {
        if (!encoder_states[i].never_reset) {
            encoder_states[i].enabled = false;
            encoder_states[i].pin_a = 0;
            encoder_states[i].pin_b = 0;
            encoder_states[i].divisor = 4;
            encoder_states[i].position = 0;
            encoder_states[i].state = 0;
            encoder_states[i].sub_count = 0;
        }
    }
}

// ============================================================================
// COMMON-HAL API IMPLEMENTATION
// ============================================================================

void common_hal_rotaryio_incrementalencoder_construct(rotaryio_incrementalencoder_obj_t *self,
    const mcu_pin_obj_t *pin_a, const mcu_pin_obj_t *pin_b) {

    // Find free encoder slot
    int8_t encoder_index = find_free_encoder();
    if (encoder_index < 0) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("All rotary encoder peripherals in use"));
    }

    // Claim pins
    claim_pin(pin_a);
    claim_pin(pin_b);

    // Initialize object
    self->pin_a = pin_a;
    self->pin_b = pin_b;
    self->encoder_index = encoder_index;
    self->divisor = 4;  // Default divisor (4 transitions per detent)
    self->position = 0;
    self->state = 0;
    self->sub_count = 0;
    self->never_reset = false;

    // Initialize state array
    encoder_state_t *state = &encoder_states[encoder_index];
    state->pin_a = pin_a->number;
    state->pin_b = pin_b->number;
    state->divisor = 4;
    state->position = 0;
    state->state = 0;
    state->sub_count = 0;
    state->enabled = true;
    state->never_reset = false;

    // Initialize using shared-module (reads initial pin state)
    // Note: JavaScript should provide initial pin states
    // For now, assume both pins start LOW (state = 0b00)
    shared_module_softencoder_state_init(self, 0);
}

void common_hal_rotaryio_incrementalencoder_deinit(rotaryio_incrementalencoder_obj_t *self) {
    if (common_hal_rotaryio_incrementalencoder_deinited(self)) {
        return;
    }

    // Release pins
    reset_pin_number(self->pin_a->number);
    reset_pin_number(self->pin_b->number);

    // Clear state
    encoder_states[self->encoder_index].enabled = false;
    self->pin_a = NULL;
    self->pin_b = NULL;
}

bool common_hal_rotaryio_incrementalencoder_deinited(rotaryio_incrementalencoder_obj_t *self) {
    return self->pin_a == NULL;
}

void common_hal_rotaryio_incrementalencoder_mark_deinit(rotaryio_incrementalencoder_obj_t *self) {
    self->pin_a = NULL;
    self->pin_b = NULL;
}

// ============================================================================
// NEVER RESET SUPPORT
// ============================================================================

void common_hal_rotaryio_incrementalencoder_never_reset(rotaryio_incrementalencoder_obj_t *self) {
    if (common_hal_rotaryio_incrementalencoder_deinited(self)) {
        return;
    }

    // Mark encoder as never_reset
    self->never_reset = true;
    encoder_states[self->encoder_index].never_reset = true;

    // Mark pins as never_reset
    never_reset_pin_number(self->pin_a->number);
    never_reset_pin_number(self->pin_b->number);
}

// ============================================================================
// UPDATE FUNCTION (called from JavaScript)
// ============================================================================

// This function is called from JavaScript when encoder pins change state
// JavaScript reads simulated encoder pins and provides new A/B states
EMSCRIPTEN_KEEPALIVE
void rotaryio_update_encoder(uint8_t encoder_index, uint8_t pin_a_state, uint8_t pin_b_state) {
    if (encoder_index >= MAX_ENCODERS) {
        return;
    }

    encoder_state_t *state = &encoder_states[encoder_index];
    if (!state->enabled) {
        return;
    }

    // Construct new 2-bit state from pin states
    uint8_t new_state = ((pin_a_state & 1) << 1) | (pin_b_state & 1);

    // Create temporary encoder object for shared-module
    rotaryio_incrementalencoder_obj_t temp_encoder;
    temp_encoder.state = state->state;
    temp_encoder.sub_count = state->sub_count;
    temp_encoder.divisor = state->divisor;
    temp_encoder.position = state->position;

    // Update using shared-module quadrature decoding
    shared_module_softencoder_state_update(&temp_encoder, new_state);

    // Write back to state array
    state->state = temp_encoder.state;
    state->sub_count = temp_encoder.sub_count;
    state->position = temp_encoder.position;
}

// ============================================================================
// POSITION AND DIVISOR ACCESSORS
// ============================================================================
// Note: These are defined in shared-module/rotaryio/IncrementalEncoder.c
// when CIRCUITPY_ROTARYIO_SOFTENCODER is enabled, but we need to ensure
// our local position is synced with the state array.

// Position getter - override shared-module version to sync with state array
mp_int_t common_hal_rotaryio_incrementalencoder_get_position(rotaryio_incrementalencoder_obj_t *self) {
    if (common_hal_rotaryio_incrementalencoder_deinited(self)) {
        return 0;
    }

    // Sync from state array (may have been updated by JavaScript)
    self->position = encoder_states[self->encoder_index].position;
    return self->position;
}

// Position setter - override shared-module version to sync with state array
void common_hal_rotaryio_incrementalencoder_set_position(rotaryio_incrementalencoder_obj_t *self,
    mp_int_t new_position) {
    if (common_hal_rotaryio_incrementalencoder_deinited(self)) {
        return;
    }

    self->position = new_position;
    encoder_states[self->encoder_index].position = new_position;
}

// Divisor getter - override shared-module version to sync with state array
mp_int_t common_hal_rotaryio_incrementalencoder_get_divisor(rotaryio_incrementalencoder_obj_t *self) {
    if (common_hal_rotaryio_incrementalencoder_deinited(self)) {
        return 4;
    }

    return self->divisor;
}

// Divisor setter - override shared-module version to sync with state array
void common_hal_rotaryio_incrementalencoder_set_divisor(rotaryio_incrementalencoder_obj_t *self,
    mp_int_t new_divisor) {
    if (common_hal_rotaryio_incrementalencoder_deinited(self)) {
        return;
    }

    self->divisor = new_divisor;
    encoder_states[self->encoder_index].divisor = new_divisor;
}
