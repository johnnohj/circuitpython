# WASI port Make-level configuration
#
# circuitpy_mpconfig.mk expects certain Make variables (not just C
# preprocessor defines). This file provides them.

# No USB endpoints
USB_NUM_ENDPOINT_PAIRS = 0

# Compression level for translated messages
CIRCUITPY_MESSAGE_COMPRESSION_LEVEL ?= 0

# No USB device support
CIRCUITPY_USB_DEVICE = 0

# Long integer implementation (must match mpconfigvariant.h)
LONGINT_IMPL = MPZ
