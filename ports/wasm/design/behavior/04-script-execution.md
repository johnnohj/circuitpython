# Stage 10-14: Auto-reload, safemode.py, boot.py, code.py, REPL

Back to [README](README.md)

---

## 10. Auto-reload

| Upstream | Our port |
|----------|----------|
| Watch filesystem for changes | JS watches for file changes |
| 500ms debounce delay | Same, in JS |
| Restart code.py on change | JS triggers reload event |

**Our behavior**: JS-side file watcher (IDB change listener or editor
save event) detects changes, sends a reload event through the event
ring, which triggers a soft reboot.

Auto-reload on save is on by default.  "Save" means persisting to
the IDBFS location (explicit user action or editor save).  This is
distinct from "Run" (see stage 13 below).

**Deviation**: Environmental.  The mechanism differs (no inotify/USB
mass storage), but the user-visible behavior is the same.

---

## 11. run_safemode_py

| Upstream | Our port |
|----------|----------|
| start_mp -> run safemode.py -> cleanup_after_vm | Not implemented |

**Our behavior**: Skip (safe mode is never entered).

**Deviation**: Environmental (no hardware faults).  Safe mode support
is gated behind `ENABLE_SIM_SAFE_MODE` (see
[02-serial-and-stack.md](02-serial-and-stack.md)).

---

## 12. run_boot_py

| Upstream | Our port |
|----------|----------|
| start_mp -> run boot.py -> cleanup_after_vm | Partially implemented |
| boot.py can configure USB, autoreload, filesystem permissions | Only autoreload relevant |

**Our behavior**: Execute boot.py if it exists.  Boot.py can import
modules, configure autoreload.  USB configuration is irrelevant (no
USB hardware).

boot.py prepares the environment for code.py execution.  Its effects
persist into code.py via supervisor-level state (autoreload settings,
filesystem permissions) and via globals saved to the heap/pystack
across the boot->code transition.

**Deviation**: Intentional (USB config no-ops).  Filesystem permission
model may differ -- TBD during migration.

**Note on VM lifecycle**: Upstream fully tears down the GC and
reinitializes between boot.py and code.py (`cleanup_after_vm` +
`start_mp`).  boot.py's Python-level globals do NOT persist into
code.py upstream -- only supervisor-level side effects do (autoreload
settings, filesystem permissions, USB config).  Whether we want to
preserve this isolation or allow boot.py globals to persist into
code.py is a design decision to resolve during Phase 4.

---

## 13. run_code_py (decided)

| Upstream | Our port |
|----------|----------|
| start_mp -> run code.py -> cleanup_after_vm | Idle until user action |
| Blocking: runs until completion or interrupt | Non-blocking: frame-budgeted via abort-resume |
| Auto-starts on boot | User-initiated (Run button or auto-reload) |

**Our behavior**: After initialization, the port idles -- everything
is ready but no user code is running.  The user initiates execution
in one of two ways:

1. **"Run" button**: Compiles the code currently displayed in the
   editor element and executes it.  This is a (re)compile from the
   editor content, not necessarily from the persisted file.

2. **"Save" + auto-reload**: Saving persists the editor content to
   the IDBFS location.  If auto-reload is enabled (default), this
   triggers a reload event -> soft reboot -> compile & run the
   persisted code.py.

In both cases, execution is frame-budgeted via abort-resume.  Each
frame runs for ~8-10ms then aborts back to the chassis.

**Deviation**: Intentional.  Upstream auto-runs code.py on boot.  We
idle until user action for better UX (matches the `index.html`
pattern of getting everything ready and waiting).

---

## 14. run_repl

| Upstream | Our port |
|----------|----------|
| start_mp -> pyexec_friendly_repl -> cleanup_after_vm | C-side readline via keystroke buffer |
| Character-at-a-time processing via pyexec_event_repl_process_char | Keystrokes delivered as part of frame packet |
| Ctrl-D -> soft reboot -> code.py | Ctrl-D -> soft reboot -> idle |

**Our behavior**: Keystrokes arrive as part of the frame packet
(input buffer filled by JS before each frame).  A background task
feeds them to C-side readline, which handles line editing, tab
completion, and hints natively.  This gives us upstream-compatible
completion/hint behavior while JS still owns the keyboard capture
and display rendering.

When the user presses Enter, the accumulated line is compiled and
executed via abort-resume, same as code.py.

**Deviation**: The input transport differs (frame packet vs UART) but
C-side readline does the actual line processing, keeping us closer
to upstream behavior than the previous JS-owned readline model.
Tab completion and hints work through the standard C path.
