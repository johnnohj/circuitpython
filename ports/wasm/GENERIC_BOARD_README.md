# Generic Metro Board Implementation

This document describes the implementation of a generic Metro-style board for the CircuitPython WebAssembly HAL system.

## Overview

The generic Metro board provides a comprehensive fallback board configuration that always succeeds when runtime board configuration is needed. It simulates a standard Arduino Metro/Uno layout with common peripherals and capabilities.

## Implementation Files

### Core Files

- **`/home/jef/dev/wasm/circuitpython/ports/wasm/generic_board.h`**: Header with pin definitions, board metadata, and peripheral configurations
- **`/home/jef/dev/wasm/circuitpython/ports/wasm/generic_board.c`**: Board initialization, JSON generation, and JavaScript interface functions
- **`/home/jef/dev/wasm/circuitpython/ports/wasm/providers/generic_provider.c`**: HAL provider implementation for the generic board

### Configuration Files

- **`/home/jef/dev/wasm/circuitpython/ports/wasm/mpconfigport.h`**: Updated with `MICROPY_USE_INTERNAL_PRINTF (0)` to avoid linking conflicts
- **`/home/jef/dev/wasm/circuitpython/ports/wasm/Makefile`**: Updated to include generic board sources and JavaScript exports
- **`/home/jef/dev/wasm/circuitpython/ports/wasm/main.c`**: Registers the generic provider during initialization

## Board Specifications

### Board Metadata
- **Name**: Generic Metro (WASM Simulator)  
- **MCU**: Virtual SAMD21G18 (for compatibility)
- **Flash**: 256KB
- **RAM**: 32KB  
- **CPU**: 48 MHz
- **Logic Level**: 3.3V

### Pin Layout

The generic board provides 36 pins with the following layout:

#### Digital Pins (D0-D13)
- **D0** (PA00): Digital + UART RX
- **D1** (PA01): Digital + UART TX  
- **D2** (PA02): Digital only
- **D3** (PA03): Digital + PWM
- **D4** (PA04): Digital only
- **D5** (PA05): Digital + PWM
- **D6** (PA06): Digital + PWM
- **D7** (PA07): Digital only
- **D8** (PA08): Digital only
- **D9** (PA09): Digital + PWM
- **D10** (PA10): Digital + PWM + SPI CS
- **D11** (PA11): Digital + PWM + SPI MOSI  
- **D12** (PA12): Digital + SPI MISO
- **D13** (PA13): Digital + SPI SCK + Built-in LED

#### Analog Pins (A0-A5)  
- **A0** (PA14): Digital + Analog Input
- **A1** (PA15): Digital + Analog Input
- **A2** (PA16): Digital + Analog Input
- **A3** (PA17): Digital + Analog Input
- **A4** (PA18): Digital + Analog Input + I2C SDA
- **A5** (PA19): Digital + Analog Input + I2C SCL

#### Special Pins
- **LED** (PA13): Built-in LED (same as D13)
- **BUTTON** (PA20): User button with internal pull-up
- **NEOPIXEL** (PA21): NeoPixel data pin

#### Peripheral Aliases
- **SDA** (PA18): I2C Data (same as A4)
- **SCL** (PA19): I2C Clock (same as A5)  
- **MOSI** (PA11): SPI Master Out (same as D11)
- **MISO** (PA12): SPI Master In (same as D12)
- **SCK** (PA13): SPI Clock (same as D13)
- **TX** (PA01): UART Transmit (same as D1)
- **RX** (PA00): UART Receive (same as D0)

### Pin Capabilities

Each pin has capability flags indicating supported functions:

- `PIN_CAP_DIGITAL` (0x01): Digital I/O
- `PIN_CAP_ANALOG_IN` (0x02): Analog input (ADC)
- `PIN_CAP_PWM` (0x04): Pulse width modulation
- `PIN_CAP_I2C` (0x08): I2C communication
- `PIN_CAP_SPI` (0x10): SPI communication  
- `PIN_CAP_UART` (0x20): UART communication
- `PIN_CAP_TOUCH` (0x40): Touch sensing (unused)

### Peripherals

The board provides the following peripheral configurations:

1. **I2C**: SDA (A4/D18), SCL (A5/D19)
2. **SPI**: MOSI (D11), MISO (D12), SCK (D13), CS (D10)
3. **UART**: TX (D1), RX (D0)
4. **NEOPIXEL**: NEOPIXEL pin

## JavaScript Interface

### Exported Functions

The following functions are exported for JavaScript access:

- `mp_js_init_generic_board()`: Initialize the generic board
- `mp_js_get_generic_board_json()`: Get board configuration as JSON
- `mp_js_generic_pin_set_value(pin_name, value)`: Set pin output value
- `mp_js_generic_pin_get_value(pin_name)`: Get pin input value  
- `mp_js_generic_pin_set_direction(pin_name, direction)`: Set pin direction
- `mp_js_generate_board_module()`: Generate Python board module source

### JavaScript Callbacks

The implementation supports optional JavaScript callbacks:

- `Module.onLEDChange(value)`: Called when LED state changes
- `Module.onGenericPinChange(pinName, value)`: Called when any pin changes
- `Module.getButtonState()`: Should return button state (true = pressed)
- `Module.getAnalogValue(pinName)`: Should return analog value (0-1023)

## HAL Provider Integration

The generic board implements the CircuitPython HAL provider interface:

### Provider Capabilities
- `HAL_CAP_DIGITAL_IO`: Digital input/output
- `HAL_CAP_ANALOG_IN`: Analog input reading
- `HAL_CAP_ANALOG_OUT`: Analog output (PWM)
- `HAL_CAP_I2C`: I2C communication
- `HAL_CAP_SPI`: SPI communication
- `HAL_CAP_PWM`: Pulse width modulation

### Provider Operations

#### Pin Operations
- `digital_set_direction(pin, output)`: Configure pin as input/output
- `digital_set_value(pin, value)`: Set digital output value
- `digital_get_value(pin)`: Read digital input value
- `digital_set_pull(pin, pull_mode)`: Configure pull-up/pull-down
- `analog_read(pin)`: Read analog input value (0-65535) 
- `analog_write(pin, value)`: Write PWM value (0-65535)
- `pin_deinit(pin)`: Cleanup pin resources

#### I2C Operations
- `i2c_create(scl_pin, sda_pin, frequency)`: Create I2C object
- `i2c_try_lock(i2c_obj)`: Acquire I2C bus lock
- `i2c_unlock(i2c_obj)`: Release I2C bus lock
- `i2c_scan(i2c_obj, addresses, count)`: Scan for I2C devices
- `i2c_writeto(i2c_obj, addr, data, len)`: Write data to I2C device
- `i2c_readfrom(i2c_obj, addr, data, len)`: Read data from I2C device
- `i2c_deinit(i2c_obj)`: Cleanup I2C resources

#### SPI Operations
- `spi_create(clk_pin, mosi_pin, miso_pin)`: Create SPI object
- `spi_configure(spi_obj, baudrate, polarity, phase)`: Configure SPI parameters
- `spi_try_lock(spi_obj)`: Acquire SPI bus lock
- `spi_unlock(spi_obj)`: Release SPI bus lock
- `spi_write(spi_obj, data, len)`: Write data to SPI device
- `spi_readinto(spi_obj, buffer, len)`: Read data from SPI device
- `spi_deinit(spi_obj)`: Cleanup SPI resources

## Build Configuration

### Source Files Added
- `generic_board.c`
- `providers/generic_provider.c`

### Makefile Changes
- Added source files to `SRC_HAL`
- Added JavaScript exports to `EXPORTED_FUNCTIONS`
- Fixed printf linking conflicts with `MICROPY_USE_INTERNAL_PRINTF (0)`

### Dependencies
- Emscripten WebAssembly toolchain
- CircuitPython HAL provider system
- MicroPython runtime

## Testing

Two test scripts verify the implementation:

### Basic Board Test (`test_generic_board.js`)
Tests generic board functionality:
- Board initialization
- JSON configuration retrieval
- Pin operations
- Board module generation

### HAL Integration Test (`test_circuitpython_hal.js`) 
Tests CircuitPython integration:
- MicroPython runtime initialization
- Python code execution
- Module loading (digitalio, board)
- HAL provider accessibility

Both tests pass successfully, confirming the generic Metro board is fully functional.

## Usage

The generic board is automatically registered as the default HAL provider during CircuitPython initialization. It provides a comprehensive fallback that ensures runtime board configuration always succeeds, making CircuitPython WASM more robust and user-friendly.

Users can interact with the board through:
1. Standard CircuitPython APIs (`digitalio`, `board`, etc.)
2. Direct JavaScript function calls
3. JavaScript callbacks for hardware simulation
4. JSON configuration for web-based interfaces

This implementation provides a solid foundation for CircuitPython WebAssembly applications while maintaining compatibility with existing CircuitPython code patterns.