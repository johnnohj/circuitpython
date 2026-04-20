/*
 * board_display.c — Initialize the WASM framebuffer display
 *
 * Creates a FramebufferDisplay backed by the WasmFramebuffer and
 * sets it as the primary display. The supervisor then automatically
 * creates a terminalio terminal on it, complete with Blinka logo
 * and the built-in font.
 *
 * Call board_display_init() after mp_init() and GC setup.
 */

#include "board_display.h"
#include "wasm_framebuffer.h"

#include "py/runtime.h"
#include "shared-bindings/framebufferio/FramebufferDisplay.h"
#include "shared-module/displayio/__init__.h"
#include "supervisor/shared/display.h"

void board_display_init(void) {
    // 1. Initialize the framebuffer singleton (clears buffer to black)
    wasm_framebuffer_init();

    // 2. Allocate a display slot from the fixed display array
    primary_display_t *disp = allocate_display_or_raise();
    framebufferio_framebufferdisplay_obj_t *display = &disp->framebuffer_display;
    display->base.type = &framebufferio_framebufferdisplay_type;

    // 3. Construct the FramebufferDisplay.
    //    This calls displayio_display_core_construct() internally,
    //    which calls supervisor_start_terminal(width, height) to
    //    create the terminal with the built-in font and Blinka logo.
    common_hal_framebufferio_framebufferdisplay_construct(
        display,
        MP_OBJ_FROM_PTR(&wasm_framebuffer_instance),
        0,      // rotation
        true    // auto_refresh
    );

    // 4. Set as primary display (board.DISPLAY)
    common_hal_displayio_set_primary_display(MP_OBJ_FROM_PTR(display));

    // 5. Reset scroll area top_left_y to 0 so text starts at the top.
    //    The Terminal constructor sets top_left_y=1 for the ring buffer,
    //    but on an empty terminal that puts the first line at the bottom.
    extern displayio_tilegrid_t supervisor_terminal_scroll_area_text_grid;
    common_hal_displayio_tilegrid_set_top_left(
        &supervisor_terminal_scroll_area_text_grid, 0, 0);
}

__attribute__((export_name("cp_display_refresh")))
void board_display_refresh(void) {
    // Force a manual refresh of the primary display, bypassing the
    // native_ms_per_frame rate limit. Called after terminal writes
    // to ensure the framebuffer is updated immediately.
    mp_obj_t primary = common_hal_displayio_get_primary_display();
    if (primary != mp_const_none) {
        framebufferio_framebufferdisplay_obj_t *display =
            (framebufferio_framebufferdisplay_obj_t *)MP_OBJ_TO_PTR(primary);
        common_hal_framebufferio_framebufferdisplay_refresh(display, 0, 0);
    }
}
