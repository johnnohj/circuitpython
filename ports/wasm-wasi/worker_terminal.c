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

#if CIRCUITPY_TERMINALIO
#include "shared-module/terminalio/Terminal.h"
#include "shared-bindings/terminalio/Terminal.h"
#endif

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

/* Cursor blink state. */
static bool _cursor_visible = true;
static uint32_t _cursor_last_toggle_ms = 0;
#define CURSOR_BLINK_MS 500

/* VT100 escape sequence parser state. */
enum {
    ESC_NONE = 0,   /* normal character processing */
    ESC_ESC,        /* received ESC (0x1b) */
    ESC_CSI,        /* received ESC [ — collecting params */
};
static int _esc_state = ESC_NONE;
static int _esc_params[4];
static int _esc_param_count = 0;

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

/* ── Cursor rendering ───────────────────────────────────────────── */

/* XOR a 6×12 block at the cursor position in the RGB565 framebuffer.
 * Calling twice restores the original pixels (XOR is self-inverse). */
static void cursor_xor(void) {
    uint8_t *fb = wasm_display_fb_addr();
    int fb_w = wasm_display_fb_width();
    int fb_h = wasm_display_fb_height();

    /* Read cursor position from the supervisor terminal. */
    #if CIRCUITPY_TERMINALIO
    extern terminalio_terminal_obj_t supervisor_terminal;
    uint16_t cx = common_hal_terminalio_terminal_get_cursor_x(&supervisor_terminal);
    uint16_t cy = common_hal_terminalio_terminal_get_cursor_y(&supervisor_terminal);
    #else
    uint16_t cx = _cur_x;
    uint16_t cy = _cur_y;
    #endif

    /* Pixel origin of the cursor cell. */
    int px = _tg->x + cx * GLYPH_W;
    int py = _tg->y + cy * GLYPH_H;

    for (int row = 0; row < GLYPH_H; row++) {
        int y = py + row;
        if (y < 0 || y >= fb_h) continue;
        for (int col = 0; col < GLYPH_W; col++) {
            int x = px + col;
            if (x < 0 || x >= fb_w) continue;
            int off = (y * fb_w + x) * 2;
            fb[off]     ^= 0xFF;
            fb[off + 1] ^= 0xFF;
        }
    }
}

void worker_terminal_cursor_tick(uint32_t now_ms) {
    if (now_ms - _cursor_last_toggle_ms >= CURSOR_BLINK_MS) {
        _cursor_last_toggle_ms = now_ms;
        _cursor_visible = !_cursor_visible;
        worker_terminal_dirty = true;
    }
}

/* Handle a CSI (ESC [) sequence finale character. */
static void _handle_csi(char final) {
    int p0 = (_esc_param_count > 0) ? _esc_params[0] : 0;
    int p1 = (_esc_param_count > 1) ? _esc_params[1] : 0;

    switch (final) {
    case 'A': /* CUU — cursor up */
        if (p0 == 0) p0 = 1;
        _cur_y = (_cur_y >= (uint16_t)p0) ? _cur_y - p0 : 0;
        break;
    case 'B': /* CUD — cursor down */
        if (p0 == 0) p0 = 1;
        _cur_y += p0;
        if (_cur_y >= _rows) _cur_y = _rows - 1;
        break;
    case 'C': /* CUF — cursor forward */
        if (p0 == 0) p0 = 1;
        _cur_x += p0;
        if (_cur_x >= _cols) _cur_x = _cols - 1;
        break;
    case 'D': /* CUB — cursor back */
        if (p0 == 0) p0 = 1;
        _cur_x = (_cur_x >= (uint16_t)p0) ? _cur_x - p0 : 0;
        break;
    case 'H': /* CUP — cursor position (row;col, 1-based) */
    case 'f':
        _cur_y = (p0 > 0) ? p0 - 1 : 0;
        _cur_x = (p1 > 0) ? p1 - 1 : 0;
        if (_cur_y >= _rows) _cur_y = _rows - 1;
        if (_cur_x >= _cols) _cur_x = _cols - 1;
        break;
    case 'J': /* ED — erase in display */
        if (p0 == 0) {
            /* Clear from cursor to end of screen */
            for (uint16_t x = _cur_x; x < _cols; x++) set_tile(x, _cur_y, 0);
            for (uint16_t y = _cur_y + 1; y < _rows; y++) clear_row(y);
        } else if (p0 == 1) {
            /* Clear from start to cursor */
            for (uint16_t y = 0; y < _cur_y; y++) clear_row(y);
            for (uint16_t x = 0; x <= _cur_x; x++) set_tile(x, _cur_y, 0);
        } else if (p0 == 2) {
            /* Clear entire screen */
            for (uint16_t y = 0; y < _rows; y++) clear_row(y);
            _cur_x = 0;
            _cur_y = 0;
        }
        break;
    case 'K': /* EL — erase in line */
        if (p0 == 0) {
            /* Clear from cursor to end of line */
            for (uint16_t x = _cur_x; x < _cols; x++) set_tile(x, _cur_y, 0);
        } else if (p0 == 1) {
            /* Clear from start of line to cursor */
            for (uint16_t x = 0; x <= _cur_x; x++) set_tile(x, _cur_y, 0);
        } else if (p0 == 2) {
            /* Clear entire line */
            for (uint16_t x = 0; x < _cols; x++) set_tile(x, _cur_y, 0);
        }
        break;
    default:
        /* Unrecognized CSI sequence — silently ignore */
        break;
    }
}

void worker_terminal_write(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = str[i];

        /* ── VT100 escape sequence state machine ── */
        if (_esc_state == ESC_ESC) {
            if (c == '[') {
                _esc_state = ESC_CSI;
                _esc_param_count = 0;
                memset(_esc_params, 0, sizeof(_esc_params));
            } else {
                /* Non-CSI escape (e.g. ESC c for reset) — ignore for now */
                _esc_state = ESC_NONE;
            }
            continue;
        }
        if (_esc_state == ESC_CSI) {
            if (c >= '0' && c <= '9') {
                /* Accumulate parameter digit */
                if (_esc_param_count == 0) _esc_param_count = 1;
                _esc_params[_esc_param_count - 1] =
                    _esc_params[_esc_param_count - 1] * 10 + (c - '0');
            } else if (c == ';') {
                /* Next parameter */
                if (_esc_param_count < 4) _esc_param_count++;
            } else if (c >= 0x40 && c <= 0x7e) {
                /* Final byte — dispatch the command */
                _handle_csi(c);
                _esc_state = ESC_NONE;
            } else {
                /* Unexpected byte — abort sequence */
                _esc_state = ESC_NONE;
            }
            continue;
        }

        /* ── Normal character processing ── */
        if (c == '\x1b') {
            _esc_state = ESC_ESC;
        } else if (c == '\r') {
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

    /* Reset cursor blink so it's visible right after typing. */
    _cursor_visible = true;
    _cursor_last_toggle_ms = 0;
}

void worker_terminal_refresh(void) {
    if (_display == NULL) {
        return;
    }
    _display->core.full_refresh = true;
    common_hal_framebufferio_framebufferdisplay_refresh(_display, 0, 0);

    /* Draw cursor on top of the composited framebuffer. */
    if (_cursor_visible) {
        cursor_xor();
    }
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
