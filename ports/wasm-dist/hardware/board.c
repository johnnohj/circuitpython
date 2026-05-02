// board.c — unified hardware simulation + SDL2 renderer
//
// Single Emscripten binary that:
//   1. Manages hardware state (GPIO, NeoPixel, analog, display)
//   2. Renders the board layout via SDL2
//   3. Handles mouse interaction (hit-test, hover, press)
//
// The wrapping board.mjs handles:
//   - Definition.json parsing → C layout calls
//   - Sync bus communication (receives state, sends interactions)
//   - Message protocol aligned with Wippersnapper Protobuf
//
// Build with Emscripten:
//   emcc board.c -o board.js -s USE_SDL=2 -s MODULARIZE=1 ...

#include <SDL2/SDL.h>
#include <emscripten.h>
#include <string.h>
#include <math.h>

// ── Limits ──

#define MAX_PINS       64
#define MAX_NEOPIXELS  64
#define MAX_NAME_LEN   16

// ── Pin categories (match Wippersnapper signal types) ──

#define CAT_UNKNOWN    0
#define CAT_DIGITAL    1
#define CAT_ANALOG     2
#define CAT_I2C        3
#define CAT_SPI        4
#define CAT_BUTTON     5
#define CAT_LED        6
#define CAT_NEOPIXEL   7

// ── GPIO slot layout ──
//   [0] enabled   [1] direction  [2] value   [3] pull
//   [4] flags     [5] category   [6] role    [7] latched
//   [8..11] reserved
#define GPIO_SLOT_SIZE   12
#define GPIO_MAX_PINS    32
#define OFF_ENABLED      0
#define OFF_DIRECTION    1
#define OFF_VALUE        2
#define OFF_PULL         3
#define OFF_FLAGS        4
#define OFF_CATEGORY     5

// ── Pin visual state ──

typedef struct {
    int    x, y;
    int    radius;
    int    category;
    char   name[MAX_NAME_LEN];
    int    hover;
    int    pressed;
} pin_layout_t;

// ── NeoPixel layout ──

typedef struct {
    int x, y;
    int radius;
} neopixel_layout_t;

// ── Display region ──

typedef struct {
    int x, y, w, h;
    int fb_width, fb_height;
    SDL_Texture *texture;
    int dirty;
} display_t;

// ── Board ──

static struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    int           width, height;

    // Layout (positions, names — set once from definition.json)
    pin_layout_t     pin_layout[MAX_PINS];
    int              pin_count;
    neopixel_layout_t neo_layout[MAX_NEOPIXELS];
    int              neo_count;

    // State (updated each frame from sync bus)
    uint8_t gpio[GPIO_MAX_PINS * GPIO_SLOT_SIZE];
    uint8_t neopixel_data[4 + MAX_NEOPIXELS * 4];  // header + RGBW

    // Display
    display_t display;

    // Background
    uint8_t bg_r, bg_g, bg_b;
} board;


// ── Init ──

EMSCRIPTEN_KEEPALIVE
void board_init(int width, int height, int bg_r, int bg_g, int bg_b) {
    SDL_Init(SDL_INIT_VIDEO);
    board.width = width;
    board.height = height;
    board.bg_r = bg_r;
    board.bg_g = bg_g;
    board.bg_b = bg_b;

    board.window = SDL_CreateWindow("Board",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, 0);
    board.renderer = SDL_CreateRenderer(board.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    memset(board.gpio, 0, sizeof(board.gpio));
    memset(board.neopixel_data, 0, sizeof(board.neopixel_data));
}


// ── Layout (from definition.json via board.mjs) ──

EMSCRIPTEN_KEEPALIVE
void board_add_pin(int index, int x, int y, int radius, int category,
                   const char *name) {
    if (index < 0 || index >= MAX_PINS) return;
    pin_layout_t *p = &board.pin_layout[index];
    p->x = x;
    p->y = y;
    p->radius = radius > 0 ? radius : 8;
    p->category = category;
    p->hover = 0;
    p->pressed = 0;
    strncpy(p->name, name ? name : "", MAX_NAME_LEN - 1);
    p->name[MAX_NAME_LEN - 1] = '\0';
    if (index >= board.pin_count) board.pin_count = index + 1;
}

EMSCRIPTEN_KEEPALIVE
void board_add_neopixel(int index, int x, int y, int radius) {
    if (index < 0 || index >= MAX_NEOPIXELS) return;
    neopixel_layout_t *n = &board.neo_layout[index];
    n->x = x;
    n->y = y;
    n->radius = radius > 0 ? radius : 6;
    if (index >= board.neo_count) board.neo_count = index + 1;
}

EMSCRIPTEN_KEEPALIVE
void board_init_display(int x, int y, int w, int h, int fb_w, int fb_h) {
    board.display.x = x;
    board.display.y = y;
    board.display.w = w;
    board.display.h = h;
    board.display.fb_width = fb_w;
    board.display.fb_height = fb_h;
    board.display.dirty = 0;
    board.display.texture = SDL_CreateTexture(board.renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        fb_w, fb_h);
}


// ── State updates (bulk from sync bus) ──

EMSCRIPTEN_KEEPALIVE
void board_update_gpio(const uint8_t *data, int len) {
    int n = len < (int)sizeof(board.gpio) ? len : (int)sizeof(board.gpio);
    memcpy(board.gpio, data, n);
}

EMSCRIPTEN_KEEPALIVE
void board_update_neopixel(const uint8_t *data, int len) {
    int n = len < (int)sizeof(board.neopixel_data) ? len : (int)sizeof(board.neopixel_data);
    memcpy(board.neopixel_data, data, n);
}

EMSCRIPTEN_KEEPALIVE
void board_update_framebuffer(const uint8_t *data, int len) {
    if (!board.display.texture) return;
    SDL_UpdateTexture(board.display.texture, NULL, data,
        board.display.fb_width * 2);
    board.display.dirty = 1;
}

EMSCRIPTEN_KEEPALIVE
void board_reset_state(void) {
    memset(board.gpio, 0, sizeof(board.gpio));
    memset(board.neopixel_data, 0, sizeof(board.neopixel_data));
}


// ── Drawing helpers ──

static void fill_circle(SDL_Renderer *r, int cx, int cy, int rad) {
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = (int)sqrt(rad * rad - dy * dy);
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void outline_circle(SDL_Renderer *r, int cx, int cy, int rad) {
    int x = rad, y = 0, err = 1 - rad;
    while (x >= y) {
        SDL_RenderDrawPoint(r, cx + x, cy + y);
        SDL_RenderDrawPoint(r, cx + y, cy + x);
        SDL_RenderDrawPoint(r, cx - y, cy + x);
        SDL_RenderDrawPoint(r, cx - x, cy + y);
        SDL_RenderDrawPoint(r, cx - x, cy - y);
        SDL_RenderDrawPoint(r, cx - y, cy - x);
        SDL_RenderDrawPoint(r, cx + y, cy - x);
        SDL_RenderDrawPoint(r, cx + x, cy - y);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

static void pin_color(int index, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t *s = &board.gpio[index * GPIO_SLOT_SIZE];
    int enabled = s[OFF_ENABLED];
    int direction = s[OFF_DIRECTION];
    int value = s[OFF_VALUE];
    int category = board.pin_layout[index].category;

    if (!enabled) { *r = 60; *g = 60; *b = 60; return; }

    if (category == CAT_BUTTON) {
        if (value) { *r = 80; *g = 120; *b = 80; }
        else       { *r = 50; *g = 200; *b = 50; }
        return;
    }
    if (direction == 0) {
        *r = value ? 200 : 100; *g = value ? 160 : 80; *b = value ? 50 : 30;
    } else {
        *r = value ? 50 : 30; *g = value ? 120 : 60; *b = value ? 220 : 110;
    }
}


// ── Render ──

EMSCRIPTEN_KEEPALIVE
void board_render(void) {
    SDL_Renderer *r = board.renderer;

    SDL_SetRenderDrawColor(r, board.bg_r, board.bg_g, board.bg_b, 255);
    SDL_RenderClear(r);

    // Display
    if (board.display.texture) {
        SDL_Rect dst = { board.display.x, board.display.y,
                         board.display.w, board.display.h };
        SDL_RenderCopy(r, board.display.texture, NULL, &dst);
        SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
        SDL_RenderDrawRect(r, &dst);
    }

    // Pins
    for (int i = 0; i < board.pin_count; i++) {
        pin_layout_t *p = &board.pin_layout[i];
        if (p->radius <= 0) continue;

        uint8_t cr, cg, cb;
        pin_color(i, &cr, &cg, &cb);

        if (p->hover) {
            cr = cr + (255 - cr) / 3;
            cg = cg + (255 - cg) / 3;
            cb = cb + (255 - cb) / 3;
        }

        SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
        fill_circle(r, p->x, p->y, p->radius);

        uint8_t *s = &board.gpio[i * GPIO_SLOT_SIZE];
        SDL_SetRenderDrawColor(r, s[OFF_ENABLED] ? 200 : 80,
            s[OFF_ENABLED] ? 200 : 80, s[OFF_ENABLED] ? 200 : 80, 255);
        outline_circle(r, p->x, p->y, p->radius);

        if (p->pressed) {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            outline_circle(r, p->x, p->y, p->radius + 2);
        }
    }

    // NeoPixels
    uint8_t *nd = board.neopixel_data;
    int neo_enabled = nd[1];
    int neo_bytes = nd[2] | (nd[3] << 8);
    int bpp = 3;
    int neo_count = neo_enabled ? neo_bytes / bpp : 0;

    for (int i = 0; i < board.neo_count; i++) {
        neopixel_layout_t *n = &board.neo_layout[i];
        if (n->radius <= 0) continue;

        uint8_t pr = 0, pg = 0, pb = 0;
        if (i < neo_count) {
            int base = 4 + i * bpp;
            pg = nd[base]; pr = nd[base + 1]; pb = nd[base + 2];  // GRB
        }

        // Glow
        if (pr + pg + pb > 0) {
            SDL_SetRenderDrawColor(r, pr / 4, pg / 4, pb / 4, 255);
            fill_circle(r, n->x, n->y, n->radius + 4);
        }

        SDL_SetRenderDrawColor(r, pr, pg, pb, 255);
        fill_circle(r, n->x, n->y, n->radius);

        SDL_SetRenderDrawColor(r, 100, 100, 100, 255);
        outline_circle(r, n->x, n->y, n->radius);
    }

    SDL_RenderPresent(r);
}


// ── Hit testing ──

EMSCRIPTEN_KEEPALIVE
int board_hit_test(int x, int y) {
    for (int i = 0; i < board.pin_count; i++) {
        pin_layout_t *p = &board.pin_layout[i];
        if (p->radius <= 0) continue;
        int dx = x - p->x, dy = y - p->y;
        if (dx * dx + dy * dy <= (p->radius + 2) * (p->radius + 2))
            return i;
    }
    return -1;
}

EMSCRIPTEN_KEEPALIVE
void board_set_hover(int pin) {
    for (int i = 0; i < board.pin_count; i++)
        board.pin_layout[i].hover = (i == pin);
}

EMSCRIPTEN_KEEPALIVE
void board_set_pressed(int pin, int pressed) {
    if (pin >= 0 && pin < MAX_PINS)
        board.pin_layout[pin].pressed = pressed;
}

EMSCRIPTEN_KEEPALIVE
const char *board_get_pin_name(int index) {
    if (index < 0 || index >= MAX_PINS) return "";
    return board.pin_layout[index].name;
}

EMSCRIPTEN_KEEPALIVE
int board_get_pin_category(int index) {
    if (index < 0 || index >= MAX_PINS) return CAT_UNKNOWN;
    return board.pin_layout[index].category;
}
