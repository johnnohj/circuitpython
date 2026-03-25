include("$(PORT_DIR)/variants/manifest.py")

# Freeze hardware shims into reactor build
# _hw must come first (other shims import it)
module("_hw.py", base_path="$(PORT_DIR)/modules", opt=3)
module("analogio.py", base_path="$(PORT_DIR)/modules", opt=3)
module("board.py", base_path="$(PORT_DIR)/modules", opt=3)
module("busio.py", base_path="$(PORT_DIR)/modules", opt=3)
module("digitalio.py", base_path="$(PORT_DIR)/modules", opt=3)
module("displayio.py", base_path="$(PORT_DIR)/modules", opt=3)
module("microcontroller.py", base_path="$(PORT_DIR)/modules", opt=3)
module("neopixel_write.py", base_path="$(PORT_DIR)/modules", opt=3)
module("pwmio.py", base_path="$(PORT_DIR)/modules", opt=3)
