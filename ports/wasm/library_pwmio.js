// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// ==============================================================================
// PWMIO Functions - PWM State Access
// ==============================================================================
// State managed in common-hal/pwmio/PWMOut.c
// JavaScript reads pwm_state[] array for oscilloscope/LED visualization

mergeInto(LibraryManager.library, {
    mp_js_pwm: () => {},  // Placeholder for postset initialization

    mp_js_pwm__postset: `
        var pwmStatePtr = null;
        function initPWMState() {
            if (pwmStatePtr === null) {
                try {
                    pwmStatePtr = Module.ccall('get_pwm_state_ptr', 'number', [], []);
                } catch (e) {
                    console.warn('PWM state not available:', e);
                }
            }
        }
        // pwm_state_t: uint16 duty_cycle (2) + uint32 frequency (4) + bool variable_freq (1) + bool enabled (1) = 8 bytes
        function getPWMChannelView(channel) {
            if (pwmStatePtr === null || channel >= 64) return null;
            return new DataView(Module.HEAPU8.buffer, pwmStatePtr + channel * 8, 8);
        }
    `,

    mp_js_pwm_get_duty_cycle: (channel) => {
        if (pwmStatePtr === null) initPWMState();
        const view = getPWMChannelView(channel);
        return view ? view.getUint16(0, true) : 0;  // Read 'duty_cycle' field
    },

    mp_js_pwm_get_frequency: (channel) => {
        if (pwmStatePtr === null) initPWMState();
        const view = getPWMChannelView(channel);
        return view ? view.getUint32(2, true) : 0;  // Read 'frequency' field
    },

    mp_js_pwm_is_enabled: (channel) => {
        if (pwmStatePtr === null) initPWMState();
        const view = getPWMChannelView(channel);
        return view ? view.getUint8(7) : 0;  // Read 'enabled' field
    },
});
