# Frozen manifest for ws-worker variant.
#
# Includes the browser variant's modules plus WS protocol wrappers
# that shadow C modules with protocol-broadcasting versions.

# Browser variant modules (asyncio, etc.)
include("$(PORT_DIR)/variants/browser/manifest.py")

# WS protocol bus
freeze("modules", "_ws_bus.py")

# Protocol wrappers — these shadow the C modules
# (C modules are accessible via _digitalio, _analogio, etc.)
freeze("modules", "digitalio.py")
freeze("modules", "analogio.py")
