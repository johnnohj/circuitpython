/*
 * supervisor/status_bar.c — Port-local status bar for WASM.
 *
 * Adapted from supervisor/shared/status_bar.c.
 * Same interface (status_bar.h), WASM-specific changes:
 *   - No BLE, USB keyboard, or web workflow status
 *   - Uses our port-local serial_write for output
 *   - Background callback queues updates for lazy rendering
 *
 * The status bar writes an OSC terminal title sequence that the
 * supervisor terminal renders in its top row.
 */

#include <stdbool.h>
#include "genhdr/mpversion.h"
#include "py/mpconfig.h"
#include "supervisor/background_callback.h"
#include "supervisor/shared/serial.h"
#include "supervisor/shared/status_bar.h"

static background_callback_t status_bar_background_cb;
static bool _forced_dirty = false;
static bool _suspended = false;

/* ------------------------------------------------------------------ */
/* Status bar rendering                                                */
/* ------------------------------------------------------------------ */

void supervisor_status_bar_clear(void) {
    if (!_suspended) {
        serial_write("\x1b" "]0;" "\x1b" "\\");
    }
}

/* supervisor_execution_status() — provided by supervisor.c */
extern void supervisor_execution_status(void);

void supervisor_status_bar_update(void) {
    if (_suspended) {
        supervisor_status_bar_request_update(true);
        return;
    }
    _forced_dirty = false;

    /* OSC title sequence: ESC ] 0 ; <text> ESC \ */
    serial_write("\x1b" "]0;");

    supervisor_execution_status();
    serial_write(" | ");
    serial_write(MICROPY_GIT_TAG);

    serial_write("\x1b" "\\");
}

/* ------------------------------------------------------------------ */
/* Background callback — lazy update                                   */
/* ------------------------------------------------------------------ */

static void status_bar_background(void *data) {
    (void)data;
    if (_suspended) {
        return;
    }
    if (_forced_dirty) {
        supervisor_status_bar_update();
    }
}

/* ------------------------------------------------------------------ */
/* Public interface                                                    */
/* ------------------------------------------------------------------ */

void supervisor_status_bar_start(void) {
    supervisor_status_bar_request_update(true);
}

void supervisor_status_bar_request_update(bool force_dirty) {
    if (force_dirty) {
        _forced_dirty = true;
    }
    background_callback_add_core(&status_bar_background_cb);
}

void supervisor_status_bar_suspend(void) {
    _suspended = true;
}

void supervisor_status_bar_resume(void) {
    _suspended = false;
    supervisor_status_bar_request_update(false);
}

void supervisor_status_bar_init(void) {
    status_bar_background_cb.fun = status_bar_background;
    status_bar_background_cb.data = NULL;
}
