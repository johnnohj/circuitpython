# The asyncio package is built from the standard implementation but with the
# core scheduler replaced with a custom scheduler that uses the JavaScript
# runtime (with setTimeout an Promise's) to contrtol the scheduling.

package(
    "asyncio",
    (
        "__init__.py",
        "core.py",
        "event.py",
        "funcs.py",
        "lock.py",
    ),
    base_path="$(PORT_DIR)",
    opt=3,
)