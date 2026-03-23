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
    return 30;  // Browser refresh rate target
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
