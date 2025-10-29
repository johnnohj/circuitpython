# Test code for the co-supervisor
# This should demonstrate non-blocking execution

import board
import digitalio
import time

# Setup: Initialize LED
led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

print("Starting LED blink loop...")

# Main loop: Should execute non-blockingly via supervisor
while True:
    led.value = not led.value
    print(f"LED toggled! Value: {led.value}")
    time.sleep(1)
