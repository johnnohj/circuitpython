// JavaScript library for CircuitPython WASM - BusIO Module
// Provides access to I2C/SPI/UART bus state for device simulation

mergeInto(LibraryManager.library, {
    // Placeholder function for initialization
    mp_js_busio: () => {},

    mp_js_busio__postset: `
        // I2C bus state structure (simplified access)
        // Each bus contains:
        //   - scl_pin, sda_pin (uint8_t each)
        //   - frequency (uint32_t)
        //   - enabled, locked (bool)
        //   - devices[128] - array of 128 device states
        //     - Each device: registers[256] + active (bool)
        //   - last transaction buffers

        var i2cStatePtr = null;
        var MAX_I2C_BUSES = 8;

        function initI2CState() {
            if (i2cStatePtr === null) {
                i2cStatePtr = Module.ccall('get_i2c_state_ptr', 'number', [], []);
            }
        }

        // Export function to enable/configure an I2C device from JavaScript
        // This allows JS to simulate I2C devices
        Module.setI2CDevice = function(busIndex, deviceAddr, registers) {
            initI2CState();
            if (busIndex >= MAX_I2C_BUSES || deviceAddr >= 128) {
                return false;
            }

            // Calculate offsets (approximate - actual struct may need padding)
            // Simplified: just set device active flag and registers
            var busOffset = busIndex * 35000;  // Approximate bus state size
            var deviceOffset = busOffset + 20 + (deviceAddr * 257);  // Skip bus metadata

            // Mark device as active
            Module.HEAPU8[i2cStatePtr + deviceOffset + 256] = 1;

            // Set register values if provided
            if (registers && registers.length > 0) {
                for (var i = 0; i < Math.min(registers.length, 256); i++) {
                    Module.HEAPU8[i2cStatePtr + deviceOffset + i] = registers[i] & 0xFF;
                }
            }

            return true;
        };

        // Export function to read I2C device registers from JavaScript
        Module.getI2CDeviceRegisters = function(busIndex, deviceAddr) {
            initI2CState();
            if (busIndex >= MAX_I2C_BUSES || deviceAddr >= 128) {
                return null;
            }

            var busOffset = busIndex * 35000;
            var deviceOffset = busOffset + 20 + (deviceAddr * 257);

            // Check if device is active
            if (!Module.HEAPU8[i2cStatePtr + deviceOffset + 256]) {
                return null;
            }

            // Read all 256 registers
            var registers = new Uint8Array(256);
            for (var i = 0; i < 256; i++) {
                registers[i] = Module.HEAPU8[i2cStatePtr + deviceOffset + i];
            }

            return registers;
        };

        // UART port state structure
        // Each port contains:
        //   - tx_pin, rx_pin (uint8_t each)
        //   - baudrate (uint32_t)
        //   - bits, parity, stop (uint8_t)
        //   - enabled (bool)
        //   - timeout (float)
        //   - rx_buffer[512], tx_buffer[512]
        //   - rx_head, rx_tail, tx_head, tx_tail (uint16_t)

        var uartStatePtr = null;
        var MAX_UART_PORTS = 8;
        var UART_BUFFER_SIZE = 512;

        function initUARTState() {
            if (uartStatePtr === null) {
                uartStatePtr = Module.ccall('get_uart_state_ptr', 'number', [], []);
            }
        }

        // Export function to write data to UART RX buffer (simulates data received)
        Module.writeUARTRx = function(portIndex, data) {
            initUARTState();
            if (portIndex >= MAX_UART_PORTS) {
                return 0;
            }

            // Approximate struct offsets
            var portOffset = portIndex * 1100;  // Approximate port state size
            var rxBufferOffset = portOffset + 20;  // Skip metadata
            var rxHeadOffset = portOffset + 20 + 512;  // After rx_buffer
            var rxTailOffset = rxHeadOffset + 2;

            var view = new DataView(Module.HEAPU8.buffer, uartStatePtr + portOffset);
            var rxHead = view.getUint16(rxHeadOffset - portOffset, true);
            var rxTail = view.getUint16(rxTailOffset - portOffset, true);

            var written = 0;
            for (var i = 0; i < data.length; i++) {
                var nextHead = (rxHead + 1) % UART_BUFFER_SIZE;
                if (nextHead === rxTail) {
                    break;  // Buffer full
                }
                Module.HEAPU8[uartStatePtr + rxBufferOffset + rxHead] = data[i];
                rxHead = nextHead;
                written++;
            }

            view.setUint16(rxHeadOffset - portOffset, rxHead, true);
            return written;
        };

        // Export function to read data from UART TX buffer
        Module.readUARTTx = function(portIndex, maxLen) {
            initUARTState();
            if (portIndex >= MAX_UART_PORTS) {
                return new Uint8Array(0);
            }

            var portOffset = portIndex * 1100;
            var txBufferOffset = portOffset + 20 + 512 + 4;  // After rx_buffer and rx head/tail
            var txHeadOffset = portOffset + 20 + 512 + 512 + 4;  // After both buffers
            var txTailOffset = txHeadOffset + 2;

            var view = new DataView(Module.HEAPU8.buffer, uartStatePtr + portOffset);
            var txHead = view.getUint16(txHeadOffset - portOffset, true);
            var txTail = view.getUint16(txTailOffset - portOffset, true);

            var available = (txHead >= txTail) ? (txHead - txTail) : (UART_BUFFER_SIZE - txTail + txHead);
            var toRead = Math.min(available, maxLen || available);

            var data = new Uint8Array(toRead);
            for (var i = 0; i < toRead; i++) {
                data[i] = Module.HEAPU8[uartStatePtr + txBufferOffset + txTail];
                txTail = (txTail + 1) % UART_BUFFER_SIZE;
            }

            view.setUint16(txTailOffset - portOffset, txTail, true);
            return data;
        };

        // Export function to get UART port info
        Module.getUARTInfo = function(portIndex) {
            initUARTState();
            if (portIndex >= MAX_UART_PORTS) {
                return null;
            }

            var portOffset = portIndex * 1100;
            var view = new DataView(Module.HEAPU8.buffer, uartStatePtr + portOffset);

            return {
                txPin: view.getUint8(0),
                rxPin: view.getUint8(1),
                baudrate: view.getUint32(2, true),
                bits: view.getUint8(6),
                parity: view.getUint8(7),
                stop: view.getUint8(8),
                enabled: view.getUint8(9) !== 0
            };
        };
    `,
});
