/*
 * wasm_framebuffer.c — WASM framebuffer display for browser Canvas
 *
 * Implements the CircuitPython framebuffer protocol (framebuffer_p_t).
 * The buffer lives in WASM linear memory so the host can read it
 * directly via memory.buffer views without any copy.
 *
 * The host discovers the buffer via exported functions:
 *   wasm_display_fb_addr()  → pointer to RGB565 pixel data
 *   wasm_display_fb_width() → width in pixels
 *   wasm_display_fb_height()→ height in pixels
 *   wasm_display_frame_count() → frame counter (increments on swapbuffers)
 */

#include "wasm_framebuffer.h"

#include "py/runtime.h"
#include "shared-module/framebufferio/FramebufferDisplay.h"

// ---- Static framebuffer in linear memory ----

uint8_t wasm_display_buffer[WASM_DISPLAY_FB_SIZE];

// Singleton instance
wasm_framebuffer_obj_t wasm_framebuffer_instance;

// ---- Framebuffer protocol implementation ----

static void wasm_fb_get_bufinfo(mp_obj_t self_in, mp_buffer_info_t *bufinfo) {
    wasm_framebuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    bufinfo->buf = self->buffer;
    bufinfo->len = self->width * self->height * WASM_DISPLAY_BPP;
    bufinfo->typecode = 'B';
}

static void wasm_fb_swapbuffers(mp_obj_t self_in, uint8_t *dirty_row_bitmask) {
    wasm_framebuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)dirty_row_bitmask;
    // Just increment the frame counter. The host polls this to know
    // when new frame data is available, then reads wasm_display_buffer
    // directly from WASM linear memory.
    self->frame_count++;
}

static void wasm_fb_deinit(mp_obj_t self_in) {
    // Static buffer — nothing to free
    (void)self_in;
}

static int wasm_fb_get_width(mp_obj_t self_in) {
    wasm_framebuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return self->width;
}

static int wasm_fb_get_height(mp_obj_t self_in) {
    wasm_framebuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return self->height;
}

static int wasm_fb_get_color_depth(mp_obj_t self_in) {
    (void)self_in;
    return 16;  // RGB565
}

static int wasm_fb_get_bytes_per_cell(mp_obj_t self_in) {
    (void)self_in;
    return 2;
}

static int wasm_fb_get_native_fps(mp_obj_t self_in) {
    (void)self_in;
    return 1000;  // No throttle — browser rAF paces frames
}

static bool wasm_fb_get_grayscale(mp_obj_t self_in) {
    (void)self_in;
    return false;
}

// ---- Protocol struct ----

static const framebuffer_p_t wasm_framebuffer_proto = {
    MP_PROTO_IMPLEMENT(MP_QSTR_protocol_framebuffer)
    .get_bufinfo = wasm_fb_get_bufinfo,
    .swapbuffers = wasm_fb_swapbuffers,
    .deinit = wasm_fb_deinit,
    .get_width = wasm_fb_get_width,
    .get_height = wasm_fb_get_height,
    .get_color_depth = wasm_fb_get_color_depth,
    .get_bytes_per_cell = wasm_fb_get_bytes_per_cell,
    .get_native_frames_per_second = wasm_fb_get_native_fps,
    .get_grayscale = wasm_fb_get_grayscale,
};

// ---- Type definition ----

MP_DEFINE_CONST_OBJ_TYPE(
    wasm_framebuffer_type,
    MP_QSTR_WasmFramebuffer,
    MP_TYPE_FLAG_NONE,
    protocol, &wasm_framebuffer_proto
    );

// ---- Initialization ----

void wasm_framebuffer_init(void) {
    wasm_framebuffer_instance.base.type = &wasm_framebuffer_type;
    wasm_framebuffer_instance.width = WASM_DISPLAY_WIDTH;
    wasm_framebuffer_instance.height = WASM_DISPLAY_HEIGHT;
    wasm_framebuffer_instance.buffer = wasm_display_buffer;
    wasm_framebuffer_instance.frame_count = 0;

    // Clear to black
    memset(wasm_display_buffer, 0, WASM_DISPLAY_FB_SIZE);
}

// ---- Exported WASM functions for host access ----
// These let the browser host discover the framebuffer address and
// dimensions by calling into the WASM instance.

__attribute__((export_name("wasm_display_fb_addr")))
uint8_t *wasm_display_fb_addr(void) {
    return wasm_display_buffer;
}

__attribute__((export_name("wasm_display_fb_width")))
int wasm_display_fb_width(void) {
    return WASM_DISPLAY_WIDTH;
}

__attribute__((export_name("wasm_display_fb_height")))
int wasm_display_fb_height(void) {
    return WASM_DISPLAY_HEIGHT;
}

__attribute__((export_name("wasm_display_frame_count")))
uint32_t wasm_display_frame_count(void) {
    return wasm_framebuffer_instance.frame_count;
}

/* Address of the frame counter in linear memory.
 * JS polls this directly (no WASM call) to check if the
 * framebuffer has new data before doing the RGB565→RGBA
 * conversion.  Same direct-memory pattern as sh_state_addr(). */
__attribute__((export_name("wasm_display_frame_count_addr")))
uintptr_t wasm_display_frame_count_addr(void) {
    return (uintptr_t)&wasm_framebuffer_instance.frame_count;
}

// ---- Cursor info for JS-side rendering ----
//
// JS draws the cursor on the canvas as an overlay — no framebuffer
// XOR, no burn-in.  This struct is filled by wasm_cursor_info_update()
// (called from cp_step) and read by JS via direct memory access.

#if CIRCUITPY_TERMINALIO
#include "shared-bindings/terminalio/Terminal.h"
#include "supervisor/shared/display.h"
#endif

static wasm_cursor_info_t _cursor_info;

__attribute__((export_name("wasm_cursor_info_addr")))
uintptr_t wasm_cursor_info_addr(void) {
    return (uintptr_t)&_cursor_info;
}

void wasm_cursor_info_update(void) {
    #if CIRCUITPY_TERMINALIO
    _cursor_info.cursor_x = common_hal_terminalio_terminal_get_cursor_x(&supervisor_terminal);
    _cursor_info.cursor_y = common_hal_terminalio_terminal_get_cursor_y(&supervisor_terminal);

    extern displayio_tilegrid_t supervisor_terminal_scroll_area_text_grid;
    displayio_tilegrid_t *sa = &supervisor_terminal_scroll_area_text_grid;
    _cursor_info.scroll_x = sa->x;
    _cursor_info.scroll_y = sa->y;
    _cursor_info.top_left_y = sa->top_left_y;
    _cursor_info.height_tiles = sa->height_in_tiles;
    _cursor_info.glyph_w = sa->tile_width;
    _cursor_info.glyph_h = sa->tile_height;

    extern displayio_group_t circuitpython_splash;
    _cursor_info.scale = circuitpython_splash.scale;
    #endif
}
