/*
 * worker_terminal.h — Clean terminal for the hardware worker.
 *
 * Owns the full pipeline: TileGrid → FramebufferDisplay → OPFS flush.
 * No supervisor terminal, no complex routing. We control every step:
 *
 *   worker_terminal_init()    — framebuffer + display + font + banner
 *   worker_terminal_write()   — process chars, set tiles, handle scroll
 *   worker_terminal_refresh() — composite scene graph into framebuffer
 *   worker_terminal_flush()   — write framebuffer to /hw/display/fb
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Set up framebuffer, display, TileGrid, draw Blinka + banner + prompt.
 * Call after mp_init() and GC setup. */
void worker_terminal_init(void);

/* Write text to the terminal. Handles \r \n \b and printable ASCII.
 * This is called from mp_hal_stdout_tx_strn on the worker. */
void worker_terminal_write(const char *str, size_t len);

/* Composite the scene graph into the framebuffer. */
void worker_terminal_refresh(void);

/* Write framebuffer to /hw/display/fb. */
void worker_terminal_flush(void);

/* Tick the cursor blink timer.  Call from the step loop with
 * the current time in ms.  Sets worker_terminal_dirty when toggled. */
void worker_terminal_cursor_tick(uint32_t now_ms);

/* True if content changed since last refresh. */
extern bool worker_terminal_dirty;
