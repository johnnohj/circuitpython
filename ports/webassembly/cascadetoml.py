"""
Minimal cascadetoml stub for CircuitPython WebAssembly port.
WebAssembly doesn't use external flash devices, so this returns empty data.
"""

def filter_toml(path, filters):
    """
    Stub implementation that returns empty NVM data for WebAssembly.
    WebAssembly doesn't need external flash devices.
    """
    return {"nvm": []}