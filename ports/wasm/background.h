// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

#pragma once

// Port-specific background task function
// Called by CircuitPython's RUN_BACKGROUND_TASKS macro
void port_background_task(void);

// Initialize background task system
void port_background_hook_init(void);
