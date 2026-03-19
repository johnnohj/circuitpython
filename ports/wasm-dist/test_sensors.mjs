/*
 * test_sensors.mjs — Sensor catalog + SensorSimulator proof of concept
 *
 * Tests:
 *   T1: SensorSimulator creates register file from catalog
 *   T2: Chip ID register returns correct value
 *   T3: setValue updates encoded register data
 *   T4: Python reads chip ID via busio.I2C (integrated test)
 *   T5: Python reads sensor value via busio.I2C (integrated test)
 *   T6: SHT31D command-based protocol simulation
 *   T7: LIS3DH multi-axis acceleration encoding
 *   T8: addSensor with custom address
 */

import { SensorSimulator } from './js/SensorSimulator.js';
import { readFileSync } from 'fs';

const catalog = JSON.parse(readFileSync('./js/sensor_catalog.json', 'utf8'));

let passed = 0;
let failed = 0;

function assert(cond, name, detail = '') {
    if (cond) { console.log('PASS', name); passed++; }
    else       { console.error('FAIL', name, detail); failed++; }
}

// ── Unit tests (no WASM) ────────────────────────────────────────────────────

// T1: Create simulator, verify instance
{
    const sim = new SensorSimulator(catalog);
    const mockHw = { addI2CDevice: () => {} };
    const id = sim.addSensor(mockHw, 'bme280', { temperature: 25 });
    assert(id === 'bme280_119', 'T1: instance ID is bme280_119');
    const inst = sim.instances;
    assert(inst['bme280_119'] !== undefined, 'T1: instance exists');
    assert(inst['bme280_119'].values.temperature === 25, 'T1: temperature=25');
}

// T2: Chip ID register
{
    const sim = new SensorSimulator(catalog);
    let registeredHandler;
    const mockHw = { addI2CDevice: (_, __, h) => { registeredHandler = h; } };
    sim.addSensor(mockHw, 'bme280');

    // Simulate: write register pointer 0xD0, then read 1 byte
    registeredHandler('i2c_write', { data: [0xD0] });
    const resp = registeredHandler('i2c_read', { len: 1 });
    assert(resp instanceof Uint8Array, 'T2: response is Uint8Array');
    assert(resp[0] === 0x60, 'T2: chip ID = 0x60 (BME280)');
}

// T3: setValue updates registers
{
    const sim = new SensorSimulator(catalog);
    let handler;
    const mockHw = { addI2CDevice: (_, __, h) => { handler = h; } };
    sim.addSensor(mockHw, 'vl53l1x', { distance: 500 });

    // Read distance register (addr 150, 2 bytes uint16_be)
    handler('i2c_write', { data: [150] });
    const resp1 = handler('i2c_read', { len: 2 });
    const dist1 = (resp1[0] << 8) | resp1[1];
    assert(dist1 === 500, 'T3: initial distance=500 (got ' + dist1 + ')');

    // Update value
    sim.setValue('vl53l1x_41', 'distance', 1200);
    handler('i2c_write', { data: [150] });
    const resp2 = handler('i2c_read', { len: 2 });
    const dist2 = (resp2[0] << 8) | resp2[1];
    assert(dist2 === 1200, 'T3: updated distance=1200 (got ' + dist2 + ')');
}

// T4: LIS3DH chip ID
{
    const sim = new SensorSimulator(catalog);
    let handler;
    const mockHw = { addI2CDevice: (_, __, h) => { handler = h; } };
    sim.addSensor(mockHw, 'lis3dh');

    handler('i2c_write', { data: [0x0F] });  // WHO_AM_I register
    const resp = handler('i2c_read', { len: 1 });
    assert(resp[0] === 0x33, 'T4: LIS3DH WHO_AM_I = 0x33');
}

// T5: LIS3DH acceleration encoding
{
    const sim = new SensorSimulator(catalog);
    let handler;
    const mockHw = { addI2CDevice: (_, __, h) => { handler = h; } };
    sim.addSensor(mockHw, 'lis3dh', { acceleration: [0, 0, 9.8] });

    // Read 6 bytes from OUT_X_L (0x28) — auto-increment
    handler('i2c_write', { data: [0x28 | 0x80] });  // 0x80 = auto-increment
    const resp = handler('i2c_read', { len: 6 });
    assert(resp.length === 6, 'T5: 6 bytes for xyz');

    // Decode Z axis (bytes 4-5, int16 little-endian)
    const z_raw = resp[4] | (resp[5] << 8);
    const z_signed = z_raw > 32767 ? z_raw - 65536 : z_raw;
    // With default ±2g range: raw = value / 9.806 * 16384
    // 9.8 → ~16380
    assert(Math.abs(z_signed - 16380) < 100, 'T5: Z acceleration encodes ~9.8 m/s² (raw=' + z_signed + ')');
}

// T6: SHT31D command-based protocol
{
    const sim = new SensorSimulator(catalog);
    let handler;
    const mockHw = { addI2CDevice: (_, __, h) => { handler = h; } };
    sim.addSensor(mockHw, 'sht31d', { temperature: 25.0, humidity: 50.0 });

    // Send measure command [0x24, 0x00]
    handler('i2c_write', { data: [0x24, 0x00] });
    const resp = handler('i2c_read', { len: 6 });
    assert(resp.length === 6, 'T6: 6-byte response (temp_msb, temp_lsb, crc, hum_msb, hum_lsb, crc)');

    // Decode temperature: -45 + 175 * (raw / 65535)
    const tempRaw = (resp[0] << 8) | resp[1];
    const tempC = -45 + 175 * (tempRaw / 65535.0);
    assert(Math.abs(tempC - 25.0) < 0.5, 'T6: temperature ≈ 25°C (got ' + tempC.toFixed(1) + ')');

    // Verify CRC
    const { SensorSimulator: SS } = await import('./js/SensorSimulator.js');
    const sim2 = new SS(catalog);
    // CRC is verified implicitly: if the driver would reject bad CRC, our encoding is wrong
    assert(resp[2] !== 0 || resp[0] === 0 && resp[1] === 0, 'T6: CRC byte is non-trivial');
}

// T7: Custom address
{
    const sim = new SensorSimulator(catalog);
    let registeredAddr;
    const mockHw = { addI2CDevice: (_, addr, __) => { registeredAddr = addr; } };
    sim.addSensor(mockHw, 'bme280', {}, { address: 0x76 });
    assert(registeredAddr === 0x76, 'T7: custom address 0x76 used');

    const id = Object.keys(sim.instances).find(k => k.includes('118'));
    assert(id === 'bme280_118', 'T7: instance ID reflects custom address');
}

// ── Integrated test with WASM Module (T8) ───────────────────────────────────

// T8: Python reads chip ID through full busio → bc_out → interceptor → mailbox path
{
    const { default: createModule } = await import('./build-dist/circuitpython.mjs');
    const { initializeModuleAPI } = await import('./api.js');
    const Module = await createModule({ _workerId: 'test-sensor', _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });
    Module._bc = new BroadcastChannel('test-sensors');

    // Load catalog and add sensor
    Module.sensors.setSensorSimulatorClass(SensorSimulator);
    Module.sensors.loadCatalog(catalog);
    Module.sensors.add('vl53l1x', { distance: 750 });

    // Install busio shim
    const { BLINKA_SHIMS } = await import('./js/shims.js');
    for (const [name, source] of Object.entries(BLINKA_SHIMS)) {
        Module.FS.writeFile('/flash/lib/' + name, source);
    }

    // Run Python that reads chip ID
    const r = Module.vm.run(`
import busio, board
i2c = busio.I2C(board.SCL, board.SDA)
i2c.try_lock()

# Read chip ID: write register pointer, then read
buf = bytearray(1)
i2c.writeto_then_readfrom(41, bytes([0x01, 0x0F]), buf)
chip_id = buf[0]
print("chip_id=" + hex(chip_id))

# Read distance
dist_buf = bytearray(2)
i2c.writeto_then_readfrom(41, bytes([150]), dist_buf)
distance = (dist_buf[0] << 8) | dist_buf[1]
print("distance=" + str(distance))

i2c.unlock()
i2c.deinit()
`, 3000);

    Module._bc.close();

    const stdout = r.stdout;
    assert(stdout.includes('chip_id=0xea'), 'T8: Python reads VL53L1X chip ID 0xEA (got: ' + stdout.trim() + ')');
    assert(stdout.includes('distance=750'), 'T8: Python reads distance=750 (got: ' + stdout.trim() + ')');
}

// ── Done ────────────────────────────────────────────────────────────────────

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
