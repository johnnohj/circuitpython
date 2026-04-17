/*
 * common-hal/displayio/__init__.c — WASI port displayio
 *
 * The displayio common_hal functions (get/set primary display,
 * release_displays, auto_primary_display) are implemented in
 * shared-module/displayio/__init__.c. This file exists only to
 * satisfy the build system's expectation of a common-hal directory
 * for any enabled module, and to provide any port-specific overrides
 * if needed in the future.
 */

// No port-specific overrides needed — shared-module provides everything.
