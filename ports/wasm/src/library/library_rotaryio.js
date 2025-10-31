/**
 * CircuitPython WASM Port - Rotary Encoder JavaScript Library
 *
 * This library provides the JavaScript interface for simulating rotary encoders
 * in the CircuitPython WASM port. It enables bidirectional communication between
 * JavaScript and the C implementation for rotary encoder state.
 *
 * ARCHITECTURE:
 * - C exports encoder state arrays via get_rotaryio_state_ptr()
 * - JavaScript accesses state arrays to read configuration and position
 * - JavaScript simulates encoder rotation and calls rotaryio_update_encoder()
 * - C performs quadrature decoding using shared-module logic
 *
 * USAGE EXAMPLE:
 * ```javascript
 * const Rotaryio = Rotaryio_Library(Module);
 *
 * // Access state arrays
 * const encoders = Rotaryio.getEncoders();
 *
 * // Simulate encoder rotation (clockwise)
 * Rotaryio.simulateRotation(0, 1);  // encoder 0, 1 step CW
 *
 * // Read current position
 * const position = Rotaryio.getPosition(0);
 * ```
 *
 * @param {Object} Module - Emscripten Module object
 * @returns {Object} Rotaryio interface
 */
const Rotaryio_Library = (Module) => {
    'use strict';

    // ==========================================================================
    // STATE STRUCTURE (must match encoder_state_t in IncrementalEncoder.c)
    // ==========================================================================
    const ENCODER_STATE_STRUCT_SIZE = 20;  // Size in bytes
    const OFFSET_PIN_A = 0;
    const OFFSET_PIN_B = 1;
    const OFFSET_DIVISOR = 2;
    const OFFSET_POSITION = 4;
    const OFFSET_STATE = 8;
    const OFFSET_SUB_COUNT = 9;
    const OFFSET_ENABLED = 10;
    const OFFSET_NEVER_RESET = 11;

    const MAX_ENCODERS = 4;

    // State array pointer (initialized lazily)
    let stateArrayPtr = null;
    let stateView = null;

    /**
     * Initialize state array access
     */
    function initStateArray() {
        if (stateArrayPtr !== null) {
            return;
        }

        try {
            stateArrayPtr = Module._get_rotaryio_state_ptr();
            if (!stateArrayPtr) {
                console.error('[Rotaryio] Failed to get state array pointer');
                return;
            }

            // Create view for efficient access
            stateView = new DataView(Module.HEAPU8.buffer, stateArrayPtr,
                ENCODER_STATE_STRUCT_SIZE * MAX_ENCODERS);
        } catch (e) {
            console.error('[Rotaryio] Failed to initialize state array:', e);
        }
    }

    /**
     * Get encoder state by index
     * @param {number} index - Encoder index (0-3)
     * @returns {Object|null} Encoder state object or null if invalid
     */
    function getEncoderState(index) {
        initStateArray();
        if (!stateView || index < 0 || index >= MAX_ENCODERS) {
            return null;
        }

        const offset = index * ENCODER_STATE_STRUCT_SIZE;
        const enabled = stateView.getUint8(offset + OFFSET_ENABLED);

        if (!enabled) {
            return null;
        }

        return {
            pin_a: stateView.getUint8(offset + OFFSET_PIN_A),
            pin_b: stateView.getUint8(offset + OFFSET_PIN_B),
            divisor: stateView.getInt8(offset + OFFSET_DIVISOR),
            position: stateView.getInt32(offset + OFFSET_POSITION, true),
            state: stateView.getUint8(offset + OFFSET_STATE),
            sub_count: stateView.getInt8(offset + OFFSET_SUB_COUNT),
            enabled: enabled !== 0,
            never_reset: stateView.getUint8(offset + OFFSET_NEVER_RESET) !== 0,
            index: index
        };
    }

    /**
     * Get all active encoders
     * @returns {Array<Object>} Array of active encoder states
     */
    function getAllEncoders() {
        const encoders = [];
        for (let i = 0; i < MAX_ENCODERS; i++) {
            const encoder = getEncoderState(i);
            if (encoder) {
                encoders.push(encoder);
            }
        }
        return encoders;
    }

    /**
     * Get encoder position
     * @param {number} index - Encoder index
     * @returns {number|null} Current position or null if invalid
     */
    function getPosition(index) {
        const encoder = getEncoderState(index);
        return encoder ? encoder.position : null;
    }

    /**
     * Set encoder position (directly modifies state array)
     * @param {number} index - Encoder index
     * @param {number} position - New position
     */
    function setPosition(index, position) {
        initStateArray();
        if (!stateView || index < 0 || index >= MAX_ENCODERS) {
            return;
        }

        const offset = index * ENCODER_STATE_STRUCT_SIZE;
        const enabled = stateView.getUint8(offset + OFFSET_ENABLED);

        if (enabled) {
            stateView.setInt32(offset + OFFSET_POSITION, position, true);
        }
    }

    /**
     * Simulate encoder rotation by updating pin states
     * This function simulates the quadrature signals from a rotary encoder
     *
     * @param {number} index - Encoder index
     * @param {number} steps - Number of steps (positive = CW, negative = CCW)
     */
    function simulateRotation(index, steps) {
        initStateArray();
        if (!stateView || index < 0 || index >= MAX_ENCODERS) {
            return;
        }

        const offset = index * ENCODER_STATE_STRUCT_SIZE;
        const enabled = stateView.getUint8(offset + OFFSET_ENABLED);

        if (!enabled) {
            return;
        }

        // Current state (2 bits: A | B)
        let currentState = stateView.getUint8(offset + OFFSET_STATE);

        // Quadrature sequence for clockwise rotation: 00 -> 10 -> 11 -> 01 -> 00
        // For counter-clockwise, reverse the sequence
        const cwSequence = [0b00, 0b10, 0b11, 0b01];
        const ccwSequence = [0b00, 0b01, 0b11, 0b10];

        const sequence = steps > 0 ? cwSequence : ccwSequence;
        const numSteps = Math.abs(steps);

        // Find current position in sequence
        let seqIndex = sequence.indexOf(currentState);
        if (seqIndex === -1) {
            seqIndex = 0;  // Default to start if current state is invalid
        }

        // Generate transitions
        for (let i = 0; i < numSteps * 4; i++) {  // 4 transitions per step
            seqIndex = (seqIndex + 1) % 4;
            const newState = sequence[seqIndex];
            const pin_a = (newState >> 1) & 1;
            const pin_b = newState & 1;

            // Call C update function
            Module._rotaryio_update_encoder(index, pin_a, pin_b);
        }
    }

    /**
     * Manually update encoder with specific pin states
     * Use this for custom encoder simulation logic
     *
     * @param {number} index - Encoder index
     * @param {number} pinAState - Pin A state (0 or 1)
     * @param {number} pinBState - Pin B state (0 or 1)
     */
    function updateEncoder(index, pinAState, pinBState) {
        if (index < 0 || index >= MAX_ENCODERS) {
            return;
        }

        Module._rotaryio_update_encoder(index, pinAState & 1, pinBState & 1);
    }

    /**
     * Debug: Print all encoder states
     */
    function debugPrintEncoders() {
        console.log('[Rotaryio] Active encoders:');
        const encoders = getAllEncoders();
        if (encoders.length === 0) {
            console.log('  (none)');
            return;
        }

        encoders.forEach(enc => {
            console.log(`  Encoder ${enc.index}:`);
            console.log(`    Pins: A=${enc.pin_a}, B=${enc.pin_b}`);
            console.log(`    Position: ${enc.position}`);
            console.log(`    Divisor: ${enc.divisor}`);
            console.log(`    State: 0b${enc.state.toString(2).padStart(2, '0')}`);
            console.log(`    Sub-count: ${enc.sub_count}`);
            console.log(`    Never reset: ${enc.never_reset}`);
        });
    }

    // ==========================================================================
    // PUBLIC API
    // ==========================================================================
    return {
        getEncoderState: getEncoderState,
        getAllEncoders: getAllEncoders,
        getPosition: getPosition,
        setPosition: setPosition,
        simulateRotation: simulateRotation,
        updateEncoder: updateEncoder,
        debugPrintEncoders: debugPrintEncoders,

        // Constants
        MAX_ENCODERS: MAX_ENCODERS
    };
};

// Export for Emscripten
mergeInto(LibraryManager.library, {
    // Rotaryio library is exposed via Rotaryio_Library function
    // Usage: const Rotaryio = Rotaryio_Library(Module);
});

// Make available globally for testing/debugging
if (typeof globalThis !== 'undefined') {
    globalThis.Rotaryio_Library = Rotaryio_Library;
}
