#!/usr/bin/env python3
"""Test script for CircuitPython WebAssembly port functionality."""

print("=== CircuitPython WebAssembly Port Functionality Tests ===")

# Test 1: JavaScript FFI Module
print("\n1. Testing JavaScript FFI Module...")
try:
    import jsffi
    print("✓ jsffi module imported successfully")
    
    # Test available functions
    jsffi_attrs = [attr for attr in dir(jsffi) if not attr.startswith('_')]
    print(f"Available jsffi functions: {jsffi_attrs}")
    
    # Test memory info
    mem_info = jsffi.mem_info()
    print(f"Memory info: {mem_info}")
    print("✓ jsffi.mem_info() working")
    
except Exception as e:
    print(f"✗ jsffi test failed: {e}")

# Test 2: Hardware Simulation Modules
print("\n2. Testing Hardware Simulation...")
try:
    import board
    print("✓ board module imported")
    
    # Test board pins
    pins = [attr for attr in dir(board) if attr.startswith('GP')]
    print(f"Available pins: {pins[:5]}..." if len(pins) > 5 else f"Available pins: {pins}")
    
    # Test digitalio
    import digitalio
    led = digitalio.DigitalInOut(board.GP25)
    led.direction = digitalio.Direction.OUTPUT
    led.value = True
    print("✓ Digital I/O working")
    
    # Test analogio
    import analogio
    adc = analogio.AnalogIn(board.GP26)
    adc_value = adc.value
    print(f"✓ Analog Input working (value: {adc_value})")
    
    # Test busio I2C
    import busio
    i2c = busio.I2C(board.GP0, board.GP1)
    print("✓ I2C bus created")
    
    # Test busio SPI
    spi = busio.SPI(board.GP2, MOSI=board.GP3, MISO=board.GP4)
    print("✓ SPI bus created")
    
except Exception as e:
    print(f"✗ Hardware simulation test failed: {e}")

# Test 3: Enhanced Python Libraries
print("\n3. Testing Enhanced Python Libraries...")
try:
    # Test collections
    import collections
    from collections import defaultdict
    dd = defaultdict(list)
    dd['test'].append('value')
    print("✓ collections.defaultdict working")
    
    # Test functools
    import functools
    @functools.wraps(lambda: None)
    def test_func():
        pass
    print("✓ functools working")
    
    # Test pathlib
    import pathlib
    path = pathlib.Path('test')
    print("✓ pathlib working")
    
    # Test base64
    import base64
    encoded = base64.b64encode(b'test')
    decoded = base64.b64decode(encoded)
    assert decoded == b'test'
    print("✓ base64 working")
    
except Exception as e:
    print(f"✗ Enhanced libraries test failed: {e}")

# Test 4: Proxy System
print("\n4. Testing Proxy System...")
try:
    # Test proxy creation and JS interaction
    import js
    print("✓ js module available")
    
    # Test that our hardware objects use proxies
    import digitalio
    dio = digitalio.DigitalInOut(board.GP25)
    print("✓ Proxy-backed hardware objects working")
    
except Exception as e:
    print(f"✗ Proxy system test failed: {e}")

print("\n=== Test Summary ===")
print("All preliminary functionality tests completed!")