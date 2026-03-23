/*
 * worker_terminal.c — Clean terminal for the hardware worker.
 *
 * Reuses the supervisor's display infrastructure (FramebufferDisplay,
 * root group, scroll area TileGrid) but manages cursor/scroll ourselves.
 * All stdout goes through worker_terminal_write — no VT100, no cooked
 * splitting, no MP_PYTHON_PRINTER confusion.
 *
 * Layout (320×240, 6×12 font, scale 1):
 *   Row 0:     status bar (1 tile high, unused)
 *   Row 1..19: scroll area (19 rows × 53 cols)
 */

#include "worker_terminal.h"
#include "wasm_framebuffer.h"
#include "board_display.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "py/runtime.h"
#include "shared-bindings/framebufferio/FramebufferDisplay.h"
#include "shared-bindings/displayio/TileGrid.h"
#include "shared-module/displayio/__init__.h"
#include "supervisor/shared/display.h"

/* ── Extern references to supervisor display objects ─────────────── */

extern displayio_tilegrid_t supervisor_terminal_scroll_area_text_grid;
extern displayio_group_t circuitpython_splash;
#if CIRCUITPY_REPL_LOGO
extern displayio_tilegrid_t supervisor_blinka_sprite;
#endif

/* ── Constants ──────────────────────────────────────────────────── */

#define GLYPH_W         6
#define GLYPH_H         12
#define FIRST_GLYPH     0x20

/* ── State ──────────────────────────────────────────────────────── */

bool worker_terminal_dirty = false;

static framebufferio_framebufferdisplay_obj_t *_display;
static displayio_tilegrid_t *_tg;   /* points to supervisor's scroll area */
static uint16_t _cols;
static uint16_t _rows;

/* Cursor in logical coordinates (0-based within scroll area). */
static uint16_t _cur_x = 0;
static uint16_t _cur_y = 0;

/* Ring buffer head — physical row that is logical row 0. */
static uint16_t _top_y = 0;

/* Banner indent: columns to skip on line 1 (next to Blinka). */
static uint16_t _banner_indent = 0;
static bool _banner_line_active = false;

/* ── Helpers ────────────────────────────────────────────────────── */

static inline uint16_t phys_y(uint16_t logical) {
    return (logical + _top_y) % _rows;
}

static void set_tile(uint16_t x, uint16_t y, uint8_t glyph) {
    common_hal_displayio_tilegrid_set_tile(_tg, x, phys_y(y), glyph);
}

static uint8_t glyph_for(char c) {
    if (c < FIRST_GLYPH || c > 0x7e) {
        return 0;
    }
    return (uint8_t)(c - FIRST_GLYPH);
}

static void clear_row(uint16_t y) {
    uint16_t py = phys_y(y);
    for (uint16_t x = 0; x < _cols; x++) {
        common_hal_displayio_tilegrid_set_tile(_tg, x, py, 0);
    }
}

static void scroll_up(void) {
    _top_y = (_top_y + 1) % _rows;
    clear_row(_rows - 1);
    common_hal_displayio_tilegrid_set_top_left(_tg, 0, _top_y);
}

/* ── Public API ─────────────────────────────────────────────────── */

void worker_terminal_init(void) {
    /* 1. board_display_init sets up framebuffer + FramebufferDisplay +
     *    supervisor terminal (scroll area TileGrid, Blinka, etc). */
    board_display_init();

    _display = &displays[0].framebuffer_display;

    /* 2. Fix the splash group: scale 1 (not 2) for our 320×240 display.
     *    Also force a full re-render of all children. */
    circuitpython_splash.scale = 1;

    /* 3. Grab the supervisor's scroll area TileGrid.
     *    supervisor_start_terminal() already allocated tiles, set
     *    dimensions, and added it to the root group. We just write to it. */
    _tg = &supervisor_terminal_scroll_area_text_grid;
    _cols = _tg->width_in_tiles;
    _rows = _tg->height_in_tiles;

    /* 4. Reset the scroll area: clear all tiles, reset top_left_y. */
    common_hal_displayio_tilegrid_set_top_left(_tg, 0, 0);
    for (uint16_t y = 0; y < _rows; y++) {
        for (uint16_t x = 0; x < _cols; x++) {
            common_hal_displayio_tilegrid_set_tile(_tg, x, y, 0);
        }
    }

    _cur_x = 0;
    _cur_y = 0;
    _top_y = 0;

    #if CIRCUITPY_REPL_LOGO
    {
        /* Position Blinka on scroll area line 1 (where banner text starts
         * after the initial \r\n). Indent the banner text to its right. */
        extern displayio_palette_t blinka_palette;
        supervisor_blinka_sprite.x = 0;
        supervisor_blinka_sprite.y = _tg->y;  /* line 0 of scroll area */
        supervisor_blinka_sprite.top_left_x = 0;
        supervisor_blinka_sprite.top_left_y = 0;
        supervisor_blinka_sprite.full_change = true;
        supervisor_blinka_sprite.moved = true;
        blinka_palette.needs_refresh = true;

        /* Update the group transform so the new position takes effect. */
        displayio_group_update_transform(&circuitpython_splash,
            &_display->core.transform);

        _banner_indent = (supervisor_blinka_sprite.pixel_width / GLYPH_W) + 1;
    }
    #endif

    worker_terminal_dirty = true;
    fprintf(stderr, "[worker_terminal] %dx%d tiles, scale=1\n", _cols, _rows);

}

void worker_terminal_write(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = str[i];

        if (c == '\r') {
            _cur_x = 0;
        } else if (c == '\n') {
            /* Absorb the first \n so banner stays on line 0 next to Blinka. */
            if (_banner_indent > 0 && !_banner_line_active && _cur_y == 0) {
                _cur_x = _banner_indent;
                _banner_line_active = true;
                continue;  /* don't advance _cur_y */
            }
            _cur_y++;
            if (_cur_y >= _rows) {
                _cur_y = _rows - 1;
                scroll_up();
            }
        } else if (c == '\b') {
            if (_cur_x > 0) {
                _cur_x--;
            }
        } else if (c == '\t') {
            uint16_t next = (_cur_x + 4) & ~3u;
            if (next > _cols) next = _cols;
            while (_cur_x < next) {
                set_tile(_cur_x, _cur_y, 0);
                _cur_x++;
            }
            if (_cur_x >= _cols) {
                _cur_x = 0;
                _cur_y++;
                if (_cur_y >= _rows) {
                    _cur_y = _rows - 1;
                    scroll_up();
                }
            }
        } else if (c >= 0x20 && c <= 0x7e) {
            set_tile(_cur_x, _cur_y, glyph_for(c));
            _cur_x++;
            if (_cur_x >= _cols) {
                _cur_x = 0;
                _cur_y++;
                if (_cur_y >= _rows) {
                    _cur_y = _rows - 1;
                    scroll_up();
                }
            }
        }
        /* Ignore other control chars. */
    }

    _tg->full_change = true;
    worker_terminal_dirty = true;
}

void worker_terminal_refresh(void) {
    if (_display == NULL) {
        return;
    }
    _display->core.full_refresh = true;
    common_hal_framebufferio_framebufferdisplay_refresh(_display, 0, 0);
}

void worker_terminal_flush(void) {
    uint8_t *fb_addr = wasm_display_fb_addr();
    int w = wasm_display_fb_width();
    int h = wasm_display_fb_height();
    size_t fb_size = (size_t)w * h * 2;

    int fd = open("/hw/display/fb", O_WRONLY | O_CREAT, 0666);
    if (fd >= 0) {
        lseek(fd, 0, SEEK_SET);
        write(fd, fb_addr, fb_size);
        close(fd);
    }
}
