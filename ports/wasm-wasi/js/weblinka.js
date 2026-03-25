/**
 * weBlinka — Hardware abstraction router for CircuitPython in the browser.
 *
 * Receives U2IF-compatible binary commands from the reactor's Python shims
 * (via WASI file write interception) and routes them to one or more targets:
 *
 *   - WorkerTarget:    postMessage to a Web Worker running CircuitPython VM
 *   - WebUSBTarget:    USB HID reports to a U2IF-firmware board (future)
 *   - WebSerialTarget: Raw REPL commands to a CircuitPython board (future)
 *
 * The shims write 64-byte command packets to /hw/cmd via WASI fd_write.
 * WasiMemfs intercepts writes to /hw/cmd and calls weblinka.send(data).
 *
 * Command format (U2IF-compatible):
 *   Byte 0:     0x00 (report ID, always zero)
 *   Byte 1:     command opcode
 *   Byte 2+:    parameters (command-specific)
 *   Remaining:  zero-padded to 64 bytes
 *
 * Response format:
 *   Byte 0:     0x00 (report ID)
 *   Byte 1:     status (0x01=OK, 0x02=NOK)
 *   Byte 2+:    response data (command-specific)
 *   Remaining:  zero-padded to 64 bytes
 */

// ---- U2IF Opcodes ----
export const CMD = {
    // GPIO
    GPIO_INIT:          0x20,
    GPIO_SET_VALUE:     0x21,
    GPIO_GET_VALUE:     0x22,

    // PWM
    PWM_INIT:           0x30,
    PWM_DEINIT:         0x31,
    PWM_SET_FREQ:       0x32,
    PWM_GET_FREQ:       0x33,
    PWM_SET_DUTY_U16:   0x34,
    PWM_GET_DUTY_U16:   0x35,

    // ADC
    ADC_INIT:           0x40,
    ADC_GET_VALUE:      0x41,

    // I2C
    I2C_INIT:           0x80,
    I2C_DEINIT:         0x81,
    I2C_WRITE:          0x82,
    I2C_READ:           0x83,

    // SPI
    SPI_INIT:           0x60,
    SPI_DEINIT:         0x61,
    SPI_WRITE:          0x62,
    SPI_READ:           0x63,

    // NeoPixel (WS2812B)
    WS2812B_INIT:       0xA0,
    WS2812B_DEINIT:     0xA1,
    WS2812B_WRITE:      0xA2,
};

export const STATUS_OK  = 0x01;
export const STATUS_NOK = 0x02;
export const REPORT_SIZE = 64;

// ---- Target interface ----

/**
 * A target receives U2IF commands and returns responses.
 * All targets implement send(command: Uint8Array) → Uint8Array|null
 */

/**
 * WorkerTarget — routes commands to a Web Worker via postMessage.
 * The worker's JS receives { type: 'u2if', data: ArrayBuffer }
 * and dispatches to its common-hal or simulated hardware.
 */
export class WorkerTarget {
    constructor(worker) {
        this.worker = worker;
        this._pending = null;
        this._resolve = null;

        // Listen for responses from the worker
        this.worker.addEventListener('message', (e) => {
            if (e.data && e.data.type === 'u2if_response') {
                if (this._resolve) {
                    this._resolve(new Uint8Array(e.data.data));
                    this._resolve = null;
                }
            }
        });
    }

    /**
     * Send a U2IF command to the worker.
     * For write commands (GPIO_SET_VALUE, etc.), fire-and-forget.
     * For read commands (GPIO_GET_VALUE, ADC_GET_VALUE), returns a response.
     */
    send(cmd) {
        const copy = new Uint8Array(cmd);
        this.worker.postMessage(
            { type: 'u2if', data: copy.buffer },
            [copy.buffer]
        );
        // Fire-and-forget for now — reads will need async
        return null;
    }
}

// Future targets:
//
// export class WebUSBTarget {
//     constructor(device) { this.device = device; }
//     async send(cmd) {
//         await this.device.transferOut(endpoint, cmd);
//         const result = await this.device.transferIn(endpoint, 64);
//         return new Uint8Array(result.data.buffer);
//     }
// }
//
// export class WebSerialTarget {
//     constructor(port) { this.port = port; }
//     send(cmd) {
//         // Translate U2IF binary → raw REPL text
//         const text = this._translate(cmd);
//         this.writer.write(new TextEncoder().encode(text));
//     }
// }

// ---- weBlinka Router ----

export class WeBlinka {
    constructor() {
        this.targets = [];       // Array of targets to route to
        this._localState = {};   // Local pin state cache (for reads without round-trip)
    }

    /**
     * Add a routing target. Commands will be sent to all targets (tee).
     */
    addTarget(target) {
        this.targets.push(target);
    }

    /**
     * Remove a target.
     */
    removeTarget(target) {
        this.targets = this.targets.filter(t => t !== target);
    }

    /**
     * Process a raw command buffer from the shims.
     * The shims write 64-byte U2IF packets to /hw/cmd.
     * This method is called by WasiMemfs on each write.
     */
    send(data) {
        if (data.length < 2) return null;

        const cmd = data[1];  // Byte 0 is report ID (0x00), byte 1 is opcode

        // Update local state cache for reads
        this._updateLocalState(cmd, data);

        // Route to all targets
        for (const target of this.targets) {
            target.send(data);
        }

        // For read commands, return from local state cache
        return this._handleRead(cmd, data);
    }

    /**
     * Process multiple commands from a single write.
     * The shims may batch commands in a single fd_write.
     */
    sendBatch(data) {
        const results = [];
        for (let i = 0; i + REPORT_SIZE <= data.length; i += REPORT_SIZE) {
            const packet = data.slice(i, i + REPORT_SIZE);
            const result = this.send(packet);
            if (result) results.push(result);
        }
        return results;
    }

    // ---- Internal state management ----

    _updateLocalState(cmd, data) {
        switch (cmd) {
            case CMD.GPIO_INIT: {
                const pin = data[2];
                const mode = data[3];  // 0=IN, 1=OUT
                const pull = data[4];  // 0=none, 1=up, 2=down
                if (!this._localState[pin]) this._localState[pin] = {};
                this._localState[pin].mode = mode;
                this._localState[pin].pull = pull;
                this._localState[pin].value = 0;
                break;
            }
            case CMD.GPIO_SET_VALUE: {
                const pin = data[2];
                const val = data[3];
                if (!this._localState[pin]) this._localState[pin] = {};
                this._localState[pin].value = val;
                break;
            }
            case CMD.PWM_SET_DUTY_U16: {
                const pin = data[2];
                const duty = data[3] | (data[4] << 8);
                if (!this._localState[pin]) this._localState[pin] = {};
                this._localState[pin].duty = duty;
                break;
            }
            case CMD.PWM_SET_FREQ: {
                const pin = data[2];
                const freq = data[3] | (data[4] << 8) | (data[5] << 16) | (data[6] << 24);
                if (!this._localState[pin]) this._localState[pin] = {};
                this._localState[pin].freq = freq;
                break;
            }
        }
    }

    _handleRead(cmd, data) {
        const response = new Uint8Array(REPORT_SIZE);
        response[0] = 0x00;  // report ID
        response[1] = STATUS_OK;

        switch (cmd) {
            case CMD.GPIO_GET_VALUE: {
                const pin = data[2];
                const state = this._localState[pin];
                response[2] = state ? state.value : 0;
                return response;
            }
            case CMD.ADC_GET_VALUE: {
                const pin = data[2];
                const state = this._localState[pin];
                const val = state ? (state.adcValue || 0) : 0;
                // ADC value at response[3:5], 16-bit LE
                response[3] = val & 0xFF;
                response[4] = (val >> 8) & 0xFF;
                return response;
            }
            case CMD.PWM_GET_FREQ: {
                const pin = data[2];
                const state = this._localState[pin];
                const freq = state ? (state.freq || 500) : 500;
                response[2] = freq & 0xFF;
                response[3] = (freq >> 8) & 0xFF;
                response[4] = (freq >> 16) & 0xFF;
                response[5] = (freq >> 24) & 0xFF;
                return response;
            }
            case CMD.PWM_GET_DUTY_U16: {
                const pin = data[2];
                const state = this._localState[pin];
                const duty = state ? (state.duty || 0) : 0;
                response[2] = duty & 0xFF;
                response[3] = (duty >> 8) & 0xFF;
                return response;
            }
            default:
                return null;  // Write-only command, no response needed
        }
    }

    /**
     * Inject a hardware event from the outside (e.g., button press,
     * sensor reading from a real board). Updates local state so the
     * reactor's shim reads get the new value.
     */
    injectEvent(pin, key, value) {
        if (!this._localState[pin]) this._localState[pin] = {};
        this._localState[pin][key] = value;
    }
}
