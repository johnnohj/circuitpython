// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/board_display.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT

// board_display.h — WASM framebuffer display initialization

#pragma once

// Initialize the framebuffer display and supervisor terminal.
// Must be called after mp_init() and GC setup.
void board_display_init(void);

// Force a display refresh (renders terminal content to framebuffer).
void board_display_refresh(void);
