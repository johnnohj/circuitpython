/*
 * SensorSimulator.js — Catalog-driven I2C/SPI sensor simulation
 *
 * Uses sensor_catalog.json to simulate hardware sensors without writing
 * per-sensor JS code.  Each catalog entry describes:
 *   - I2C address + chip ID register
 *   - Measurement registers + data format
 *   - Value encoding (formula to convert real-world values to raw register bytes)
 *
 * Special sensor_type handling:
 *   - "bme280": calibration-based compensation (reads calibration registers,
 *               computes raw ADC values using inverse BME280 algorithm)
 *   - "vl53l1x": write hooks for MODE_START → data_ready + range_status
 *   - "sht31d": enhanced command protocol (status, serial number)
 *
 * Usage:
 *   import { SensorSimulator } from './SensorSimulator.js';
 *   import catalog from './sensor_catalog.json' with { type: 'json' };
 *
 *   const sim = new SensorSimulator(catalog);
 *   sim.addSensor(hw, 'bme280', { temperature: 25, humidity: 60 });
 *   sim.setValue('bme280_119', 'temperature', 30);
 *
 * The simulator registers I2C device handlers on the HardwareSimulator.
 * When Python reads registers via busio.I2C, the handler returns encoded
 * values from the catalog's value_encoding formulas.
 */

// Node.js compat: import assertions vary by version, so accept catalog as constructor arg

export class SensorSimulator {
    /**
     * @param {Object} catalog  Parsed sensor_catalog.json
     */
    constructor(catalog) {
        this._catalog = catalog.sensors || catalog;
        this._instances = {};   // id → { spec, values, regFile }
    }

    /**
     * Add a simulated sensor to a HardwareSimulator.
     *
     * @param {Object} hw             HardwareSimulator instance
     * @param {string} sensorType     Key in catalog (e.g. 'bme280')
     * @param {Object} [initialValues]  e.g. { temperature: 25, humidity: 60 }
     * @param {Object} [opts]
     * @param {number} [opts.address]   Override I2C address
     * @param {string} [opts.busId]     I2C bus id (default: auto-detect from first i2c_init event)
     * @returns {string}  Instance ID (e.g. 'bme280_119')
     */
    addSensor(hw, sensorType, initialValues = {}, opts = {}) {
        const spec = this._catalog[sensorType];
        if (!spec) {
            throw new Error(`Unknown sensor type: ${sensorType}. Available: ${Object.keys(this._catalog).join(', ')}`);
        }

        const address = opts.address ?? spec.addresses[0];
        const id = `${sensorType}_${address}`;

        // Merge defaults from capabilities with provided values
        const values = {};
        for (const [cap, info] of Object.entries(spec.capabilities)) {
            values[cap] = initialValues[cap] ?? info.default;
        }

        // Build register file — size based on max register address
        const maxAddr = this._maxRegAddr(spec);
        const regFileSize = Math.max(512, maxAddr + 32);
        const regFile = new Uint8Array(regFileSize);
        this._fillChipId(spec, regFile);
        this._fillDefaults(spec, regFile);

        // Sensor-type specific initialization
        if (spec.sensor_type === 'bme280') {
            this._fillBME280Calibration(spec, regFile);
        }

        this._instances[id] = { spec, values, regFile, address, selectedReg: 0 };

        // Encode initial values into register file
        this._encodeAllValues(id);

        // Register I2C device handler
        const handler = (cmd, ev) => this._handleTransaction(id, cmd, ev);
        hw.addI2CDevice(opts.busId ?? null, address, handler);

        // If no busId specified, register on all future buses too
        if (!opts.busId) {
            // Store handler for lazy registration when i2c_init fires
            if (!hw._sensorHandlers) hw._sensorHandlers = [];
            hw._sensorHandlers.push({ address, handler });
        }

        return id;
    }

    /**
     * Update a simulated sensor value at runtime.
     * @param {string} id           Instance ID from addSensor()
     * @param {string} capability   e.g. 'temperature'
     * @param {*} value             New value (number or array for multi-axis)
     */
    setValue(id, capability, value) {
        const inst = this._instances[id];
        if (!inst) throw new Error(`Unknown sensor instance: ${id}`);
        inst.values[capability] = value;
        this._encodeCapability(id, capability);
    }

    /**
     * Get current simulated value.
     */
    getValue(id, capability) {
        return this._instances[id]?.values[capability];
    }

    /**
     * List all sensor instances.
     */
    get instances() {
        const result = {};
        for (const [id, inst] of Object.entries(this._instances)) {
            result[id] = {
                type: inst.spec.name,
                address: inst.address,
                values: { ...inst.values },
                capabilities: Object.keys(inst.spec.capabilities),
            };
        }
        return result;
    }

    // ── Internal ──────────────────────────────────────────────────────────────

    _fillChipId(spec, regFile) {
        if (spec.chip_id) {
            regFile[spec.chip_id.register] = spec.chip_id.value;
        }
    }

    _fillDefaults(spec, regFile) {
        if (!spec.registers) return;
        for (const [, info] of Object.entries(spec.registers)) {
            if (info.default !== undefined) {
                regFile[info.addr] = info.default;
            }
        }
    }

    _encodeAllValues(id) {
        const inst = this._instances[id];
        if (inst.spec.sensor_type === 'bme280') {
            this._bme280EncodeAll(inst);
            return;
        }
        for (const cap of Object.keys(inst.spec.capabilities)) {
            this._encodeCapability(id, cap);
        }
    }

    _encodeCapability(id, capability) {
        const inst = this._instances[id];
        const spec = inst.spec;

        // BME280 uses calibration-based encoding for all capabilities at once
        if (spec.sensor_type === 'bme280') {
            this._bme280EncodeAll(inst);
            return;
        }

        const value = inst.values[capability];
        const encoding = spec.value_encoding?.[capability];
        if (!encoding) return;

        // Find the register(s) for this capability
        let regInfo = null;
        if (spec.registers) {
            for (const [, info] of Object.entries(spec.registers)) {
                if (info.capability === capability) {
                    regInfo = info;
                    break;
                }
            }
        }
        if (!regInfo) return;

        const format = regInfo.format || 'uint16_be';
        const raw = this._valueToRaw(value, encoding, format);
        this._writeRegBytes(inst.regFile, regInfo.addr, regInfo.size, format, raw);
    }

    _valueToRaw(value, encoding, format) {
        // Apply inverse formula to convert real-world value to raw register value
        // For multi-axis (acceleration), value is an array
        if (Array.isArray(value)) {
            return value.map(v => this._evalInverse(v, encoding.inverse));
        }
        return this._evalInverse(value, encoding.inverse);
    }

    _evalInverse(value, inverseFormula) {
        // Simple formula evaluator for catalog inverse formulas
        // Supports: "value * K", "(value + K) * K", "value / K * K", "value"
        if (!inverseFormula || inverseFormula === 'value') return Math.round(value);
        try {
            // Safe-ish eval: only 'value' variable, basic arithmetic
            const fn = new Function('value', 'return ' + inverseFormula);
            return Math.round(fn(value));
        } catch {
            return Math.round(value);
        }
    }

    _writeRegBytes(regFile, addr, size, format, raw) {
        if (format === 'uint16_be') {
            const v = typeof raw === 'number' ? Math.max(0, Math.min(65535, raw)) : 0;
            regFile[addr]     = (v >> 8) & 0xFF;
            regFile[addr + 1] = v & 0xFF;
        } else if (format === 'uint20_be') {
            const v = typeof raw === 'number' ? Math.max(0, Math.min(0xFFFFF, raw)) : 0;
            regFile[addr]     = (v >> 12) & 0xFF;
            regFile[addr + 1] = (v >> 4) & 0xFF;
            regFile[addr + 2] = (v << 4) & 0xF0;
        } else if (format === 'int16_le_x3' && Array.isArray(raw)) {
            // 3 × int16 little-endian (accelerometer x,y,z)
            for (let i = 0; i < 3; i++) {
                const v = Math.max(-32768, Math.min(32767, raw[i] ?? 0));
                const u = v < 0 ? v + 65536 : v;
                regFile[addr + i * 2]     = u & 0xFF;
                regFile[addr + i * 2 + 1] = (u >> 8) & 0xFF;
            }
        } else {
            // Fallback: write raw as single byte
            regFile[addr] = typeof raw === 'number' ? raw & 0xFF : 0;
        }
    }

    _handleTransaction(id, cmd, ev) {
        const inst = this._instances[id];
        if (!inst) return null;
        const spec = inst.spec;

        if (spec.protocol === 'command') {
            return this._handleCommand(inst, cmd, ev);
        }
        return this._handleRegister(inst, cmd, ev);
    }

    _parseRegAddr(inst, data) {
        // Determine register address from write data.
        // 16-bit addresses: if max register addr > 255 (VL53L1X), combine 2 bytes.
        // Auto-increment bit: bit 7 (0x80) is used by some sensors (LIS3DH) to
        // signal multi-byte reads; mask it off for the address lookup.
        if (!data || data.length === 0) return inst.selectedReg;
        const max = this._maxRegAddr(inst.spec);
        if (max > 255 && data.length >= 2) {
            return (data[0] << 8) | data[1];
        }
        // Auto-increment bit (0x80) only applies to sensors whose register
        // addresses fit in 7 bits (e.g. LIS3DH: max reg ~0x3F).
        // Sensors with registers > 0x7F (e.g. BME280: 0xD0) use the full byte.
        if (max <= 0x7F) {
            return data[0] & 0x7F;
        }
        return data[0];
    }

    _maxRegAddr(spec) {
        if (spec._maxAddr !== undefined) return spec._maxAddr;
        let max = 0;
        if (spec.registers) {
            for (const info of Object.values(spec.registers)) {
                if (info.addr > max) max = info.addr;
            }
        }
        spec._maxAddr = max;
        return max;
    }

    _handleRegister(inst, cmd, ev) {
        if (cmd === 'i2c_write' && ev.data?.length > 0) {
            inst.selectedReg = this._parseRegAddr(inst, ev.data);
            // Remaining bytes after the address are data to write
            const addrLen = this._maxRegAddr(inst.spec) > 255 ? 2 : 1;
            if (ev.data.length > addrLen) {
                for (let i = addrLen; i < ev.data.length; i++) {
                    inst.regFile[inst.selectedReg + i - addrLen] = ev.data[i];
                }
                // Post-write hooks
                this._postWriteHook(inst, inst.selectedReg, ev.data.length - addrLen);
            }
            return null;
        }

        if (cmd === 'i2c_read') {
            const n = ev.len ?? 1;
            const result = new Uint8Array(n);
            for (let i = 0; i < n; i++) {
                result[i] = inst.regFile[inst.selectedReg + i];
            }
            return result;
        }

        if (cmd === 'i2c_write_read') {
            if (ev.data?.length > 0) {
                inst.selectedReg = this._parseRegAddr(inst, ev.data);
            }
            const n = ev.read_len ?? 1;
            const result = new Uint8Array(n);
            for (let i = 0; i < n; i++) {
                result[i] = inst.regFile[inst.selectedReg + i];
            }
            return result;
        }

        if (cmd === 'i2c_scan') {
            return true;  // device present
        }

        return null;
    }

    // ── Post-write hooks ─────────────────────────────────────────────────────

    _postWriteHook(inst, regAddr, dataLen) {
        if (inst.spec.sensor_type === 'vl53l1x') {
            this._vl53l1xPostWrite(inst, regAddr, dataLen);
        }
    }

    _vl53l1xPostWrite(inst, regAddr, dataLen) {
        // MODE_START register (0x0087 = 135)
        // When written with 0x40, simulate instant measurement completion
        if (regAddr === 0x0087 || (regAddr <= 0x0087 && regAddr + dataLen > 0x0087)) {
            const modeStart = inst.regFile[0x0087];
            if (modeStart === 0x40) {
                // Data ready: set GPIO_TIO_HV_STATUS bit 0
                // The interrupt polarity logic in the driver:
                //   int_pol = (reg[0x30] & 0x10) >> 4 → inverted → _interrupt_polarity
                //   data_ready = (reg[0x31] & 0x01) == _interrupt_polarity
                // After init seq, reg[0x30] gets written by the driver's 88-byte blob.
                // The blob sets 0x30 = 0x01 (bit4=0), so int_pol=0, _interrupt_polarity=1.
                // Therefore data_ready needs bit 0 of 0x31 = 1.
                inst.regFile[0x0031] |= 0x01;

                // Valid range status at 0x0089
                inst.regFile[0x0089] = 0x09;
            }
        }
    }

    // ── Command protocol (SHT31D) ────────────────────────────────────────────

    _handleCommand(inst, cmd, ev) {
        const spec = inst.spec;

        if (cmd === 'i2c_write' && ev.data?.length >= 2) {
            // Command-based protocol: 2-byte command
            const cmdBytes = ev.data.slice(0, 2);
            inst._lastCmd = cmdBytes;
            return null;
        }

        if (cmd === 'i2c_read') {
            // If we have a pending command, find matching response
            if (inst._lastCmd) {
                for (const [, cmdSpec] of Object.entries(spec.commands)) {
                    if (cmdSpec.cmd &&
                        cmdSpec.cmd[0] === inst._lastCmd[0] &&
                        cmdSpec.cmd[1] === inst._lastCmd[1]) {
                        const result = this._encodeCommandResponse(inst, cmdSpec);
                        inst._lastCmd = null;
                        return result;
                    }
                }
                inst._lastCmd = null;
            }
            return new Uint8Array(ev.len ?? 1);
        }

        if (cmd === 'i2c_scan') {
            return true;
        }

        return null;
    }

    _encodeCommandResponse(inst, cmdSpec) {
        if (cmdSpec.response_format === 'sht3x_temp_hum') {
            return this._encodeSHT3x(inst);
        }
        if (cmdSpec.response_format === 'sht3x_status') {
            return this._encodeSHT3xStatus(inst);
        }
        if (cmdSpec.response_format === 'sht3x_serial') {
            return this._encodeSHT3xSerial(inst);
        }
        return new Uint8Array(cmdSpec.response_size ?? 1);
    }

    _encodeSHT3x(inst) {
        // SHT3x response: temp_msb, temp_lsb, temp_crc, hum_msb, hum_lsb, hum_crc
        const tempRaw = this._evalInverse(
            inst.values.temperature,
            inst.spec.value_encoding.temperature.inverse
        );
        const humRaw = this._evalInverse(
            inst.values.humidity,
            inst.spec.value_encoding.humidity.inverse
        );
        const t = Math.max(0, Math.min(65535, tempRaw));
        const h = Math.max(0, Math.min(65535, humRaw));
        const result = new Uint8Array(6);
        result[0] = (t >> 8) & 0xFF;
        result[1] = t & 0xFF;
        result[2] = this._crc8(result.subarray(0, 2));
        result[3] = (h >> 8) & 0xFF;
        result[4] = h & 0xFF;
        result[5] = this._crc8(result.subarray(3, 5));
        return result;
    }

    _encodeSHT3xStatus(inst) {
        // Status: 2 bytes + CRC. Bit 13 = heater. Return all-clear.
        const result = new Uint8Array(3);
        result[0] = 0x00;
        result[1] = 0x00;
        result[2] = this._crc8(result.subarray(0, 2));
        return result;
    }

    _encodeSHT3xSerial(inst) {
        // Serial number: 2 words, each with CRC = 6 bytes
        // Return a fake serial 0x0042_0001
        const result = new Uint8Array(6);
        result[0] = 0x00; result[1] = 0x42;
        result[2] = this._crc8(result.subarray(0, 2));
        result[3] = 0x00; result[4] = 0x01;
        result[5] = this._crc8(result.subarray(3, 5));
        return result;
    }

    _crc8(data) {
        // Sensirion CRC-8 (polynomial 0x31, init 0xFF)
        let crc = 0xFF;
        for (let i = 0; i < data.length; i++) {
            crc ^= data[i];
            for (let b = 0; b < 8; b++) {
                crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
                crc &= 0xFF;
            }
        }
        return crc;
    }

    // ── BME280 calibration-based encoding ────────────────────────────────────

    _fillBME280Calibration(spec, regFile) {
        const cal = spec.calibration;
        if (!cal) return;

        // Pack temperature + pressure calibration at 0x88 (24 bytes)
        // struct "<HhhHhhhhhhhh"
        const tp = [
            cal.dig_T1, cal.dig_T2, cal.dig_T3,
            cal.dig_P1, cal.dig_P2, cal.dig_P3,
            cal.dig_P4, cal.dig_P5, cal.dig_P6,
            cal.dig_P7, cal.dig_P8, cal.dig_P9,
        ];
        const tpFormats = ['H','h','h','H','h','h','h','h','h','h','h','h'];
        let offset = 0x88;
        for (let i = 0; i < tp.length; i++) {
            this._packLE16(regFile, offset, tp[i], tpFormats[i] === 'H');
            offset += 2;
        }

        // H1 at 0xA1 (1 byte, unsigned)
        regFile[0xA1] = cal.dig_H1 & 0xFF;

        // H2-H6 at 0xE1 (7 bytes, struct "<hBbBbb")
        // H2: signed 16-bit LE
        this._packLE16(regFile, 0xE1, cal.dig_H2, false);
        // H3: unsigned 8-bit
        regFile[0xE3] = cal.dig_H3 & 0xFF;
        // H4 and H5 share nibbles via bytes at 0xE4, 0xE5, 0xE6
        const h4 = cal.dig_H4;
        const h5 = cal.dig_H5;
        const x = h4 >> 4;  // coeff[2]: signed 8-bit
        const yLo = h4 & 0xF;
        const yHi = h5 & 0xF;
        const y = yLo | (yHi << 4);  // coeff[3]: unsigned 8-bit
        const z = h5 >> 4;  // coeff[4]: signed 8-bit
        regFile[0xE4] = x & 0xFF;   // signed byte
        regFile[0xE5] = y & 0xFF;   // unsigned byte
        regFile[0xE6] = z & 0xFF;   // signed byte
        // H6: signed 8-bit
        regFile[0xE7] = cal.dig_H6 & 0xFF;
    }

    _packLE16(regFile, addr, value, unsigned) {
        let v = value;
        if (!unsigned && v < 0) v = v + 65536;
        regFile[addr]     = v & 0xFF;
        regFile[addr + 1] = (v >> 8) & 0xFF;
    }

    _bme280EncodeAll(inst) {
        const cal = inst.spec.calibration;
        if (!cal) return;

        const targetTemp = inst.values.temperature;
        const targetPressure = inst.values.pressure;
        const targetHumidity = inst.values.humidity;

        // Find raw temperature ADC using binary search
        const rawTemp = this._bme280InverseTemp(targetTemp, cal);
        // Compute t_fine (needed for pressure and humidity compensation)
        const { t_fine } = this._bme280ForwardTemp(rawTemp, cal);
        // Find raw pressure ADC
        const rawPressure = this._bme280InversePressure(targetPressure, t_fine, cal);
        // Find raw humidity ADC
        const rawHumidity = this._bme280InverseHumidity(targetHumidity, t_fine, cal);

        // Write raw ADC values to register file
        // Temperature at 0xFA (3 bytes, uint20_be, raw * 16 gives the 24-bit register value)
        this._writeRegBytes(inst.regFile, 0xFA, 3, 'uint20_be', Math.round(rawTemp));
        // Pressure at 0xF7 (3 bytes, uint20_be)
        this._writeRegBytes(inst.regFile, 0xF7, 3, 'uint20_be', Math.round(rawPressure));
        // Humidity at 0xFD (2 bytes, uint16_be)
        this._writeRegBytes(inst.regFile, 0xFD, 2, 'uint16_be', Math.round(rawHumidity));
    }

    // BME280 forward compensation (matches the Bosch/Adafruit algorithm exactly)

    _bme280ForwardTemp(rawAdc, cal) {
        const var1 = (rawAdc / 16384.0 - cal.dig_T1 / 1024.0) * cal.dig_T2;
        const var2 = ((rawAdc / 131072.0 - cal.dig_T1 / 8192.0) ** 2) * cal.dig_T3;
        const t_fine = Math.round(var1 + var2);
        return { temp: t_fine / 5120.0, t_fine };
    }

    _bme280ForwardPressure(rawAdc, t_fine, cal) {
        let var1 = t_fine / 2.0 - 64000.0;
        let var2 = var1 * var1 * cal.dig_P6 / 32768.0;
        var2 = var2 + var1 * cal.dig_P5 * 2.0;
        var2 = var2 / 4.0 + cal.dig_P4 * 65536.0;
        const var3 = cal.dig_P3 * var1 * var1 / 524288.0;
        var1 = (var3 + cal.dig_P2 * var1) / 524288.0;
        var1 = (1.0 + var1 / 32768.0) * cal.dig_P1;
        if (var1 === 0) return 0;
        let pressure = 1048576.0 - rawAdc;
        pressure = ((pressure - var2 / 4096.0) * 6250.0) / var1;
        var1 = cal.dig_P9 * pressure * pressure / 2147483648.0;
        var2 = pressure * cal.dig_P8 / 32768.0;
        pressure = pressure + (var1 + var2 + cal.dig_P7) / 16.0;
        return pressure / 100.0; // hPa
    }

    _bme280ForwardHumidity(rawAdc, t_fine, cal) {
        let var1 = t_fine - 76800.0;
        let var2 = cal.dig_H4 * 64.0 + (cal.dig_H5 / 16384.0) * var1;
        let var3 = rawAdc - var2;
        let var4 = cal.dig_H2 / 65536.0;
        let var5 = 1.0 + (cal.dig_H3 / 67108864.0) * var1;
        let var6 = 1.0 + (cal.dig_H6 / 67108864.0) * var1 * var5;
        var6 = var3 * var4 * (var5 * var6);
        let humidity = var6 * (1.0 - cal.dig_H1 * var6 / 524288.0);
        return Math.max(0, Math.min(100, humidity));
    }

    // BME280 inverse: binary search for raw ADC that produces target reading

    _bme280InverseTemp(targetTemp, cal) {
        let lo = 0, hi = 1048575; // 20-bit range
        for (let i = 0; i < 30; i++) {
            const mid = Math.floor((lo + hi) / 2);
            const { temp } = this._bme280ForwardTemp(mid, cal);
            if (temp < targetTemp) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    _bme280InversePressure(targetPressureHPa, t_fine, cal) {
        const targetPa = targetPressureHPa; // already in hPa
        let lo = 0, hi = 1048575;
        for (let i = 0; i < 30; i++) {
            const mid = Math.floor((lo + hi) / 2);
            const p = this._bme280ForwardPressure(mid, t_fine, cal);
            // Pressure decreases as raw ADC increases (inverse relationship)
            if (p > targetPa) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    _bme280InverseHumidity(targetHum, t_fine, cal) {
        let lo = 0, hi = 65535; // 16-bit range
        for (let i = 0; i < 25; i++) {
            const mid = Math.floor((lo + hi) / 2);
            const h = this._bme280ForwardHumidity(mid, t_fine, cal);
            if (h < targetHum) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }
}
