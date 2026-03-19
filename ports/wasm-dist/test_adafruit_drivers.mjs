/*
 * test_adafruit_drivers.mjs — Integration tests: real Adafruit drivers vs SensorSimulator
 *
 * Tests each Adafruit CircuitPython driver against the catalog-driven simulator
 * to verify register-level compatibility.
 *
 * Drivers are installed to /circuitpy/lib/ (matching real CircuitPython hardware).
 * Shims (board, busio, etc.) are at /lib/ (built-in equivalents).
 *
 * Tests:
 *   T1: LIS3DH — read acceleration via adafruit_lis3dh
 *   T2: SHT31D — read temperature/humidity via adafruit_sht31d
 *   T3: BME280 — read temp/pressure/humidity via adafruit_bme280
 *   T4: VL53L1X — read distance via adafruit_vl53l1x
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

// ── Driver library source files ──────────────────────────────────────────────
// Adapted from upstream Adafruit CircuitPython libraries.
// Installed to /circuitpy/lib/ for verisimilitude with real hardware.

const ADAFRUIT_BUS_DEVICE_INIT = `\
from .i2c_device import I2CDevice
__version__ = "5.2.0"
`;

const ADAFRUIT_BUS_DEVICE_I2C = `\
class I2CDevice:
    def __init__(self, i2c, device_address, probe=True):
        self.i2c = i2c
        self.device_address = device_address
        if probe:
            self.__probe()

    def __probe(self):
        while not self.i2c.try_lock():
            pass
        try:
            if self.device_address not in self.i2c.scan():
                raise ValueError("No I2C device at address: " + hex(self.device_address))
        finally:
            self.i2c.unlock()

    def __enter__(self):
        while not self.i2c.try_lock():
            pass
        return _I2CDeviceContext(self.i2c, self.device_address)

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.i2c.unlock()
        return False

    def readinto(self, buf, *, start=0, end=None):
        if end is None:
            end = len(buf)
        while not self.i2c.try_lock():
            pass
        try:
            self.i2c.readfrom_into(self.device_address, buf, start=start, end=end)
        finally:
            self.i2c.unlock()

    def write(self, buf, *, start=0, end=None):
        if end is None:
            end = len(buf)
        while not self.i2c.try_lock():
            pass
        try:
            self.i2c.writeto(self.device_address, buf, start=start, end=end)
        finally:
            self.i2c.unlock()

    def write_then_readinto(self, out_buffer, in_buffer, *,
                            out_start=0, out_end=None,
                            in_start=0, in_end=None):
        if out_end is None:
            out_end = len(out_buffer)
        if in_end is None:
            in_end = len(in_buffer)
        while not self.i2c.try_lock():
            pass
        try:
            self.i2c.writeto_then_readfrom(
                self.device_address, out_buffer, in_buffer,
                out_start=out_start, out_end=out_end,
                in_start=in_start, in_end=in_end)
        finally:
            self.i2c.unlock()


class _I2CDeviceContext:
    def __init__(self, i2c, device_address):
        self._i2c = i2c
        self._address = device_address

    def readinto(self, buf, *, start=0, end=None):
        if end is None:
            end = len(buf)
        self._i2c.readfrom_into(self._address, buf, start=start, end=end)

    def write(self, buf, *, start=0, end=None):
        if end is None:
            end = len(buf)
        self._i2c.writeto(self._address, buf, start=start, end=end)

    def write_then_readinto(self, out_buffer, in_buffer, *,
                            out_start=0, out_end=None,
                            in_start=0, in_end=None):
        if out_end is None:
            out_end = len(out_buffer)
        if in_end is None:
            in_end = len(in_buffer)
        self._i2c.writeto_then_readfrom(
            self._address, out_buffer, in_buffer,
            out_start=out_start, out_end=out_end,
            in_start=in_start, in_end=in_end)
`;

const ADAFRUIT_LIS3DH = `\
import struct
import time
from collections import namedtuple
from micropython import const

_REG_WHOAMI = const(0x0F)
_REG_TEMPCFG = const(0x1F)
_REG_CTRL1 = const(0x20)
_REG_CTRL3 = const(0x22)
_REG_CTRL4 = const(0x23)
_REG_CTRL5 = const(0x24)
_REG_OUT_X_L = const(0x28)

RANGE_16_G = const(0b11)
RANGE_8_G = const(0b10)
RANGE_4_G = const(0b01)
RANGE_2_G = const(0b00)
DATARATE_400_HZ = const(0b0111)

STANDARD_GRAVITY = 9.806

AccelerationTuple = namedtuple("acceleration", ("x", "y", "z"))


class LIS3DH:
    def __init__(self, int1=None, int2=None):
        device_id = self._read_register_byte(_REG_WHOAMI)
        if device_id != 0x33:
            raise RuntimeError("Failed to find LIS3DH!")
        self._write_register_byte(_REG_CTRL5, 0x80)
        time.sleep(0.01)
        self._write_register_byte(_REG_CTRL1, 0x07)
        self.data_rate = DATARATE_400_HZ
        self._write_register_byte(_REG_CTRL4, 0x88)
        self._write_register_byte(_REG_TEMPCFG, 0x80)
        self._write_register_byte(_REG_CTRL5, 0x08)

    @property
    def data_rate(self):
        ctl1 = self._read_register_byte(_REG_CTRL1)
        return (ctl1 >> 4) & 0x0F

    @data_rate.setter
    def data_rate(self, rate):
        ctl1 = self._read_register_byte(_REG_CTRL1)
        ctl1 &= ~(0xF0)
        ctl1 |= rate << 4
        self._write_register_byte(_REG_CTRL1, ctl1)

    @property
    def range(self):
        ctl4 = self._read_register_byte(_REG_CTRL4)
        return (ctl4 >> 4) & 0x03

    @property
    def acceleration(self):
        divider = 1
        accel_range = self.range
        if accel_range == RANGE_16_G:
            divider = 1365
        elif accel_range == RANGE_8_G:
            divider = 4096
        elif accel_range == RANGE_4_G:
            divider = 8190
        elif accel_range == RANGE_2_G:
            divider = 16380
        x, y, z = struct.unpack("<hhh", self._read_register(_REG_OUT_X_L | 0x80, 6))
        x = (x / divider) * STANDARD_GRAVITY
        y = (y / divider) * STANDARD_GRAVITY
        z = (z / divider) * STANDARD_GRAVITY
        return AccelerationTuple(x, y, z)

    def _read_register_byte(self, register):
        return self._read_register(register, 1)[0]

    def _read_register(self, register, length):
        raise NotImplementedError

    def _write_register_byte(self, register, value):
        raise NotImplementedError


class LIS3DH_I2C(LIS3DH):
    def __init__(self, i2c, *, address=0x18, int1=None, int2=None):
        self._bus = i2c
        self._addr = address
        self._buffer = bytearray(6)
        # Probe: verify device exists
        while not self._bus.try_lock(): pass
        try:
            if address not in self._bus.scan():
                raise ValueError("No I2C device at " + hex(address))
        finally:
            self._bus.unlock()
        super().__init__(int1=int1, int2=int2)

    def _read_register(self, register, length):
        self._buffer[0] = register & 0xFF
        self._bus.writeto_then_readfrom(
            self._addr, self._buffer, self._buffer,
            out_start=0, out_end=1,
            in_start=0, in_end=length)
        return self._buffer

    def _write_register_byte(self, register, value):
        self._buffer[0] = register & 0xFF
        self._buffer[1] = value & 0xFF
        while not self._bus.try_lock(): pass
        try:
            self._bus.writeto(self._addr, self._buffer, start=0, end=2)
        finally:
            self._bus.unlock()
`;

const ADAFRUIT_SHT31D = `\
import struct
import time
from adafruit_bus_device.i2c_device import I2CDevice
from micropython import const

_SHT31_DEFAULT_ADDRESS = const(0x44)
_SHT31_SECONDARY_ADDRESS = const(0x45)
_SHT31_ADDRESSES = (_SHT31_DEFAULT_ADDRESS, _SHT31_SECONDARY_ADDRESS)
_SHT31_READSTATUS = const(0xF32D)
_SHT31_CLEARSTATUS = const(0x3041)
_SHT31_HEATER_ENABLE = const(0x306D)
_SHT31_HEATER_DISABLE = const(0x3066)
_SHT31_SOFTRESET = const(0x30A2)
_SHT31_PERIODIC_BREAK = const(0x3093)

REP_HIGH = "High"
REP_MED = "Medium"
REP_LOW = "Low"

_SINGLE_COMMANDS = (
    (REP_LOW, False, 0x2416),
    (REP_MED, False, 0x240B),
    (REP_HIGH, False, 0x2400),
    (REP_LOW, True, 0x2C10),
    (REP_MED, True, 0x2C0D),
    (REP_HIGH, True, 0x2C06),
)

_DELAY = ((REP_LOW, 0.0045), (REP_MED, 0.0065), (REP_HIGH, 0.0155))


def _crc(data):
    crc = 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc <<= 1
                crc ^= 0x31
            else:
                crc <<= 1
    return crc & 0xFF


def _unpack(data):
    length = len(data)
    crc = [None] * (length // 3)
    word = [None] * (length // 3)
    for i in range(length // 6):
        word[i * 2], crc[i * 2], word[(i * 2) + 1], crc[(i * 2) + 1] = struct.unpack(
            ">HBHB", data[i * 6 : (i * 6) + 6]
        )
        if crc[i * 2] == _crc(data[i * 6 : (i * 6) + 2]):
            length = (i + 1) * 6
    for i in range(length // 3):
        if crc[i] != _crc(data[i * 3 : (i * 3) + 2]):
            raise RuntimeError("CRC mismatch")
    return word[: length // 3]


class SHT31D:
    def __init__(self, i2c_bus, address=_SHT31_DEFAULT_ADDRESS):
        if address not in _SHT31_ADDRESSES:
            raise ValueError("Invalid address: " + hex(address))
        self.i2c_device = I2CDevice(i2c_bus, address)
        self._repeatability = REP_HIGH
        self._clock_stretching = False
        self._last_read = 0
        self._cached_temperature = None
        self._cached_humidity = None
        self._reset()

    def _command(self, command):
        with self.i2c_device as i2c:
            i2c.write(struct.pack(">H", command))

    def _reset(self):
        self._command(_SHT31_PERIODIC_BREAK)
        time.sleep(0.001)
        self._command(_SHT31_SOFTRESET)
        time.sleep(0.0015)

    def _data(self):
        data = bytearray(6)
        data[0] = 0xFF
        for command in _SINGLE_COMMANDS:
            if self._repeatability == command[0] and self._clock_stretching == command[1]:
                self._command(command[2])
        for delay in _DELAY:
            if self._repeatability == delay[0]:
                time.sleep(delay[1])
        with self.i2c_device as i2c:
            i2c.readinto(data)
        word = _unpack(data)
        length = len(word)
        temperature = [None] * (length // 2)
        humidity = [None] * (length // 2)
        for i in range(length // 2):
            temperature[i] = -45 + (175 * (word[i * 2] / 65535))
            humidity[i] = 100 * (word[(i * 2) + 1] / 65535)
        if len(temperature) == 1 and len(humidity) == 1:
            return temperature[0], humidity[0]
        return temperature, humidity

    @property
    def temperature(self):
        t, _ = self._data()
        return t

    @property
    def relative_humidity(self):
        _, h = self._data()
        return h
`;

const ADAFRUIT_BME280_INIT = `\
# adafruit_bme280 package init
`;

const ADAFRUIT_BME280_PROTOCOL = `\
class I2C_Impl:
    def __init__(self, i2c, address):
        from adafruit_bus_device import i2c_device
        self._i2c = i2c_device.I2CDevice(i2c, address)

    def read_register(self, register, length):
        with self._i2c as i2c:
            i2c.write(bytes([register & 0xFF]))
            result = bytearray(length)
            i2c.readinto(result)
            return result

    def write_register_byte(self, register, value):
        with self._i2c as i2c:
            i2c.write(bytes([register & 0xFF, value & 0xFF]))
`;

const ADAFRUIT_BME280_BASIC = `\
import struct
from time import sleep
from micropython import const
from adafruit_bme280.protocol import I2C_Impl

_BME280_ADDRESS = const(0x77)
_BME280_CHIPID = const(0x60)
_BME280_REGISTER_CHIPID = const(0xD0)
OVERSCAN_X1 = const(0x01)
OVERSCAN_X16 = const(0x05)
_BME280_MODES = (0x00, 0x01, 0x03)
IIR_FILTER_DISABLE = const(0)
STANDBY_TC_125 = const(0x02)
MODE_SLEEP = const(0x00)
MODE_FORCE = const(0x01)
MODE_NORMAL = const(0x03)
_BME280_REGISTER_SOFTRESET = const(0xE0)
_BME280_REGISTER_CTRL_HUM = const(0xF2)
_BME280_REGISTER_STATUS = const(0xF3)
_BME280_REGISTER_CTRL_MEAS = const(0xF4)
_BME280_REGISTER_CONFIG = const(0xF5)
_BME280_REGISTER_TEMPDATA = const(0xFA)
_BME280_REGISTER_HUMIDDATA = const(0xFD)


class Adafruit_BME280:
    def __init__(self, bus_implementation):
        self._bus_implementation = bus_implementation
        chip_id = self._read_byte(_BME280_REGISTER_CHIPID)
        if _BME280_CHIPID != chip_id:
            raise RuntimeError("Failed to find BME280! Chip ID 0x%x" % chip_id)
        self._iir_filter = IIR_FILTER_DISABLE
        self.overscan_humidity = OVERSCAN_X1
        self.overscan_temperature = OVERSCAN_X1
        self.overscan_pressure = OVERSCAN_X16
        self._t_standby = STANDBY_TC_125
        self._mode = MODE_SLEEP
        self._reset()
        self._read_coefficients()
        self._write_ctrl_meas()
        self._write_config()
        self.sea_level_pressure = 1013.25
        self._t_fine = None

    def _read_temperature(self):
        if self.mode != MODE_NORMAL:
            self.mode = MODE_FORCE
            while self._get_status() & 0x08:
                sleep(0.002)
        raw_temperature = self._read24(_BME280_REGISTER_TEMPDATA) / 16
        var1 = (raw_temperature / 16384.0 - self._temp_calib[0] / 1024.0) * self._temp_calib[1]
        var2 = (
            (raw_temperature / 131072.0 - self._temp_calib[0] / 8192.0)
            * (raw_temperature / 131072.0 - self._temp_calib[0] / 8192.0)
        ) * self._temp_calib[2]
        self._t_fine = int(var1 + var2)

    def _reset(self):
        self._write_register_byte(_BME280_REGISTER_SOFTRESET, 0xB6)
        sleep(0.004)

    def _write_ctrl_meas(self):
        self._write_register_byte(_BME280_REGISTER_CTRL_HUM, self.overscan_humidity)
        self._write_register_byte(_BME280_REGISTER_CTRL_MEAS, self._ctrl_meas)

    def _get_status(self):
        return self._read_byte(_BME280_REGISTER_STATUS)

    def _write_config(self):
        normal_flag = False
        if self._mode == MODE_NORMAL:
            normal_flag = True
            self.mode = MODE_SLEEP
        self._write_register_byte(_BME280_REGISTER_CONFIG, self._config)
        if normal_flag:
            self.mode = MODE_NORMAL

    @property
    def mode(self):
        return self._mode

    @mode.setter
    def mode(self, value):
        if not value in _BME280_MODES:
            raise ValueError("Mode '%s' not supported" % (value))
        self._mode = value
        self._write_ctrl_meas()

    @property
    def _config(self):
        config = 0
        if self.mode == 0x03:
            config += self._t_standby << 5
        if self._iir_filter:
            config += self._iir_filter << 2
        return config

    @property
    def _ctrl_meas(self):
        ctrl_meas = self.overscan_temperature << 5
        ctrl_meas += self.overscan_pressure << 2
        ctrl_meas += self.mode
        return ctrl_meas

    @property
    def temperature(self):
        self._read_temperature()
        return self._t_fine / 5120.0

    @property
    def pressure(self):
        self._read_temperature()
        adc = self._read24(0xF7) / 16
        var1 = float(self._t_fine) / 2.0 - 64000.0
        var2 = var1 * var1 * self._pressure_calib[5] / 32768.0
        var2 = var2 + var1 * self._pressure_calib[4] * 2.0
        var2 = var2 / 4.0 + self._pressure_calib[3] * 65536.0
        var3 = self._pressure_calib[2] * var1 * var1 / 524288.0
        var1 = (var3 + self._pressure_calib[1] * var1) / 524288.0
        var1 = (1.0 + var1 / 32768.0) * self._pressure_calib[0]
        if not var1:
            raise ArithmeticError("Invalid calibration")
        pressure = 1048576.0 - adc
        pressure = ((pressure - var2 / 4096.0) * 6250.0) / var1
        var1 = self._pressure_calib[8] * pressure * pressure / 2147483648.0
        var2 = pressure * self._pressure_calib[7] / 32768.0
        pressure = pressure + (var1 + var2 + self._pressure_calib[6]) / 16.0
        pressure /= 100
        return pressure

    @property
    def humidity(self):
        self._read_temperature()
        hum = self._read_register(0xFD, 2)
        adc = float(hum[0] << 8 | hum[1])
        var1 = float(self._t_fine) - 76800.0
        var2 = self._humidity_calib[3] * 64.0 + (self._humidity_calib[4] / 16384.0) * var1
        var3 = adc - var2
        var4 = self._humidity_calib[1] / 65536.0
        var5 = 1.0 + (self._humidity_calib[2] / 67108864.0) * var1
        var6 = 1.0 + (self._humidity_calib[5] / 67108864.0) * var1 * var5
        var6 = var3 * var4 * (var5 * var6)
        humidity = var6 * (1.0 - self._humidity_calib[0] * var6 / 524288.0)
        if humidity > 100:
            return 100
        if humidity < 0:
            return 0
        return humidity

    @property
    def relative_humidity(self):
        return self.humidity

    def _read_coefficients(self):
        coeff = self._read_register(0x88, 24)
        coeff = list(struct.unpack("<HhhHhhhhhhhh", bytes(coeff)))
        coeff = [float(i) for i in coeff]
        self._temp_calib = coeff[:3]
        self._pressure_calib = coeff[3:]
        self._humidity_calib = [0] * 6
        self._humidity_calib[0] = self._read_byte(0xA1)
        coeff = self._read_register(0xE1, 7)
        coeff = list(struct.unpack("<hBbBbb", bytes(coeff)))
        self._humidity_calib[1] = float(coeff[0])
        self._humidity_calib[2] = float(coeff[1])
        self._humidity_calib[3] = float((coeff[2] << 4) | (coeff[3] & 0xF))
        self._humidity_calib[4] = float((coeff[4] << 4) | (coeff[3] >> 4))
        self._humidity_calib[5] = float(coeff[5])

    def _read_byte(self, register):
        return self._read_register(register, 1)[0]

    def _read24(self, register):
        ret = 0.0
        for b in self._read_register(register, 3):
            ret *= 256.0
            ret += float(b & 0xFF)
        return ret

    def _read_register(self, register, length):
        return self._bus_implementation.read_register(register, length)

    def _write_register_byte(self, register, value):
        self._bus_implementation.write_register_byte(register, value)


class Adafruit_BME280_I2C(Adafruit_BME280):
    def __init__(self, i2c, address=0x77):
        super().__init__(I2C_Impl(i2c, address))
`;

const ADAFRUIT_VL53L1X = `\
import struct
import time
from adafruit_bus_device import i2c_device
from micropython import const

_VL53L1X_VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND = const(0x0008)
_GPIO_HV_MUX__CTRL = const(0x0030)
_GPIO__TIO_HV_STATUS = const(0x0031)
_PHASECAL_CONFIG__TIMEOUT_MACROP = const(0x004B)
_RANGE_CONFIG__TIMEOUT_MACROP_A_HI = const(0x005E)
_RANGE_CONFIG__VCSEL_PERIOD_A = const(0x0060)
_RANGE_CONFIG__TIMEOUT_MACROP_B_HI = const(0x0061)
_RANGE_CONFIG__VCSEL_PERIOD_B = const(0x0063)
_RANGE_CONFIG__VALID_PHASE_HIGH = const(0x0069)
_SD_CONFIG__WOI_SD0 = const(0x0078)
_SD_CONFIG__INITIAL_PHASE_SD0 = const(0x007A)
_ROI_CONFIG__USER_ROI_CENTRE_SPAD = const(0x007F)
_ROI_CONFIG__USER_ROI_REQUESTED_GLOBAL_XY_SIZE = const(0x0080)
_SYSTEM__INTERRUPT_CLEAR = const(0x0086)
_SYSTEM__MODE_START = const(0x0087)
_VL53L1X_RESULT__RANGE_STATUS = const(0x0089)
_VL53L1X_RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0 = const(0x0096)
_VL53L1X_IDENTIFICATION__MODEL_ID = const(0x010F)

TB_SHORT_DIST = {
    15: (b"\\x00\\x1d", b"\\x00\\x27"),
    20: (b"\\x00\\x51", b"\\x00\\x6e"),
    33: (b"\\x00\\xd6", b"\\x00\\x6e"),
    50: (b"\\x01\\xae", b"\\x01\\xe8"),
    100: (b"\\x02\\xe1", b"\\x03\\x88"),
    200: (b"\\x03\\xe1", b"\\x04\\x96"),
    500: (b"\\x05\\x91", b"\\x05\\xc1"),
}

TB_LONG_DIST = {
    20: (b"\\x00\\x1e", b"\\x00\\x22"),
    33: (b"\\x00\\x60", b"\\x00\\x6e"),
    50: (b"\\x00\\xad", b"\\x00\\xc6"),
    100: (b"\\x01\\xcc", b"\\x01\\xea"),
    200: (b"\\x02\\xd9", b"\\x02\\xf8"),
    500: (b"\\x04\\x8f", b"\\x04\\xa4"),
}


class VL53L1X:
    def __init__(self, i2c, address=41):
        self.i2c_device = i2c_device.I2CDevice(i2c, address)
        self._i2c = i2c
        model_id, module_type, mask_rev = self.model_info
        if model_id != 0xEA or module_type != 0xCC or mask_rev != 0x10:
            raise RuntimeError("Wrong sensor ID or type!")
        self._sensor_init()
        self._timing_budget = None
        self.timing_budget = 50

    def _sensor_init(self):
        init_seq = bytes(
            [
                0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08,
                0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
                0x00, 0xFF, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x20, 0x0B, 0x00, 0x00, 0x02, 0x0A, 0x21,
                0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC8,
                0x00, 0x00, 0x38, 0xFF, 0x01, 0x00, 0x08, 0x00,
                0x00, 0x01, 0xCC, 0x0F, 0x01, 0xF1, 0x0D, 0x01,
                0x68, 0x00, 0x80, 0x08, 0xB8, 0x00, 0x00, 0x00,
                0x00, 0x0F, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x01, 0x0F, 0x0D, 0x0E, 0x0E, 0x00,
                0x00, 0x02, 0xC7, 0xFF, 0x9B, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x00,
            ]
        )
        self._write_register(0x002D, init_seq)
        self.start_ranging()
        while not self.data_ready:
            time.sleep(0.01)
        self.clear_interrupt()
        self.stop_ranging()
        self._write_register(_VL53L1X_VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND, b"\\x09")
        self._write_register(0x0B, b"\\x00")

    @property
    def model_info(self):
        info = self._read_register(_VL53L1X_IDENTIFICATION__MODEL_ID, 3)
        return (info[0], info[1], info[2])

    @property
    def distance(self):
        if self._read_register(_VL53L1X_RESULT__RANGE_STATUS)[0] != 0x09:
            return None
        dist = self._read_register(_VL53L1X_RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0, 2)
        dist = struct.unpack(">H", dist)[0]
        return dist / 10

    def start_ranging(self):
        self._write_register(_SYSTEM__MODE_START, b"\\x40")

    def stop_ranging(self):
        self._write_register(_SYSTEM__MODE_START, b"\\x00")

    def clear_interrupt(self):
        self._write_register(_SYSTEM__INTERRUPT_CLEAR, b"\\x01")

    @property
    def data_ready(self):
        if self._read_register(_GPIO__TIO_HV_STATUS)[0] & 0x01 == self._interrupt_polarity:
            return True
        return False

    @property
    def timing_budget(self):
        return self._timing_budget

    @timing_budget.setter
    def timing_budget(self, val):
        reg_vals = None
        mode = self.distance_mode
        if mode == 1:
            reg_vals = TB_SHORT_DIST
        if mode == 2:
            reg_vals = TB_LONG_DIST
        if reg_vals is None:
            raise RuntimeError("Unknown distance mode.")
        if val not in reg_vals:
            raise ValueError("Invalid timing budget.")
        self._write_register(_RANGE_CONFIG__TIMEOUT_MACROP_A_HI, reg_vals[val][0])
        self._write_register(_RANGE_CONFIG__TIMEOUT_MACROP_B_HI, reg_vals[val][1])
        self._timing_budget = val

    @property
    def _interrupt_polarity(self):
        int_pol = self._read_register(_GPIO_HV_MUX__CTRL)[0] & 0x10
        int_pol = (int_pol >> 4) & 0x01
        return 0 if int_pol else 1

    @property
    def distance_mode(self):
        mode = self._read_register(_PHASECAL_CONFIG__TIMEOUT_MACROP)[0]
        if mode == 0x14:
            return 1
        if mode == 0x0A:
            return 2
        return None

    def _write_register(self, address, data, length=None):
        if length is None:
            length = len(data)
        with self.i2c_device as i2c:
            i2c.write(struct.pack(">H", address) + data[:length])

    def _read_register(self, address, length=1):
        data = bytearray(length)
        with self.i2c_device as i2c:
            i2c.write(struct.pack(">H", address))
            i2c.readinto(data)
        return data
`;

// ── Helper: create Module with drivers installed ─────────────────────────────

const { default: createModule } = await import('./build-dist/circuitpython.mjs');
const { initializeModuleAPI } = await import('./api.js');
const { BLINKA_SHIMS } = await import('./js/shims.js');

async function createVM(sensors) {
    const Module = await createModule({ _workerId: 'test-drv-' + Math.random(), _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });
    Module._bc = new BroadcastChannel('test-drv-' + Math.random());

    // Load catalog and add sensors
    Module.sensors.setSensorSimulatorClass(SensorSimulator);
    Module.sensors.loadCatalog(catalog);
    for (const [type, values, opts] of sensors) {
        Module.sensors.add(type, values, opts);
    }

    // Install shims at /lib/ (built-in equivalents)
    for (const [name, source] of Object.entries(BLINKA_SHIMS)) {
        Module.FS.writeFile('/flash/lib/' + name, source);
    }

    // Install Adafruit drivers at /circuitpy/lib/ (user libraries)
    const mkdirp = (path) => {
        const parts = path.split('/').filter(Boolean);
        let cur = '';
        for (const p of parts) {
            cur += '/' + p;
            try { Module.FS.mkdir(cur); } catch {}
        }
    };

    const writeLib = (path, content) => {
        const full = '/flash/circuitpy/lib/' + path;
        const dir = full.split('/').slice(0, -1).join('/');
        mkdirp(dir);
        Module.FS.writeFile(full, content);
    };

    writeLib('adafruit_bus_device/__init__.py', ADAFRUIT_BUS_DEVICE_INIT);
    writeLib('adafruit_bus_device/i2c_device.py', ADAFRUIT_BUS_DEVICE_I2C);
    writeLib('adafruit_lis3dh.py', ADAFRUIT_LIS3DH);
    writeLib('adafruit_sht31d.py', ADAFRUIT_SHT31D);
    writeLib('adafruit_bme280/__init__.py', ADAFRUIT_BME280_INIT);
    writeLib('adafruit_bme280/basic.py', ADAFRUIT_BME280_BASIC);
    writeLib('adafruit_bme280/protocol.py', ADAFRUIT_BME280_PROTOCOL);
    writeLib('adafruit_vl53l1x.py', ADAFRUIT_VL53L1X);

    return Module;
}

function run(Module, code, timeout = 10000) {
    // Prepend sys.path setup (real CP has /circuitpy/lib in path)
    const preamble = `import sys\nif '/circuitpy/lib' not in sys.path: sys.path.append('/circuitpy/lib')\n`;
    const r = Module.vm.run(preamble + code, timeout);
    Module._bc.close();
    return r;
}

// ── T1: LIS3DH ──────────────────────────────────────────────────────────────

{
    console.log('\n--- T1: LIS3DH (adafruit_lis3dh) ---');
    const Module = await createVM([
        ['lis3dh', { acceleration: [0, 0, 9.8] }],
    ]);

    const r = run(Module, `
import board, busio
i2c = busio.I2C(board.SCL, board.SDA)
import adafruit_lis3dh
lis3dh = adafruit_lis3dh.LIS3DH_I2C(i2c)
x, y, z = lis3dh.acceleration
print("x=%.2f y=%.2f z=%.2f" % (x, y, z))
`);

    const stdout = r.stdout.trim();
    const stderr = r.stderr?.trim() || '';
    if (stderr) console.log('  stderr:', stderr);
    console.log('  stdout:', stdout);

    const m = stdout.match(/x=([-\d.]+) y=([-\d.]+) z=([-\d.]+)/);
    if (m) {
        const x = parseFloat(m[1]);
        const y = parseFloat(m[2]);
        const z = parseFloat(m[3]);
        assert(Math.abs(x) < 0.5, 'T1: x ≈ 0 (got ' + x.toFixed(2) + ')');
        assert(Math.abs(y) < 0.5, 'T1: y ≈ 0 (got ' + y.toFixed(2) + ')');
        assert(Math.abs(z - 9.8) < 0.2, 'T1: z ≈ 9.8 (got ' + z.toFixed(2) + ')');
    } else {
        assert(false, 'T1: parse acceleration output', stdout);
    }
}

// ── T2: SHT31D ──────────────────────────────────────────────────────────────

{
    console.log('\n--- T2: SHT31D (adafruit_sht31d) ---');
    const Module = await createVM([
        ['sht31d', { temperature: 25.0, humidity: 60.0 }],
    ]);

    const r = run(Module, `
import board, busio
i2c = busio.I2C(board.SCL, board.SDA)
import adafruit_sht31d
sht = adafruit_sht31d.SHT31D(i2c)
t = sht.temperature
h = sht.relative_humidity
print("t=%.1f h=%.1f" % (t, h))
`);

    const stdout = r.stdout.trim();
    const stderr = r.stderr?.trim() || '';
    if (stderr) console.log('  stderr:', stderr);
    console.log('  stdout:', stdout);

    const m = stdout.match(/t=([-\d.]+) h=([-\d.]+)/);
    if (m) {
        const t = parseFloat(m[1]);
        const h = parseFloat(m[2]);
        assert(Math.abs(t - 25.0) < 1.0, 'T2: temperature ≈ 25°C (got ' + t.toFixed(1) + ')');
        assert(Math.abs(h - 60.0) < 1.0, 'T2: humidity ≈ 60% (got ' + h.toFixed(1) + ')');
    } else {
        assert(false, 'T2: parse SHT31D output', stdout);
    }
}

// ── T3: BME280 ──────────────────────────────────────────────────────────────

{
    console.log('\n--- T3: BME280 (adafruit_bme280) ---');
    const Module = await createVM([
        ['bme280', { temperature: 25.0, pressure: 1013.25, humidity: 45.0 }, { address: 0x77 }],
    ]);

    const r = run(Module, `
import board, busio
i2c = busio.I2C(board.SCL, board.SDA)
from adafruit_bme280 import basic as adafruit_bme280
bme = adafruit_bme280.Adafruit_BME280_I2C(i2c)
t = bme.temperature
p = bme.pressure
h = bme.humidity
print("t=%.1f p=%.1f h=%.1f" % (t, p, h))
`);

    const stdout = r.stdout.trim();
    const stderr = r.stderr?.trim() || '';
    if (stderr) console.log('  stderr:', stderr);
    console.log('  stdout:', stdout);

    const m = stdout.match(/t=([-\d.]+) p=([-\d.]+) h=([-\d.]+)/);
    if (m) {
        const t = parseFloat(m[1]);
        const p = parseFloat(m[2]);
        const h = parseFloat(m[3]);
        assert(Math.abs(t - 25.0) < 1.0, 'T3: temperature ≈ 25°C (got ' + t.toFixed(1) + ')');
        assert(Math.abs(p - 1013.25) < 5.0, 'T3: pressure ≈ 1013.25 hPa (got ' + p.toFixed(1) + ')');
        assert(Math.abs(h - 45.0) < 3.0, 'T3: humidity ≈ 45% (got ' + h.toFixed(1) + ')');
    } else {
        assert(false, 'T3: parse BME280 output', stdout);
    }
}

// ── T4: VL53L1X ─────────────────────────────────────────────────────────────

{
    console.log('\n--- T4: VL53L1X (adafruit_vl53l1x) ---');
    const Module = await createVM([
        ['vl53l1x', { distance: 1500 }],
    ]);

    const r = run(Module, `
import board, busio
i2c = busio.I2C(board.SCL, board.SDA)
import adafruit_vl53l1x
vl = adafruit_vl53l1x.VL53L1X(i2c)
vl.start_ranging()
d = vl.distance
vl.stop_ranging()
print("d=%.1f" % d)
`);

    const stdout = r.stdout.trim();
    const stderr = r.stderr?.trim() || '';
    if (stderr) console.log('  stderr:', stderr);
    console.log('  stdout:', stdout);

    const m = stdout.match(/d=([-\d.]+)/);
    if (m) {
        const d = parseFloat(m[1]);
        // distance in the catalog is mm, driver returns cm (distance_mm / 10)
        assert(Math.abs(d - 150.0) < 1.0, 'T4: distance ≈ 150 cm (got ' + d.toFixed(1) + ')');
    } else {
        assert(false, 'T4: parse VL53L1X output', stdout);
    }
}

// ── Done ─────────────────────────────────────────────────────────────────────

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
