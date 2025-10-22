// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Background task integration for CircuitPython WASM port
// Hooks into CircuitPython's native yielding mechanism (RUN_BACKGROUND_TASKS)

#include "background.h"
#include "message_queue.h"
#include "supervisor/shared/tick.h"

// Called by CircuitPython's background task system
// This runs frequently (1000-10000 times per second) during:
// - VM execution (via MICROPY_VM_HOOK_LOOP)
// - I/O operations
// - time.sleep()
// - REPL input

void port_background_task(void) {
    // Process any completed messages from JavaScript
    message_queue_process();

    // Could add other background tasks here:
    // - USB processing (if we add WebUSB support)
    // - Network events
    // - Sensor polling
    // - Animation updates
}

// Called during port initialization
void port_background_hook_init(void) {
    // Initialize message queue system
    message_queue_init();

    // Any other background system initialization
}
