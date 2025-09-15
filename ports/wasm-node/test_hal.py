import board
print("Testing unified HAL architecture...")
print("Board ID:", board.board_id)

# Test pin access
available_pins = [name for name in dir(board) if not name.startswith('_')]
print("Available pins:", available_pins[:10])

# Test digital I/O
try:
    import digitalio
    led = digitalio.DigitalInOut(board.LED)
    led.direction = digitalio.Direction.OUTPUT
    print("Digital I/O test: SUCCESS - LED pin configured")
    led.value = True
    print("Digital I/O test: SUCCESS - LED set to HIGH")
    led.value = False
    print("Digital I/O test: SUCCESS - LED set to LOW")
except Exception as e:
    print("Digital I/O test: FAILED -", str(e))

print("HAL architecture test completed!")