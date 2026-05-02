# ws-worker variant: browser + WS protocol wrappers
include $(TOP)/ports/wasm/variants/browser/mpconfigvariant.mk

# Add module aliases (dual-registration under _prefixed names)
SRC_C += variants/ws-worker/module_aliases.c

# Freeze protocol wrapper .py files
FROZEN_MANIFEST = variants/ws-worker/manifest.py
