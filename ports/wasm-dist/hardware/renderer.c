// renderer.c — Emscripten SDL2 board renderer (reactor pattern)
//
// No main(). The wrapping .mjs calls exported functions to:
//   1. Initialize with board dimensions
//   2. Define pin/component positions (from definition.json)
//   3. Update state each frame (GPIO, NeoPixel, framebuffer)
//   4. Render the board
//   5. Hit-test mouse positions
//
// Outputs nothing directly — the .mjs reads interaction state
// and posts to the parent page / sync bus.

#include <SDL2/SDL.h>
#include <emscripten.h>
#include <string.h>
#include <math.h>

// ── Board layout limits ──

#define MAX_PINS       64
#define MAX_NEOPIXELS  64
#define MAX_NAME_LEN   16

// ── Pin categories (match ports/wasm convention) ──

#define CAT_UNKNOWN    0
#define CAT_DIGITAL    1
#define CAT_ANALOG     2
#define CAT_I2C        3
#define CAT_SPI        4
#define CAT_BUTTON     5
#define CAT_LED        6
#define CAT_NEOPIXEL   7

// ── Pin visual state ──

typedef struct {
    int    x, y;            // position on board
    int    radius;          // visual size
    int    category;        // CAT_*
    int    enabled;         // claimed by code
    int    direction;       // 0=input, 1=output
    int    value;           // current value
    int    pull;            // 0=none, 1=up, 2=down
    char   name[MAX_NAME_LEN];
    int    hover;           // mouse is over this pin
    int    pressed;         // mouse is pressing this pin
} pin_t;

// ── NeoPixel state ──

typedef struct {
    int x, y;
    int radius;
    uint8_t r, g, b;
} neopixel_t;

// ── Display region (built-in screen) ──

typedef struct {
    int x, y, w, h;        // position/size on board
    int fb_width, fb_height;
    uint16_t *framebuffer;  // RGB565, allocated
    SDL_Texture *texture;   // updated from framebuffer
    int dirty;
} display_t;

// ── Board state ──

static struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    int           width, height;

    pin_t         pins[MAX_PINS];
    int           pin_count;

    neopixel_t    neopixels[MAX_NEOPIXELS];
    int           neopixel_count;

    display_t     display;

    // Interaction output — .mjs polls this
    int           hover_pin;     // -1 = none
    int           click_pin;     // -1 = none
    int           click_x, click_y;
    int           interaction_pending;

    // Board background color
    uint8_t       bg_r, bg_g, bg_b;
} board;


// ── Exported: Initialize ──

EMSCRIPTEN_KEEPALIVE
void hw_renderer_init(int width, int height, int bg_r, int bg_g, int bg_b) {
    SDL_Init(SDL_INIT_VIDEO);

    board.width = width;
    board.height = height;
    board.bg_r = bg_r;
    board.bg_g = bg_g;
    board.bg_b = bg_b;
    board.hover_pin = -1;
    board.click_pin = -1;

    board.window = SDL_CreateWindow("Board",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, 0);
    board.renderer = SDL_CreateRenderer(board.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
}


// ── Exported: Define board elements ──

EMSCRIPTEN_KEEPALIVE
void hw_add_pin(int index, int x, int y, int radius, int category,
                const char *name) {
    if (index < 0 || index >= MAX_PINS) return;
    pin_t *p = &board.pins[index];
    p->x = x;
    p->y = y;
    p->radius = radius > 0 ? radius : 8;
    p->category = category;
    p->enabled = 0;
    p->direction = 0;
    p->value = 0;
    p->pull = 0;
    p->hover = 0;
    p->pressed = 0;
    strncpy(p->name, name ? name : "", MAX_NAME_LEN - 1);
    p->name[MAX_NAME_LEN - 1] = '\0';
    if (index >= board.pin_count) board.pin_count = index + 1;
}

EMSCRIPTEN_KEEPALIVE
void hw_add_neopixel(int index, int x, int y, int radius) {
    if (index < 0 || index >= MAX_NEOPIXELS) return;
    neopixel_t *n = &board.neopixels[index];
    n->x = x;
    n->y = y;
    n->radius = radius > 0 ? radius : 6;
    n->r = n->g = n->b = 0;
    if (index >= board.neopixel_count) board.neopixel_count = index + 1;
}

EMSCRIPTEN_KEEPALIVE
void hw_init_display(int x, int y, int w, int h, int fb_width, int fb_height) {
    board.display.x = x;
    board.display.y = y;
    board.display.w = w;
    board.display.h = h;
    board.display.fb_width = fb_width;
    board.display.fb_height = fb_height;
    board.display.dirty = 0;

    // Allocate framebuffer
    int pixels = fb_width * fb_height;
    board.display.framebuffer = (uint16_t *)malloc(pixels * 2);
    memset(board.display.framebuffer, 0, pixels * 2);

    // Create streaming texture for efficient updates
    board.display.texture = SDL_CreateTexture(board.renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        fb_width, fb_height);
}


// ── Exported: Update state (called by .mjs from sync bus data) ──

EMSCRIPTEN_KEEPALIVE
void hw_set_pin_state(int index, int enabled, int direction, int value, int pull) {
    if (index < 0 || index >= MAX_PINS) return;
    pin_t *p = &board.pins[index];
    p->enabled = enabled;
    p->direction = direction;
    p->value = value;
    p->pull = pull;
}

EMSCRIPTEN_KEEPALIVE
void hw_set_neopixel_rgb(int index, int r, int g, int b) {
    if (index < 0 || index >= MAX_NEOPIXELS) return;
    board.neopixels[index].r = r;
    board.neopixels[index].g = g;
    board.neopixels[index].b = b;
}

// Update framebuffer from RGB565 data
EMSCRIPTEN_KEEPALIVE
void hw_update_framebuffer(const uint8_t *data, int size) {
    if (!board.display.framebuffer) return;
    int max = board.display.fb_width * board.display.fb_height * 2;
    if (size > max) size = max;
    memcpy(board.display.framebuffer, data, size);
    board.display.dirty = 1;
}


// ── Drawing helpers ──

static void draw_filled_circle(SDL_Renderer *r, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrt(radius * radius - dy * dy);
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void draw_circle_outline(SDL_Renderer *r, int cx, int cy, int radius) {
    int x = radius, y = 0, err = 1 - radius;
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
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static void pin_color(const pin_t *p, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (!p->enabled) {
        *r = 60; *g = 60; *b = 60;  // unclaimed: dark grey
        return;
    }
    if (p->category == CAT_BUTTON) {
        if (p->value) { *r = 80; *g = 120; *b = 80; }   // released: dim green
        else          { *r = 50; *g = 200; *b = 50; }    // pressed: bright green
        return;
    }
    if (p->direction == 0) {
        // Input
        if (p->value) { *r = 200; *g = 160; *b = 50; }  // high: amber
        else          { *r = 100; *g = 80;  *b = 30; }   // low: dim amber
    } else {
        // Output
        if (p->value) { *r = 50; *g = 120; *b = 220; }   // high: blue
        else          { *r = 30; *g = 60;  *b = 110; }    // low: dim blue
    }
}


// ── Exported: Render one frame ──

EMSCRIPTEN_KEEPALIVE
void hw_render(void) {
    SDL_Renderer *r = board.renderer;

    // Background
    SDL_SetRenderDrawColor(r, board.bg_r, board.bg_g, board.bg_b, 255);
    SDL_RenderClear(r);

    // Display (built-in screen)
    if (board.display.texture) {
        if (board.display.dirty) {
            SDL_UpdateTexture(board.display.texture, NULL,
                board.display.framebuffer,
                board.display.fb_width * 2);
            board.display.dirty = 0;
        }
        SDL_Rect dst = {
            board.display.x, board.display.y,
            board.display.w, board.display.h
        };
        SDL_RenderCopy(r, board.display.texture, NULL, &dst);

        // Display border
        SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
        SDL_RenderDrawRect(r, &dst);
    }

    // Pins
    for (int i = 0; i < board.pin_count; i++) {
        pin_t *p = &board.pins[i];
        if (p->radius <= 0) continue;

        uint8_t cr, cg, cb;
        pin_color(p, &cr, &cg, &cb);

        // Hover brightens
        if (p->hover) {
            cr = cr + (255 - cr) / 3;
            cg = cg + (255 - cg) / 3;
            cb = cb + (255 - cb) / 3;
        }

        // Filled circle
        SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
        draw_filled_circle(r, p->x, p->y, p->radius);

        // Outline (white if claimed, grey if not)
        if (p->enabled) {
            SDL_SetRenderDrawColor(r, 200, 200, 200, 255);
        } else {
            SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
        }
        draw_circle_outline(r, p->x, p->y, p->radius);

        // Press indicator
        if (p->pressed) {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            draw_circle_outline(r, p->x, p->y, p->radius + 2);
        }
    }

    // NeoPixels
    for (int i = 0; i < board.neopixel_count; i++) {
        neopixel_t *n = &board.neopixels[i];
        if (n->radius <= 0) continue;

        // Glow effect: if lit, draw a larger dim circle behind
        if (n->r + n->g + n->b > 0) {
            SDL_SetRenderDrawColor(r, n->r / 4, n->g / 4, n->b / 4, 255);
            draw_filled_circle(r, n->x, n->y, n->radius + 4);
        }

        // Main LED circle
        SDL_SetRenderDrawColor(r, n->r, n->g, n->b, 255);
        draw_filled_circle(r, n->x, n->y, n->radius);

        // Outline
        SDL_SetRenderDrawColor(r, 100, 100, 100, 255);
        draw_circle_outline(r, n->x, n->y, n->radius);
    }

    SDL_RenderPresent(r);
}


// ── Exported: Hit testing ──

// Returns pin index at (x,y), or -1 if none
EMSCRIPTEN_KEEPALIVE
int hw_hit_test(int x, int y) {
    for (int i = 0; i < board.pin_count; i++) {
        pin_t *p = &board.pins[i];
        if (p->radius <= 0) continue;
        int dx = x - p->x, dy = y - p->y;
        if (dx * dx + dy * dy <= (p->radius + 2) * (p->radius + 2)) {
            return i;
        }
    }
    return -1;
}

// Set hover state (called by .mjs on mousemove)
EMSCRIPTEN_KEEPALIVE
void hw_set_hover(int pin_index) {
    for (int i = 0; i < board.pin_count; i++) {
        board.pins[i].hover = (i == pin_index);
    }
    board.hover_pin = pin_index;
}

// Set pressed state (called by .mjs on mousedown/mouseup)
EMSCRIPTEN_KEEPALIVE
void hw_set_pressed(int pin_index, int pressed) {
    if (pin_index >= 0 && pin_index < MAX_PINS) {
        board.pins[pin_index].pressed = pressed;
    }
}

// Get pin name (for tooltips)
EMSCRIPTEN_KEEPALIVE
const char *hw_get_pin_name(int index) {
    if (index < 0 || index >= MAX_PINS) return "";
    return board.pins[index].name;
}

// Get pin category
EMSCRIPTEN_KEEPALIVE
int hw_get_pin_category(int index) {
    if (index < 0 || index >= MAX_PINS) return CAT_UNKNOWN;
    return board.pins[index].category;
}
