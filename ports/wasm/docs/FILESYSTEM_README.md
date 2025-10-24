# CircuitPython WASM Persistent Filesystem

This implementation provides **Option 3: Hybrid** from the WASM_PORT_ROADMAP.md - combining frozen modules with IndexedDB for persistent user files.

## Features

### ✅ Implemented

1. **Persistent Storage**: Files are saved to IndexedDB and survive page reloads
2. **Auto-Run Workflow**: Automatic execution of `boot.py` and `code.py`
3. **File Management**: Create, read, update, delete files
4. **Project Export/Import**: Save and restore entire projects as JSON
5. **VFS Synchronization**: Seamless integration with Emscripten's virtual filesystem

## Quick Start

### Basic Usage (Memory Only)

```javascript
import { loadCircuitPython } from './circuitpython.mjs';

const ctpy = await loadCircuitPython({
    autoRun: true,  // Auto-run boot.py and code.py
    verbose: true
});

// Write code
ctpy.FS.writeFile('/code.py', 'print("Hello WASM!")');

// Run it
ctpy.runFile('/code.py');
```

### Persistent Storage with IndexedDB

```javascript
import { loadCircuitPython } from './circuitpython.mjs';

const ctpy = await loadCircuitPython({
    filesystem: 'indexeddb',  // Enable persistent storage
    autoRun: true,            // Auto-run boot.py and code.py
    verbose: true,
    stdout: (text) => console.log(text)
});

// Save a file (persists across page reloads!)
await ctpy.saveFile('/code.py', `
import board
import time

print("This code persists!")
time.sleep(1)
`);

// Save a library
await ctpy.saveFile('/lib/mymodule.py', `
def greet(name):
    return f"Hello, {name}!"
`);

// Run the workflow
ctpy.runWorkflow();  // Runs boot.py then code.py
```

## API Reference

### loadCircuitPython(options)

Main initialization function with new options:

```typescript
interface LoadOptions {
    // Existing options
    pystack?: number;          // Python stack size in words (default: 2048)
    heapsize?: number;         // Heap size in bytes (default: 1MB)
    url?: string;              // URL to load circuitpython.mjs
    stdin?: Function;          // Input handler
    stdout?: Function;         // Output handler
    stderr?: Function;         // Error output handler
    linebuffer?: boolean;      // Buffer output by lines (default: true)
    verbose?: boolean;         // Log initialization (default: false)

    // NEW: Filesystem options
    filesystem?: 'memory' | 'indexeddb';  // Storage type (default: 'memory')
    autoRun?: boolean;                    // Auto-run boot.py and code.py (default: false)
}
```

### Returned Object Properties

```typescript
interface CircuitPython {
    // Existing properties
    _module: EmscriptenModule;
    virtualClock: VirtualClock;
    PyProxy: typeof PyProxy;
    FS: EmscriptenFS;
    globals: {
        __dict__: any;
        get(key: string): any;
        set(key: string, value: any): void;
        delete(key: string): void;
    };
    registerJsModule(name: string, module: any): void;
    pyimport(name: string): any;
    runPython(code: string): any;
    runPythonAsync(code: string): Promise<any>;
    replInit(): void;
    replProcessChar(chr: number): number;
    replProcessCharWithAsyncify(chr: number): Promise<number>;

    // NEW: Filesystem methods
    filesystem: CircuitPythonFilesystem | null;  // IndexedDB filesystem (null if memory mode)
    runFile(filepath: string): any;               // Run a Python file
    runWorkflow(): void;                          // Run boot.py then code.py
    saveFile(filepath: string, content: string | Uint8Array): Promise<void>;  // Save to VFS + IndexedDB
}
```

### CircuitPythonFilesystem Class

When `filesystem: 'indexeddb'` is enabled:

```typescript
class CircuitPythonFilesystem {
    // Initialize the database
    async init(): Promise<void>;

    // File operations
    async writeFile(path: string, content: string | Uint8Array): Promise<void>;
    async readFile(path: string): Promise<Uint8Array>;
    async deleteFile(path: string): Promise<void>;
    async exists(path: string): Promise<boolean>;
    async listFiles(dirPath?: string): Promise<FileEntry[]>;

    // VFS synchronization
    async syncToVFS(Module): Promise<void>;      // Load files from IndexedDB to VFS
    async syncFromVFS(Module, paths: string[]): Promise<void>;  // Save files from VFS to IndexedDB

    // Project management
    async exportProject(): Promise<Blob>;         // Export all files as JSON
    async importProject(blob: Blob): Promise<void>;  // Import files from JSON
    async clear(): Promise<void>;                 // Delete all files
}
```

## File Structure in IndexedDB

```javascript
// Database: 'circuitpython'
// Object Store: 'files'
// Key Path: 'path'

// File record:
{
    path: '/code.py',           // File path
    content: Uint8Array,        // File content as bytes
    modified: 1705932845000,    // Last modified timestamp
    size: 1024,                 // Size in bytes
    isDirectory: false          // Directory flag
}
```

## Usage Examples

### Example 1: Simple Blink with Persistence

```javascript
const ctpy = await loadCircuitPython({
    filesystem: 'indexeddb',
    autoRun: true,
    verbose: true
});

// Save code that will run on every page load
await ctpy.saveFile('/code.py', `
import board
import digitalio
import time

led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

for i in range(10):
    led.value = not led.value
    time.sleep(0.5)
`);

// Reload the page - code.py runs automatically!
```

### Example 2: Library Management

```javascript
const ctpy = await loadCircuitPython({
    filesystem: 'indexeddb',
    verbose: true
});

// Install a library (persists!)
await ctpy.saveFile('/lib/neopixel.py', libraryCode);

// Use it in your code
await ctpy.saveFile('/code.py', `
import neopixel
import board

pixels = neopixel.NeoPixel(board.NEOPIXEL, 10)
pixels[0] = (255, 0, 0)
pixels.show()
`);

ctpy.runWorkflow();
```

### Example 3: Project Export/Import

```javascript
const ctpy = await loadCircuitPython({
    filesystem: 'indexeddb'
});

// Create multiple files
await ctpy.saveFile('/code.py', mainCode);
await ctpy.saveFile('/boot.py', bootCode);
await ctpy.saveFile('/lib/helpers.py', helperCode);

// Export entire project
const projectBlob = await ctpy.filesystem.exportProject();

// Save to disk
const url = URL.createObjectURL(projectBlob);
const a = document.createElement('a');
a.href = url;
a.download = 'my-project.json';
a.click();

// Later, import it back
const fileInput = document.querySelector('input[type="file"]');
fileInput.addEventListener('change', async (e) => {
    const file = e.target.files[0];
    await ctpy.filesystem.importProject(file);
    await ctpy.filesystem.syncToVFS(ctpy._module);
    ctpy.runWorkflow();
});
```

### Example 4: File Browser

```javascript
const ctpy = await loadCircuitPython({
    filesystem: 'indexeddb'
});

// List all files
const files = await ctpy.filesystem.listFiles();
console.table(files.map(f => ({
    path: f.path,
    size: `${(f.size / 1024).toFixed(1)} KB`,
    modified: new Date(f.modified).toLocaleString()
})));

// List files in /lib
const libFiles = await ctpy.filesystem.listFiles('/lib');
console.log('Libraries:', libFiles.map(f => f.path));
```

## Demo

See [demo_filesystem.html](./demo_filesystem.html) for a complete interactive demo with:

- Code editor for `code.py`
- File list browser
- Save/Load functionality
- Run button
- Project export/import
- Console output

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   Web Browser                        │
├─────────────────────────────────────────────────────┤
│                                                       │
│  ┌───────────────┐         ┌────────────────────┐  │
│  │   Your App    │←───────→│  CircuitPython     │  │
│  │   (HTML/JS)   │         │  WASM Runtime      │  │
│  └───────────────┘         └────────────────────┘  │
│         ↓                            ↑              │
│  ┌─────────────────────────────┐    │              │
│  │  CircuitPythonFilesystem    │    │              │
│  │  (filesystem.js)            │    │              │
│  └─────────────────────────────┘    │              │
│         ↓                            │              │
│  ┌─────────────────────────────┐    │              │
│  │     IndexedDB               │    │              │
│  │  (Persistent Storage)       │    │              │
│  └─────────────────────────────┘    │              │
│                                      │              │
│         Sync on init                 │              │
│         ─────────────────────────────┘              │
│                                      ↓              │
│                            ┌────────────────────┐  │
│                            │  Emscripten VFS    │  │
│                            │  (Memory Only)     │  │
│                            └────────────────────┘  │
│                                                      │
└─────────────────────────────────────────────────────┘
```

## Workflow

```
1. Page Load
   ↓
2. loadCircuitPython({ filesystem: 'indexeddb' })
   ↓
3. Initialize IndexedDB
   ↓
4. Load files from IndexedDB → Emscripten VFS
   ↓
5. If autoRun: true
   ↓
6. Run boot.py (if exists)
   ↓
7. Run code.py (if exists)
   ↓
8. User can edit/save files
   ↓
9. Files saved to BOTH VFS and IndexedDB
   ↓
10. Page reload → Start from step 1 (files persist!)
```

## Browser Compatibility

- ✅ Chrome/Edge (all features)
- ✅ Firefox (all features)
- ✅ Safari (all features)
- ✅ All modern browsers with IndexedDB support

## Storage Limits

Typical browser IndexedDB limits:
- **Chrome**: ~60% of free disk space
- **Firefox**: ~50% of free disk space
- **Safari**: ~1GB

Practical limits for CircuitPython projects: **50-100MB** is safe across all browsers.

## Future Enhancements

1. **Frozen Modules**: Pre-compile Adafruit libraries into WASM for instant loading
2. **Auto-save**: Automatically save code on file writes from Python
3. **File Watching**: Auto-reload on file changes
4. **Library Manager**: Install libraries from Adafruit bundle with one click
5. **Multi-tab Sync**: Share filesystem across browser tabs

## Related Files

- `api.js` - Main API with filesystem integration
- `filesystem.js` - IndexedDB filesystem implementation
- `demo_filesystem.html` - Interactive demo
- `WASM_PORT_ROADMAP.md` - Overall roadmap and plans
