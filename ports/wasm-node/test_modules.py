import sys
print("Available modules:")
for module in sys.modules:
    print(" -", module)

print("\nTrying to access board from HAL provider...")
try:
    from hal_provider import hal_get_provider
    provider = hal_get_provider()
    if provider:
        print("HAL provider found:", provider)
        print("Has get_board_module:", hasattr(provider, 'get_board_module'))
except Exception as e:
    print("Error accessing HAL provider:", e)

print("\nChecking available imports:")
try:
    import digitalio
    print("✓ digitalio available")
except ImportError as e:
    print("✗ digitalio not available:", e)

try:
    import microcontroller
    print("✓ microcontroller available")
except ImportError as e:
    print("✗ microcontroller not available:", e)