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

// Note: port_background_task() has been moved to supervisor/port.c
// to match CircuitPython's architecture where all port_* functions
// are implemented in the supervisor directory.
//
// port_background_task() now calls message_queue_process() from there.
//
// message_queue_init() is called from port_init() in supervisor/port.c
