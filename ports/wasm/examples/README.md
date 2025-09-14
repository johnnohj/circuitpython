# CircuitPython WASM + xterm.js Integration Examples

This directory demonstrates **proper I/O wiring** between CircuitPython WASM and xterm.js terminal interfaces.

## Architecture Overview

```
┌─────────────────┐    ┌─────────────────┐     ┌─────────────────┐
│   User Input    │───▶│   xterm.js      │───▶│ CircuitPython   │
│   (keyboard)    │    │   Terminal      │     │ WASM Module     │
└─────────────────┘    └─────────────────┘     └─────────────────┘
                                ▲                        │
                                │                        │
                                │       stdout/stderr    │
                                └────────────────────────┘
```

### Key Principles

1. **WASM Internal I/O**: CircuitPython WASM handles Python execution internally using `printf()`
2. **Clean API Surface**: WASM exposes minimal, documented functions for integration
3. **No Custom Solutions**: Applications use standard xterm.js + documented WASM APIs
4. **Character-level Processing**: REPL processes input character-by-character for proper terminal behavior

## Examples

### 1. Headless Terminal (`headless-example.mjs`)

**Node.js example using @xterm/headless**

```bash
cd examples
npm install
npm start
```

**Features:**
- Demonstrates proper WASM ↔ xterm.js I/O wiring
- Character-by-character REPL processing
- Terminal state management and debugging
- Dynamic module loading integration
- Headless operation (no browser required)

### 2. Browser Terminal (`browser-terminal.html`)

**Browser example using full xterm.js**

```bash
cd examples
npm run serve
# Open http://localhost:8080/browser-terminal.html
```

**Features:**
- Complete browser-based terminal interface
- Visual terminal with proper theming
- Interactive REPL with history
- **Blinka glyph PNG image integration with DOM overlay**
- Module loading examples
- Automated test demonstrations

### 3. Module Loading Demo (`module-loading-demo.html`)

**Complete dynamic module loading demonstration**

```bash
cd examples
npm run serve
# Open http://localhost:8080/module-loading-demo.html
```

**Features:**
- Interactive module editor with syntax highlighting
- Real-time module loading and testing
- Proper error handling and status reporting
- Memory management examples
- Multiple loading method demonstrations

## WASM API Reference

### Core Functions

```c
// Initialize CircuitPython with heap size
EMSCRIPTEN_KEEPALIVE void _mp_js_init_with_heap(int heap_size);

// Deferred post-initialization
EMSCRIPTEN_KEEPALIVE void _mp_js_post_init(void);

// Initialize REPL
EMSCRIPTEN_KEEPALIVE void _mp_js_repl_init(void);

// Process single character through REPL
EMSCRIPTEN_KEEPALIVE int _mp_js_repl_process_char(int char_code);

// Load Python module from source code (returns 0=success, -1=invalid params, -2=execution error)
EMSCRIPTEN_KEEPALIVE int _mp_js_load_module(const char* name, const char* source);

// Get Blinka glyph path for terminal display (returns C string pointer)
EMSCRIPTEN_KEEPALIVE const char* _mp_js_get_blinka_glyph_path(void);
```

### I/O Configuration

```javascript
// Proper WASM initialization with I/O handlers
const circuitPython = await _createCircuitPythonModule({
    stdout: (charCode) => {
        // Send WASM output to terminal
        terminal.write(String.fromCharCode(charCode));
    },
    stderr: (charCode) => {
        // Handle error output (optional: style differently)
        terminal.write(String.fromCharCode(charCode));
    }
});
```

### Blinka Glyph Integration

```javascript
// Display Blinka PNG image using direct DOM manipulation
async function displayImageInTerminal(terminal, imagePath) {
    try {
        // Insert the image directly into the terminal DOM
        const terminalElement = document.getElementById('terminal');
        const xtermScreen = terminalElement.querySelector('.xterm-screen');
        
        if (xtermScreen) {
            // Create image element
            const img = document.createElement('img');
            img.src = imagePath;
            img.style.width = '16px';
            img.style.height = '16px';
            img.style.display = 'inline-block';
            img.style.position = 'absolute';
            img.style.zIndex = '1000';
            
            // Position at current cursor location
            const cursorX = terminal.buffer.active.cursorX;
            const cursorY = terminal.buffer.active.cursorY;
            img.style.left = `${cursorX * 9}px`;
            img.style.top = `${cursorY * 17}px`;
            
            xtermScreen.appendChild(img);
            terminal.write(' '); // Advance cursor
        }
    } catch (error) {
        terminal.write('🐍 '); // Fallback to emoji
    }
}

// Display Blinka glyph before CircuitPython banner
await displayImageInTerminal(terminal, './blinka_glyph.png');
```

### Terminal Input Processing

```javascript
// Handle terminal input and send to WASM REPL
terminal.onData((data) => {
    for (let i = 0; i < data.length; i++) {
        const charCode = data.charCodeAt(i);
        const result = circuitPython._mp_js_repl_process_char(charCode);

        // Handle special cases:
        // result === 0: normal processing
        // result === 1: need more input (multi-line)
        // result === 2: interrupt (Ctrl+C)
    }
});
```

## Integration Patterns

### 1. Terminal Initialization Sequence

```javascript
// 1. Create terminal
const terminal = new Terminal(options);
terminal.open(containerElement);

// 2. Load WASM with I/O handlers
const mp = await _createCircuitPythonModule({ stdout, stderr });

// 3. Initialize CircuitPython
mp._mp_js_init_with_heap(1024 * 1024); // 1MB heap

// 4. Initialize REPL
mp._mp_js_repl_init();

// 5. Set up input handling
terminal.onData(data => processInput(data));
```

### 2. Dynamic Module Loading

## Import Resolution Order

The WASM module resolves imports using the following priority order:

**Browser Environment:**
1. **Browser Local Storage** - User's saved modules (`localStorage`)
2. **File System Access API** - Local filesystem and USB storage (requires web-editor integration)  
3. **Local HTTP Paths** - Served by web server (`./modules/`, `./lib/`, etc.)
4. **GitHub Raw API** - Adafruit libraries as fallback

**Node.js Environment:**
1. **Current Working Directory** - `./modules/`, `./lib/`, `./`
2. **Relative Paths** - Standard Node.js module resolution

## Web-Editor Integration

For web-editor applications, the module resolver supports:

**Browser Local Storage:**
```javascript
// Save a module to localStorage
localStorage.setItem('circuitpython_module_mymodule', `
def hello_world():
    print("Hello from saved module!")
`);

// The module resolver will automatically find it when imported
```

**File System Access API:**
```javascript
// This requires web-editor integration to implement
// The resolver checks for File System Access API availability
// and can be extended to work with the web-editor's file manager
```

**Adafruit Library Access:**
```python
# These will automatically fallback to GitHub if not found locally
import neopixel       # Loads from Adafruit_CircuitPython_NeoPixel
import displayio      # Loads from Community Bundle
import adafruit_led   # Loads from Adafruit libraries
```

## PyScript-Compatible API

The WASM build provides PyScript-compatible functions for easy integration:

```javascript
// PyScript-style import (automatic resolution)
await pyImport('neopixel');
await pyImport('adafruit_led');

// Architecture-specified loadPythonModule function
await loadPythonModule('mymodule');  // Auto-resolves via priority chain
await loadPythonModule('custom', `   // Load from source
def hello():
    print("Hello from custom module!")
`);

// Module cache management
circuitpython.isModuleCached('neopixel');  // Check if cached
circuitpython.clearModuleCache('neopixel'); // Clear specific module
circuitpython.getCachedModules();           // List all cached modules
```

## Loading Custom Modules

```javascript
// Method 1: Using ccall (recommended for string handling)
const moduleSource = `
def my_function():
    print("Hello from dynamic module!")
    return 42

# Module-level code executes on load
print(f"Module '{__name__}' loaded successfully!")
`;

const result = circuitPython.ccall('mp_js_load_module', 'number', ['string', 'string'], ['my_module', moduleSource]);

// Handle results
switch (result) {
    case 0:
        console.log('✅ Module loaded successfully');
        break;
    case -1:
        console.error('❌ Invalid parameters (check module name/source)');
        break;
    case -2:
        console.error('❌ Python execution error in module');
        break;
    default:
        console.error('❌ Unknown error:', result);
}

// Method 2: Manual memory allocation (advanced)
const moduleName = 'advanced_module';
const nameLen = circuitPython.lengthBytesUTF8(moduleName) + 1;
const sourceLen = circuitPython.lengthBytesUTF8(moduleSource) + 1;

const namePtr = circuitPython._malloc(nameLen);
const sourcePtr = circuitPython._malloc(sourceLen);

circuitPython.stringToUTF8(moduleName, namePtr, nameLen);
circuitPython.stringToUTF8(moduleSource, sourcePtr, sourceLen);

const result2 = circuitPython._mp_js_load_module(namePtr, sourcePtr);

// Clean up memory
circuitPython._free(namePtr);
circuitPython._free(sourcePtr);
```

### 3. State Management

```javascript
// Terminal state can be serialized/restored
const state = terminal.serialize();

// WASM state is managed internally
// Use REPL character processing for state transitions
```

## Error Handling

### Common Issues and Solutions

1. **No Output Visible**: Ensure stdout/stderr handlers are properly configured in WASM initialization
2. **Input Not Working**: Verify `terminal.onData` is connected to `_mp_js_repl_process_char`
3. **Module Loading Fails**: Check module source code syntax and API return codes
4. **Memory Issues**: Increase heap size in `_mp_js_init_with_heap`

### Debugging Tips

```javascript
// Enable WASM debugging
const mp = await _createCircuitPythonModule({
    stdout: (charCode) => {
        const char = String.fromCharCode(charCode);
        console.log('WASM stdout:', charCode, char); // Debug output
        terminal.write(char);
    }
});

// Monitor terminal state
console.log('Terminal buffer:', terminal.buffer.active.length);
console.log('Cursor position:', terminal.buffer.active.cursorX, terminal.buffer.active.cursorY);
```

## Performance Considerations

### Optimizations

1. **Batch Character Processing**: For paste operations, process characters in batches
2. **Terminal Buffer Management**: Use appropriate scrollback limits
3. **WASM Heap Sizing**: Balance memory usage vs performance
4. **Output Buffering**: Consider buffering stdout for better performance

### Memory Usage

- **Minimal Setup**: ~512KB heap for basic REPL
- **With Modules**: 1-2MB heap for dynamic module loading
- **Large Applications**: 4MB+ heap for complex programs

## Browser Compatibility

### Supported Browsers
- Chrome/Chromium 70+
- Firefox 65+
- Safari 14+
- Edge 79+

### WebAssembly Requirements
- WASM support with ES6 module integration
- SharedArrayBuffer for threading (if enabled)
- Proper CORS headers for module loading

## Development Workflow

### Building Examples

```bash
# Build CircuitPython WASM
cd .. && make

# Install dependencies
cd examples
npm install

# Run headless example
npm start

# Serve browser example
npm run serve
```

### Testing Integration

1. **Headless Testing**: Use `headless-example.mjs` for automated testing
2. **Browser Testing**: Use `browser-terminal.html` for interactive testing
3. **Unit Testing**: Test individual WASM API functions

## Contributing

When extending these examples:

1. **Keep WASM Internal**: Don't add JavaScript workarounds for WASM functionality
2. **Document APIs**: All new WASM exports should be documented
3. **Test Both Modes**: Verify headless and browser integration
4. **Follow Patterns**: Use established I/O wiring patterns

## Related Documentation

- [xterm.js API Documentation](https://xtermjs.org/docs/)
- [@xterm/headless Package](https://www.npmjs.com/package/@xterm/headless)
- [CircuitPython WASM Port README](../README.md)
- [Emscripten Documentation](https://emscripten.org/docs/)

---

These examples demonstrate the **correct architecture** for terminal integration: clean separation between WASM execution and JavaScript presentation, with documented APIs for proper I/O wiring.
