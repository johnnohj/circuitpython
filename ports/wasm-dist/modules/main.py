# main.py — CircuitPython WASM browser supervisor (frozen module)
#
# Owns the full board lifecycle: boot.py → code.py → REPL → soft reboot.
# Runs as a long-lived asyncio program pumped by cp_step() each frame.

import sys
import asyncio
import time

try:
    import jsffi
    _hal = jsffi.globalThis.hal if hasattr(jsffi.globalThis, 'hal') else None
except ImportError:
    _hal = None


# ── Helpers ──

def _set_state(state):
    if _hal and hasattr(_hal, 'set_state'):
        _hal.set_state(state)

def _has_key():
    if _hal and hasattr(_hal, 'has_key'):
        return bool(_hal.has_key())
    return False

def _get_key():
    if _hal and hasattr(_hal, 'get_key'):
        return str(_hal.get_key())
    return ''

def _read_file(path):
    try:
        with open(path, 'r') as f:
            return f.read()
    except OSError:
        return None

def _run_file(path):
    source = _read_file(path)
    if source is None:
        return False
    try:
        code = compile(source, path, 'exec')
        exec(code, {'__name__': '__main__'})
    except SystemExit:
        pass
    except KeyboardInterrupt:
        print()
    except Exception as e:
        print(type(e).__name__ + ':', e)
    return True


# ── Lifecycle ──

async def _wait_for_key():
    while True:
        if _has_key():
            _get_key()
            return
        await asyncio.sleep(0)

async def _repl_loop():
    buf = ''
    print('>>> ', end='')
    while True:
        if not _has_key():
            await asyncio.sleep(0)
            continue
        key = _get_key()
        if key == 'ctrl-d':
            if not buf:
                return
            continue
        elif key == 'ctrl-c':
            print()
            buf = ''
            print('>>> ', end='')
            continue
        elif key == 'Enter':
            print()
            if buf.strip():
                try:
                    try:
                        code = compile(buf, '<stdin>', 'eval')
                        result = eval(code)
                        if result is not None:
                            print(repr(result))
                    except SyntaxError:
                        code = compile(buf, '<stdin>', 'exec')
                        exec(code)
                except Exception as e:
                    print(type(e).__name__ + ':', e)
            buf = ''
            print('>>> ', end='')
        elif key == 'Backspace':
            if buf:
                buf = buf[:-1]
                print('\b \b', end='')
        elif len(key) == 1 and ord(key) >= 32:
            buf += key
            print(key, end='')

async def _lifecycle():
    while True:
        ver = getattr(sys, 'version', 'CircuitPython')
        print(ver, 'running on wasm-browser')
        _set_state('booting')
        _run_file('/boot.py')
        print('Auto-reload is on. Simply save files over USB to run them or enter REPL to disable.')
        _set_state('running')
        if _read_file('/code.py') is not None:
            print('code.py output:')
            _run_file('/code.py')
            print('\r\nCode done running.')
            print('\r\nPress any key to enter the REPL. Use CTRL-D to reload.')
            _set_state('waiting')
            await _wait_for_key()
        _set_state('repl')
        await _repl_loop()
        print('\r\nsoft reboot')

try:
    asyncio.run(_lifecycle())
except Exception as e:
    print('main.py crash:', type(e).__name__ + ':', e)
