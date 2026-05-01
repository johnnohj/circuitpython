
import board, digitalio, neopixel
pixels = neopixel.NeoPixel(board.NEOPIXEL, 10, auto_write=True)
btn = digitalio.DigitalInOut(board.BUTTON_A)
btn.direction = digitalio.Direction.INPUT
btn.pull = digitalio.Pull.UP
print("OK: neopixel + button claimed")
