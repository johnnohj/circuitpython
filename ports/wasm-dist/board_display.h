/*
 * board_display.h — WASM framebuffer display initialization
 */

#pragma once

// Initialize the framebuffer display and supervisor terminal.
// Must be called after mp_init() and GC setup.
void board_display_init(void);

// Force a display refresh (renders terminal content to framebuffer).
void board_display_refresh(void);
