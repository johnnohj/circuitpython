// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// ==============================================================================
// DigitalIO Functions - GPIO State Access
// ==============================================================================
// State managed in common-hal/digitalio/DigitalInOut.c
// JavaScript reads/writes gpio_state[] array for hardware simulation

mergeInto(LibraryManager.library, {
    mp_js_gpio: () => {},  // Placeholder for postset initialization

    mp_js_gpio__postset: `
        var gpioStatePtr = null;
        function initGPIOState() {
            if (gpioStatePtr === null) {
                try {
                    gpioStatePtr = Module.ccall('get_gpio_state_ptr', 'number', [], []);
                } catch (e) {
                    console.warn('GPIO state not available:', e);
                }
            }
        }
        // gpio_pin_state_t is stored in common-hal/digitalio/DigitalInOut.c
        // Struct size may be padded - use 8 bytes per pin to be safe
        function getGPIOPinView(pin) {
            if (gpioStatePtr === null || pin >= 64) return null;
            return new DataView(Module.HEAPU8.buffer, gpioStatePtr + pin * 8, 8);
        }
    `,

    mp_js_gpio_get_value: (pin) => {
        if (gpioStatePtr === null) initGPIOState();
        const view = getGPIOPinView(pin);
        return view ? view.getUint8(0) : 0;  // Read 'value' field
    },

    mp_js_gpio_set_input_value: (pin, value) => {
        if (gpioStatePtr === null) initGPIOState();
        const view = getGPIOPinView(pin);
        if (view && view.getUint8(1) === 0) {  // Only if direction == input
            view.setUint8(0, value ? 1 : 0);
        }
    },

    mp_js_gpio_get_direction: (pin) => {
        if (gpioStatePtr === null) initGPIOState();
        const view = getGPIOPinView(pin);
        return view ? view.getUint8(1) : 0;  // Read 'direction' field
    },
});
