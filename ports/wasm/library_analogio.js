// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// ==============================================================================
// AnalogIO Functions - ADC/DAC State Access
// ==============================================================================
// State managed in common-hal/analogio/AnalogIn.c
// JavaScript reads/writes analog_state[] array for sensor simulation

mergeInto(LibraryManager.library, {
    mp_js_analog: () => {},  // Placeholder for postset initialization

    mp_js_analog__postset: `
        var analogStatePtr = null;
        function initAnalogState() {
            if (analogStatePtr === null) {
                try {
                    analogStatePtr = Module.ccall('get_analog_state_ptr', 'number', [], []);
                } catch (e) {
                    console.warn('Analog state not available:', e);
                }
            }
        }
        // analog_pin_state_t: uint16 value + bool is_output + bool enabled = 4 bytes
        function getAnalogPinView(pin) {
            if (analogStatePtr === null || pin >= 64) return null;
            return new DataView(Module.HEAPU8.buffer, analogStatePtr + pin * 4, 4);
        }
    `,

    mp_js_analog_get_value: (pin) => {
        if (analogStatePtr === null) initAnalogState();
        const view = getAnalogPinView(pin);
        return view ? view.getUint16(0, true) : 0;
    },

    mp_js_analog_set_input_value: (pin, value) => {
        if (analogStatePtr === null) initAnalogState();
        const view = getAnalogPinView(pin);
        if (view && !view.getUint8(2)) {  // Only if is_output == false (ADC)
            view.setUint16(0, value, true);
        }
    },
});
