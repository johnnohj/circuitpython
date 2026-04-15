# main.py — CircuitPython WASM browser supervisor
#
# Owns the full board lifecycle: boot.py → code.py → REPL → soft reboot.
# Runs as a long-lived program pumped by cp_step() each frame.
# The VM hook loop yields at backwards branches, keeping the frame loop
# responsive without needing asyncio (Phase 1).
#
# Later phases add asyncio for scheduling and jsffi hardware modules.

import sys
import time

# jsffi for JS communication (browser variant only)
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
    """Execute a .py file. Returns True if ran, False if not found."""
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

def _wait_for_key():
    """Spin until a key is available. The VM hook yields each iteration."""
    while True:
        if _has_key():
            _get_key()  # consume
            return
        time.sleep(0)  # yield one frame


def _repl_loop():
    """Minimal REPL (Phase 1 placeholder)."""
    buf = ''
    print('>>> ', end='')
    while True:
        if not _has_key():
            time.sleep(0)
            continue

        key = _get_key()

        if key == 'ctrl-d':
            if not buf:
                return  # soft reboot
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
                    sys.print_exception(e)
            buf = ''
            print('>>> ', end='')
        elif key == 'Backspace':
            if buf:
                buf = buf[:-1]
                print('\b \b', end='')
        elif len(key) == 1 and ord(key) >= 32:
            buf += key
            print(key, end='')


def _lifecycle():
    """Main lifecycle: boot → code.py → wait → REPL → soft reboot."""
    while True:
        # Banner
        ver = getattr(sys, 'version', 'CircuitPython')
        print(ver, 'running on wasm-browser')

        # boot.py
        _set_state('booting')
        _run_file('/boot.py')

        # Auto-reload message
        print('Auto-reload is on. Simply save files over USB to run them or enter REPL to disable.')

        # code.py
        _set_state('running')
        if _read_file('/code.py') is not None:
            print('code.py output:')
            _run_file('/code.py')
            print('\r\nCode done running.')
            print('\r\nPress any key to enter the REPL. Use CTRL-D to reload.')
            _set_state('waiting')
            _wait_for_key()

        # REPL
        _set_state('repl')
        _repl_loop()

        # Ctrl-D → soft reboot
        print('\r\nsoft reboot')


# ── Entry ──
try:
    _lifecycle()
except Exception as e:
    print('main.py crash:', type(e).__name__ + ':', e)
