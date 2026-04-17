/*
 * wasm_framebuffer.h — WASM framebuffer display for browser Canvas
 *
 * A simple framebuffer in WASM linear memory that implements the
 * CircuitPython framebuffer protocol. The display pipeline renders
 * the REPL terminal (terminalio) into this buffer. The browser host
 * reads the buffer directly from WASM memory and paints it onto a
 * Canvas element.
 *
 * RGB565 format, configurable dimensions. The host discovers the
 * buffer address, width, and height via exported WASM functions.
 */

#pragma once

#include "py/obj.h"

// Default display dimensions (matches a small built-in screen)
#ifndef WASM_DISPLAY_WIDTH
#define WASM_DISPLAY_WIDTH  320
#endif
#ifndef WASM_DISPLAY_HEIGHT
#define WASM_DISPLAY_HEIGHT 240
#endif

// Bytes per pixel (RGB565 = 2)
#define WASM_DISPLAY_BPP    2

// Total framebuffer size in bytes
#define WASM_DISPLAY_FB_SIZE (WASM_DISPLAY_WIDTH * WASM_DISPLAY_HEIGHT * WASM_DISPLAY_BPP)

typedef struct {
    mp_obj_base_t base;
    uint16_t width;
    uint16_t height;
    uint8_t *buffer;
    volatile uint32_t frame_count;  // Incremented on each swapbuffers
} wasm_framebuffer_obj_t;

extern const mp_obj_type_t wasm_framebuffer_type;

// Module-level singleton (created during board init)
extern wasm_framebuffer_obj_t wasm_framebuffer_instance;

// The actual pixel buffer (exported for WASM host access)
extern uint8_t wasm_display_buffer[WASM_DISPLAY_FB_SIZE];

// Initialize the framebuffer singleton
void wasm_framebuffer_init(void);

// Accessors (also exported as WASM functions for host access)
uint8_t *wasm_display_fb_addr(void);
int wasm_display_fb_width(void);
int wasm_display_fb_height(void);

// Cursor info — filled by wasm_cursor_info_update(), read by JS
// via wasm_cursor_info_addr() direct memory access.
typedef struct {
    uint16_t cursor_x;      // character column
    uint16_t cursor_y;      // character row (logical, in tile buffer)
    uint16_t scroll_x;      // scroll area pixel origin X
    uint16_t scroll_y;      // scroll area pixel origin Y
    uint16_t top_left_y;    // circular buffer scroll offset
    uint16_t height_tiles;  // scroll area height in tiles
    uint16_t glyph_w;       // glyph width in pixels
    uint16_t glyph_h;       // glyph height in pixels
    uint16_t scale;         // display group scale factor
    uint16_t _pad;
} wasm_cursor_info_t;

void wasm_cursor_info_update(void);
uint32_t wasm_display_frame_count(void);
