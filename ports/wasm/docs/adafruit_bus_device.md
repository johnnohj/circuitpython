# adafruit_bus_device Module for WASM

## Overview

The `adafruit_bus_device` module is now enabled for the CircuitPython WASM port! This module provides convenient wrappers around `busio.SPI` and `busio.I2C` for managing device communication.

## Implementation Details

**Key Finding**: `adafruit_bus_device` is a **pure software module** with no port-specific code required. It's implemented entirely in `shared-module` and works automatically with any port that has `busio` support.

### What Was Done

1. **Enabled Module**: Added `CIRCUITPY_BUSDEVICE = 1` to `mpconfigport.mk`
2. **Created Manifests**: Added `manifest.py` files to all variant directories (standard, integrated, asyncified)
3. **Defined Default Pins**: Added default bus pin assignments to `common-hal/board/mpconfigboard.h`

### Default Pin Assignments

The WASM virtual board now has default pins for easy bus creation:

| Bus | Pins | Usage |
|-----|------|-------|
| **I2C** | SDA=GPIO0, SCL=GPIO1 | `board.I2C()` |
| **SPI** | SCK=GPIO2, MOSI=GPIO3, MISO=GPIO4 | `board.SPI()` |
| **UART** | TX=GPIO5, RX=GPIO6 | `board.UART()` |

## Usage Examples

### Basic SPIDevice Example

```python
import board
import digitalio
from adafruit_bus_device.spi_device import SPIDevice

# Create SPI bus with default pins (GPIO2/3/4)
spi = board.SPI()

# Create chip select pin
cs = digitalio.DigitalInOut(board.GPIO10)

# Create SPI device with automatic locking and CS management
device = SPIDevice(spi, cs, baudrate=1000000, polarity=0, phase=0)

# Use the device with automatic CS and bus locking
with device as spi:
    # CS automatically asserted, bus locked
    spi.write(bytes([0x01, 0x02, 0x03]))
    result = bytearray(3)
    spi.readinto(result)
    # CS automatically de-asserted, bus unlocked
```

### Basic I2CDevice Example

```python
import board
from adafruit_bus_device.i2c_device import I2CDevice

# Create I2C bus with default pins (GPIO0/1)
i2c = board.I2C()

# Create I2C device for address 0x48
device = I2CDevice(i2c, 0x48)

# Use the device with automatic bus locking
with device as i2c:
    # Bus automatically locked
    i2c.write(bytes([0x00]))  # Write register address
    result = bytearray(2)
    i2c.readinto(result)  # Read 2 bytes
    # Bus automatically unlocked
```

### Custom Pins

You can also specify custom pins instead of using defaults:

```python
import board
import busio
from adafruit_bus_device.spi_device import SPIDevice

# Custom SPI pins
spi = busio.SPI(clock=board.GPIO20, MOSI=board.GPIO21, MISO=board.GPIO22)
cs = digitalio.DigitalInOut(board.GPIO23)

device = SPIDevice(spi, cs)
```

## Features

### SPIDevice Benefits

- **Automatic bus locking**: Ensures exclusive access during transactions
- **CS management**: Automatically asserts/de-asserts chip select
- **Configuration**: Automatically configures bus parameters (baudrate, polarity, phase)
- **Extra clocks**: Optional extra clock cycles after transaction

### I2CDevice Benefits

- **Automatic bus locking**: Ensures exclusive access during transactions
- **Device probing**: Can verify device presence at address
- **Simple API**: Context manager for clean resource management

## Testing

You can test `adafruit_bus_device` with the WASM virtual hardware:

```python
# Test script - run in WASM REPL or via runPythonAsync()
import board
from adafruit_bus_device.spi_device import SPIDevice
from adafruit_bus_device.i2c_device import I2CDevice
import digitalio

print("Testing adafruit_bus_device...")

# Test SPI device creation
spi = board.SPI()
cs = digitalio.DigitalInOut(board.GPIO10)
spi_dev = SPIDevice(spi, cs)
print("✓ SPIDevice created successfully")

# Test I2C device creation
i2c = board.I2C()
i2c_dev = I2CDevice(i2c, 0x48)
print("✓ I2CDevice created successfully")

# Test SPI context manager
with spi_dev as s:
    print("✓ SPI device context manager works")

# Test I2C context manager
with i2c_dev as i:
    print("✓ I2C device context manager works")

print("All tests passed!")
```

## Architecture

```
adafruit_bus_device (shared-module)
    ├── SPIDevice - wraps busio.SPI
    │   └── Automatic locking, CS management, configuration
    │
    └── I2CDevice - wraps busio.I2C
        └── Automatic locking, device addressing
            ↓
        busio (WASM common-hal)
            ├── SPI - Virtual SPI hardware
            └── I2C - Virtual I2C hardware
                ↓
            Virtual Hardware Layer (JavaScript)
                └── Browser/Node.js simulation
```

## Files Modified

1. **mpconfigport.mk** - Added `CIRCUITPY_BUSDEVICE = 1`
2. **common-hal/board/mpconfigboard.h** - Added default pin definitions
3. **variants/*/manifest.py** - Created empty manifests for all variants

## Benefits for WASM

1. **No additional code**: Works automatically with existing `busio` implementation
2. **Cleaner API**: Simplifies device driver code
3. **Resource safety**: Automatic locking prevents conflicts
4. **Standard interface**: Compatible with all CircuitPython device libraries that use `adafruit_bus_device`

## Next Steps

With `adafruit_bus_device` enabled, the WASM port can now support:
- CircuitPython device drivers (sensors, displays, etc.)
- Libraries built on `adafruit_bus_device`
- Educational examples demonstrating I2C/SPI protocols
- Web-based hardware simulation with device emulation
