# displayio Implementation Plan for WASM

## Overview

Implementing `displayio` and `busdisplay` for the WASM port would enable graphics rendering to HTML canvas elements, making CircuitPython graphics code work in web browsers.

## Architecture

### Modules Involved

1. **displayio** - Core graphics primitives (shared-module):
   - `Bitmap` - Image data storage
   - `Palette` - Color palettes
   - `ColorConverter` - Color space conversion
   - `TileGrid` - Tiled sprite/background graphics
   - `Group` - Hierarchical scene graph
   - `OnDiskBitmap` - Load images from files

2. **busdisplay** - Display hardware interface:
   - `BusDisplay` - Main display class that drives a display over a bus (SPI/I2C)

### What's Already Implemented

- **shared-module/displayio**: ~3000+ lines of graphics logic
  - Scene graph management
  - Dirty rectangle tracking
  - Rendering pipeline
  - Color conversion

- **shared-module/busdisplay**: ~431 lines
  - Display state management
  - Refresh coordination
  - Bus communication

### What Needs Implementation (common-hal for WASM)

The common-hal needs to:
1. Create and manage HTML canvas elements
2. Render framebuffer to canvas
3. Handle display refresh
4. Manage display properties (brightness, rotation, etc.)

## Required common-hal Functions

###busdisplay.BusDisplay

```c
// Create display (width, height, color depth, bus, etc.)
void common_hal_busdisplay_busdisplay_construct(...)

// Render framebuffer to canvas
bool common_hal_busdisplay_busdisplay_refresh(...)

// Property getters/setters
uint16_t common_hal_busdisplay_busdisplay_get_width(...)
uint16_t common_hal_busdisplay_busdisplay_get_height(...)
void common_hal_busdisplay_busdisplay_set_rotation(...)
mp_float_t common_hal_busdisplay_busdisplay_get_brightness(...)
// ... etc
```

## Implementation Strategy

### 1. Canvas Management (JavaScript Side)

Create JavaScript helper functions in `library_displayio.js`:

```javascript
// Create canvas element
function displayio_create_canvas(width, height, canvasId) {
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    canvas.id = canvasId;
    document.body.appendChild(canvas);
    return canvas;
}

// Render framebuffer to canvas
function displayio_render_framebuffer(canvasId, framebuffer, width, height, colorDepth) {
    const canvas = document.getElementById(canvasId);
    const ctx = canvas.getContext('2d');
    const imageData = ctx.createImageData(width, height);

    // Convert framebuffer to canvas ImageData
    // Handle different color depths (16-bit, 24-bit, etc.)
    // ...

    ctx.putImageData(imageData, 0, 0);
}
```

### 2. C Implementation (common-hal/busdisplay)

```c
// common-hal/busdisplay/BusDisplay.h
typedef struct {
    mp_obj_base_t base;
    busdisplay_bus_obj_t *bus;
    uint16_t width;
    uint16_t height;
    uint16_t rotation;
    uint8_t color_depth;
    uint32_t canvas_id;  // Unique ID for this canvas
    uint8_t *framebuffer;  // Pixel data
    displayio_group_t *root_group;
    // ... other state
} busdisplay_busdisplay_obj_t;

// common-hal/busdisplay/BusDisplay.c
void common_hal_busdisplay_busdisplay_construct(...) {
    // 1. Allocate framebuffer
    self->framebuffer = m_malloc(width * height * (color_depth / 8));

    // 2. Call JavaScript to create canvas
    EM_ASM({
        Module.displayio_create_canvas($0, $1, $2);
    }, width, height, self->canvas_id);

    // 3. Initialize display state
    self->width = width;
    self->height = height;
    // ...
}

bool common_hal_busdisplay_busdisplay_refresh(...) {
    // 1. Get dirty regions from shared-module
    // 2. Render scene graph to framebuffer
    // 3. Send framebuffer to canvas

    EM_ASM({
        Module.displayio_render_framebuffer($0, $1, $2, $3, $4);
    }, self->canvas_id, self->framebuffer, self->width, self->height, self->color_depth);

    return true;
}
```

### 3. File Structure

```
ports/wasm/
├── common-hal/
│   └── busdisplay/
│       ├── BusDisplay.c        # Main implementation
│       ├── BusDisplay.h        # Type definitions
│       └── __init__.c          # Module init
│
├── src/library/
│   └── library_displayio.js    # Canvas rendering helpers
│
└── docs/
    └── displayio.md            # Usage examples
```

## Estimated Implementation Effort

### Phase 1: Basic Canvas Display (2-3 hours)
- ✅ Create canvas management JavaScript functions
- ✅ Implement BusDisplay construct/refresh
- ✅ Support simple 16-bit RGB565 color
- ✅ Test with basic rectangles and text

### Phase 2: Full Color Support (1-2 hours)
- ✅ Support multiple color depths (1, 2, 4, 8, 16, 24-bit)
- ✅ Implement ColorConverter integration
- ✅ Test with Bitmap and Palette

### Phase 3: Advanced Features (2-3 hours)
- ✅ Rotation support
- ✅ Brightness control (canvas opacity)
- ✅ Dirty rectangle optimization
- ✅ Multiple canvas support

### Phase 4: Integration (1-2 hours)
- ✅ Enable modules in mpconfigport.mk
- ✅ Documentation and examples
- ✅ Test suite with various displays
- ✅ Integration with existing graphics libraries

**Total Estimated Time**: 6-10 hours

## Benefits for WASM Port

1. **Visual Output**: Graphics displayed in browser
2. **Educational**: Learn displayio concepts visually
3. **Library Compatibility**: Run existing CircuitPython graphics code
4. **Web Integration**: Combine with HTML/CSS/JavaScript
5. **Testing**: Visual testing of graphics code without hardware

## Example Usage

```python
import board
import displayio
import busdisplay
import terminalio
from adafruit_display_text import label

# Create SPI bus for display
spi = board.SPI()

# Create display (this creates HTML canvas)
display = busdisplay.BusDisplay(
    spi,
    width=320,
    height=240,
    colstart=0,
    rowstart=0,
    color_depth=16,
    # ... init sequence for specific display
)

# Create a group to hold display objects
group = displayio.Group()

# Add a text label
text = "Hello from WASM!"
text_area = label.Label(terminalio.FONT, text=text, color=0xFFFFFF)
text_area.x = 10
text_area.y = 120
group.append(text_area)

# Show the group on the display
display.root_group = group

# Canvas is automatically updated!
```

The canvas would appear in the browser showing "Hello from WASM!" rendered with CircuitPython's displayio!

## Dependencies

### Already Implemented in WASM
- ✅ busio.SPI (for display communication)
- ✅ digitalio (for CS/DC pins)
- ✅ Memory management (framebuffer allocation)

### Needs Implementation
- ❌ common-hal/busdisplay/BusDisplay.c
- ❌ library_displayio.js (canvas helpers)
- ❌ Module enablement (CIRCUITPY_DISPLAYIO, CIRCUITPY_BUSDISPLAY)

## Open Questions

1. **Canvas Placement**: Where should canvases appear in DOM?
   - Auto-append to body?
   - User-specified container?
   - Multiple display support?

2. **Display IDs**: How to manage multiple displays?
   - Auto-incrementing IDs?
   - User-specified canvas IDs?

3. **Refresh Strategy**: When to update canvas?
   - Auto-refresh on dirty?
   - Manual refresh only?
   - RequestAnimationFrame integration?

4. **Color Conversion**: Browser uses RGBA, displays use RGB565/etc.
   - Conversion in C or JavaScript?
   - Performance implications?

## Next Steps

1. Create `common-hal/busdisplay` directory structure
2. Implement basic BusDisplay with 16-bit RGB565
3. Create canvas rendering JavaScript library
4. Enable modules in build system
5. Test with simple graphics examples
6. Iterate on features and optimizations

This would be a significant and exciting addition to the WASM port, enabling visual CircuitPython programming in web browsers!
