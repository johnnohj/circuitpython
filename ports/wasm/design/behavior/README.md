# Expected Port/Board Behavior

**Status**: Living document (created 2026-04-27, updated throughout migration)
**Purpose**: Define what this port SHOULD do at each lifecycle stage, how
it deviates from upstream, and how behavior differs across runtime
environments.  This is the acceptance criteria for the migration.

## Files in this directory

### Lifecycle stages (upstream main.c mapping)
- [01-hardware-init.md](01-hardware-init.md) — `port_init`, pin reset, heap init
- [02-serial-and-stack.md](02-serial-and-stack.md) — `serial_early_init`, safe mode window, `stack_init`
- [03-filesystem.md](03-filesystem.md) — `filesystem_init`, reset port/board, `board_init`
- [04-script-execution.md](04-script-execution.md) — auto-reload, safemode.py, boot.py, code.py, REPL
- [05-vm-lifecycle.md](05-vm-lifecycle.md) — `start_mp`, `cleanup_after_vm`

### Cross-cutting concerns
- [06-runtime-environments.md](06-runtime-environments.md) — Node CLI vs browser vs Web Worker
- [07-deviations.md](07-deviations.md) — structural, environmental, intentional, gaps
- [08-acceptance-criteria.md](08-acceptance-criteria.md) — concrete test cases
- [09-open-questions.md](09-open-questions.md) — decisions to resolve during migration

## Upstream lifecycle (reference)

CircuitPython's `main.c` defines the canonical lifecycle:

```
main()
 |- port_init()                    [hardware init, safe mode detect]
 |- reset_all_pins()
 |- port_heap_init()
 |- serial_early_init()
 |- wait_for_safe_mode_reset()     [user button window]
 |- stack_init()
 |- filesystem_init()              [mount CIRCUITPY]
 |- reset_port() / reset_board()
 |- board_init()                   [displays, buses]
 |- autoreload_enable()
 |
 |- run_safemode_py()              [if safe mode]
 |- run_boot_py()
 |- supervisor_workflow_start()
 |
 +- loop:
     |- run_repl()                 [if not skipping]
     +- run_code_py()              [-> loop back]
```

Each `run_*` call does: `start_mp()` -> execute -> `cleanup_after_vm()`.
The VM is fully torn down and rebuilt between stages.
