# CircuitPython WASM Port - Development Roadmap

## Current Status (2025-01)

### ✅ Completed Features

**Core Architecture:**
- Virtual timing system with 32kHz clock simulation (inspired by Renode)
- Shared memory for WASM ↔ JavaScript communication
- Message queue system for async hardware operations
- Native yielding via `RUN_BACKGROUND_TASKS` (no ASYNCIFY needed)
- Three timing modes: REALTIME, MANUAL, FAST_FORWARD

**Hardware Modules Implemented:**
- `digitalio` - GPIO with direction, pull, drive modes
- `analogio` - AnalogIn, AnalogOut with yielding
- `busio.I2C` - Full I2C with read/write/probe
- `busio.SPI` - Configurable SPI with transfers
- `busio.UART` - Complete UART with baudrate control
- `board` - Board pin definitions (stub)
- `microcontroller` - CPU info, reset functions
- `time` - Timing functions using virtual clock

**Build System:**
- Emscripten-based WASM compilation
- ~625KB total size (224KB .mjs + 401KB .wasm)
- Selective module enabling (not CIRCUITPY_FULL_BUILD=1, see below)
- VirtualClock automatically initialized on module load
- Proper upstream tags fetched from github.com/adafruit/circuitpython

**Supervisor:**
- Consolidated supervisor/port.c (all functions in one place)
- Virtual timing integrated with CircuitPython supervisor
- Background task system for yielding

### 📦 CircuitPython Modules Status

**Why Not CIRCUITPY_FULL_BUILD=1?**
We cannot enable CIRCUITPY_FULL_BUILD=1 because it would enable ALL 34+ CircuitPython modules, many requiring hardware implementations (common-hal files) we haven't created yet. Instead, we selectively enable modules that work in WASM.

**Currently Enabled (14 modules):**

*Hardware modules with WASM implementations:*
- ✅ `analogio` - AnalogIn/AnalogOut with virtual hardware
- ✅ `busio` - I2C, SPI, UART with message queue
- ✅ `digitalio` - GPIO with virtual pins
- ✅ `microcontroller` - CPU info, reset, Pin definitions
- ✅ `time` - Timing with virtual 32kHz clock

*Software-only utility modules:*
- ✅ `binascii` - Binary/ASCII conversions
- ✅ `builtins.pow3` - 3-argument pow() function
- ✅ `errno` - Error codes
- ✅ `json` - JSON parsing/serialization
- ✅ `OPT_MAP_LOOKUP_CACHE` - Performance optimization (attribute access)
- ✅ `os.getenv` - Environment variables
- ✅ `re` - Regular expressions
- ✅ `ulab` - NumPy-like scientific computing (6.5.3)
- ✅ `zlib` - Compression

**Cannot Add (7 modules - need circuitpy_defns.mk):**

These modules require `py/circuitpy_defns.mk` which causes duplicate symbol errors (uzlib, etc.) when included:
- 🔴 `aesio` - AES encryption (needs shared-bindings sources)
- 🔴 `atexit` - Cleanup handlers (needs shared-bindings sources)
- 🔴 `busdevice` - I2C/SPI helpers (needs shared-bindings sources)
- 🔴 `codeop` - Code compilation (needs shared-bindings sources)
- 🔴 `locale` - Localization (needs shared-bindings sources)
- 🔴 `msgpack` - MessagePack (needs shared-bindings sources)
- 🔴 `traceback` - Enhanced tracebacks (needs shared-bindings sources)
- 🔴 `warnings` - Python warnings (needs qstr setup)

**Excluded (11 modules - require hardware implementations):**

*Audio modules:*
- ❌ `audiobusio` - I2S audio interface
- ❌ `audioio` - Audio output
- ❌ `audiomp3` - MP3 decoding (depends on audiocore)

*Display modules:*
- ❌ `displayio` - Display management and framebuffer
- ❌ `paralleldisplaybus` - Parallel display interface
- ❌ `framebufferio` - Framebuffer displays
- ❌ `bitmapfilter` - Bitmap filters (depends on displayio)
- ❌ `bitmaptools` - Bitmap manipulation (depends on displayio)
- ❌ `pixelbuf` - LED/NeoPixel control

*Other hardware:*
- ❌ `bitbangio` - Software I2C/SPI (could virtualize in future)
- ❌ `bleio` - Bluetooth Low Energy
- ❌ `countio` - Pulse/edge counting
- ❌ `frequencyio` - Frequency measurement
- ❌ `i2ctarget` - I2C peripheral mode
- ❌ `keypad` - Keyboard matrix scanning
- ❌ `pulseio` - Pulse width I/O
- ❌ `sdcardio` - SD card access

**Future Possibilities:**

*Virtual Display (Phase 4?):*
If we implement a virtual display (HTML5 Canvas), we could enable:
- `displayio` + all bitmap/display modules
- `pixelbuf` (for virtual NeoPixels)

*Virtual Audio (Phase 5?):*
With Web Audio API integration:
- `audioio`, `audiobusio`, `audiomp3`

---

## Critical Gap Identified: Boot/Code Workflow

### The Problem

CircuitPython's core workflow on physical boards:
```
1. Mount filesystem (appears as USB drive "CIRCUITPY")
2. Run boot.py (optional, for configuration)
3. Run code.py (main user program)
4. Load libraries from lib/ directory
5. Auto-reload on file save
```

**Current WASM Port Status:**
- ✅ Emscripten VFS mounted at `/`
- ✅ `/lib` added to `sys.path`
- ✅ Manual execution via `ctpy.runPython(code)`
- ✅ REPL mode available
- ❌ No automatic boot.py execution
- ❌ No automatic code.py execution
- ❌ No persistent filesystem (files lost on reload)
- ❌ No file browsing/editing interface

### Architecture Options Analysis

#### Option 1: Server-Side Only (Current)
**Implementation:** Files in Emscripten VFS (memory-only)

```javascript
const mp = await loadCircuitPython();
FS.writeFile('/code.py', 'print("Hello!")');
mp.runPython(FS.readFile('/code.py', {encoding: 'utf8'}));
```

**Pros:**
- ✅ Simple, works now
- ✅ Fast (no I/O)
- ✅ No additional dependencies

**Cons:**
- ❌ Files lost on page reload
- ❌ Can't share projects easily
- ❌ Not true to CircuitPython workflow
- ❌ No persistent library management

#### Option 2: Client-Side (IndexedDB)
**Implementation:** Files persist in browser IndexedDB

```javascript
class CircuitPythonFilesystem {
    async saveFile(path, content) {
        await indexedDB.put('circuitpy', {path, content});
    }
}

const mp = await loadCircuitPython({
    filesystem: new CircuitPythonFilesystem(),
    autoRun: true  // Runs boot.py then code.py
});
```

**Pros:**
- ✅ Persistence across sessions
- ✅ True to CircuitPython workflow
- ✅ Can export/import projects
- ✅ Libraries persist

**Cons:**
- ❌ More complex implementation
- ❌ Browser storage limits (~50MB typical)
- ❌ Need to sync Emscripten VFS ↔ IndexedDB

#### Option 3: Hybrid (RECOMMENDED)
**Implementation:** Frozen modules + IndexedDB for user files

```typescript
const mp = await loadCircuitPython({
    // Built-in libraries in WASM (frozen)
    frozenModules: ['/lib/adafruit_*.mpy'],

    // User files in IndexedDB
    userFilesystem: new IndexedDBFS('/'),

    // Auto-execute workflow
    async onInit() {
        await this.runFileIfExists('/boot.py');
        await this.runFileIfExists('/code.py');
    }
});
```

**Pros:**
- ✅ Best of both worlds
- ✅ Fast library loading (frozen)
- ✅ Persistent user code
- ✅ Professional development experience

**Cons:**
- ⚠️  Most complex to implement
- ⚠️  Requires careful FS synchronization

---

## Phased Implementation Plan

### Phase 1: Basic Boot/Code Workflow (Foundation)
**Goal:** Get boot.py/code.py execution working with Emscripten VFS

**Tasks:**
- [ ] Add `runFile(filepath)` helper to api.js exports
- [ ] Create `runWorkflow()` function that executes boot.py → code.py
- [ ] Update loadCircuitPython() to accept `autoRun: boolean` option
- [ ] Add error reporting to JavaScript console with proper formatting
- [ ] Create default code.py if none exists
- [ ] Test workflow with sample boot.py and code.py

**API Design:**
```typescript
// New exports in api.js
export interface CircuitPythonOptions {
    autoRun?: boolean;  // Auto-execute boot.py and code.py
    onError?: (filename: string, error: Error) => void;
}

export async function loadCircuitPython(options?: CircuitPythonOptions);

// Helper functions
export function runFile(mp, filepath: string): any;
export function runWorkflow(mp): void;
```

**Example Usage:**
```javascript
import { loadCircuitPython } from './circuitpython.mjs';

const mp = await loadCircuitPython({
    autoRun: true,
    onError: (file, err) => console.error(`Error in ${file}:`, err)
});
// boot.py and code.py already executed!
```

**Estimated Effort:** 1-2 days
**Dependencies:** None
**Risk:** Low - uses existing Emscripten VFS

---

### Phase 2: IndexedDB Persistence (Enhancement)
**Goal:** Persistent filesystem that survives page reloads

**Tasks:**
- [ ] Create CircuitPythonFilesystem class for IndexedDB operations
- [ ] Implement file CRUD operations (create, read, update, delete, list)
- [ ] Add Emscripten VFS ↔ IndexedDB synchronization layer
- [ ] Auto-save files when written from Python
- [ ] Lazy-load files from IndexedDB on first access
- [ ] Add project export/import (JSON format)
- [ ] Handle library installations to /lib

**Architecture:**
```typescript
class CircuitPythonFilesystem {
    private db: IDBDatabase;

    // File operations
    async writeFile(path: string, content: Uint8Array | string): Promise<void>;
    async readFile(path: string): Promise<Uint8Array>;
    async deleteFile(path: string): Promise<void>;
    async listDir(path: string): Promise<FileEntry[]>;
    async exists(path: string): Promise<boolean>;

    // Project management
    async exportProject(): Promise<Blob>;
    async importProject(data: Blob): Promise<void>;

    // Sync with Emscripten VFS
    async syncToVFS(mp): Promise<void>;
    async syncFromVFS(mp): Promise<void>;
}
```

**Storage Structure:**
```javascript
// IndexedDB schema
{
    stores: {
        files: {
            keyPath: 'path',
            indexes: {
                modified: 'modified',
                size: 'size'
            }
        },
        metadata: {
            keyPath: 'key'
        }
    }
}

// File record
{
    path: '/code.py',
    content: Uint8Array,
    modified: 1705932845000,
    size: 1024,
    isDirectory: false
}
```

**Emscripten VFS Integration:**
```c
// Add to main.c or new file: indexeddb_vfs.c
// Custom VFS backend that proxies to JavaScript IndexedDB

// JavaScript side (library.js)
EM_JS(int, indexeddb_open, (const char* path), {
    const pathStr = UTF8ToString(path);
    return Module.filesystem.openFile(pathStr);
});

EM_JS(int, indexeddb_read, (int fd, void* buf, int len), {
    const data = Module.filesystem.readFile(fd, len);
    writeArrayToMemory(data, buf);
    return data.length;
});

EM_JS(int, indexeddb_write, (int fd, const void* buf, int len), {
    const data = new Uint8Array(HEAPU8.buffer, buf, len);
    return Module.filesystem.writeFile(fd, data);
});
```

**Example Usage:**
```javascript
const mp = await loadCircuitPython({
    filesystem: 'indexeddb',  // or 'memory' for current behavior
    autoRun: true
});

// Files persist across sessions!
// Edit code.py, reload page, code.py still there
```

**Estimated Effort:** 1-2 weeks
**Dependencies:** Phase 1
**Risk:** Medium - requires careful VFS integration

---

### Phase 3: IDE Features (User Experience)
**Goal:** Rich editing and browsing experience

**Tasks:**
- [ ] Monaco Editor integration for code editing
- [ ] File browser component (tree view)
- [ ] Syntax highlighting for Python
- [ ] Error highlighting with line numbers
- [ ] Drag-and-drop file upload
- [ ] Library management UI
- [ ] Code autocompletion (CircuitPython API)
- [ ] Virtual serial console (REPL)
- [ ] Download project as .zip
- [ ] Share project via URL (encode in hash)

**Component Architecture:**
```typescript
// Editor component
class CircuitPythonEditor {
    private monaco: monaco.Editor;
    private filesystem: CircuitPythonFilesystem;
    private runtime: CircuitPythonRuntime;

    async openFile(path: string): Promise<void>;
    async saveFile(): Promise<void>;
    async run(): Promise<void>;
    async stop(): Promise<void>;
}

// File browser
class FileBrowser {
    render(files: FileEntry[]): HTMLElement;
    on(event: 'select' | 'delete' | 'rename', handler: Function);
}

// Serial console (REPL)
class SerialConsole {
    private repl: REPL;
    write(text: string): void;
    onInput(handler: (char: string) => void);
}
```

**UI Mockup:**
```
┌─────────────────────────────────────────────────────┐
│  CircuitPython WASM Editor                    ▶ Run │
├──────────────┬──────────────────────────────────────┤
│  Files       │  code.py                             │
│  ├─ boot.py  │  1  import board                     │
│  ├─ code.py  │  2  import digitalio                 │
│  └─ lib/     │  3  import time                      │
│     ├─ neo*  │  4                                   │
│     └─ disp* │  5  led = digitalio.DigitalInOut()   │
│              │  6  led.direction = Direction.OUTPUT │
│              │  7                                    │
│              │  8  while True:                       │
│  + New File  │  9      led.value = not led.value    │
│  📥 Upload   │ 10      time.sleep(0.5)              │
├──────────────┴──────────────────────────────────────┤
│  Console (REPL)                                      │
│  >>> print("Hello from WASM!")                       │
│  Hello from WASM!                                    │
│  >>>                                                 │
└──────────────────────────────────────────────────────┘
```

**Integration with Virtual Hardware:**
```typescript
// Hardware visualization
class VirtualBoard {
    private pins: Map<string, PinState>;
    private canvas: HTMLCanvasElement;

    updatePin(pin: string, value: boolean): void {
        // Update visual representation
        this.pins.set(pin, {value, timestamp: Date.now()});
        this.render();
    }

    render(): void {
        // Draw board with LED states, etc.
    }
}

// Connect to message queue
mp.onHardwareRequest((type, params) => {
    if (type === MSG_TYPE_GPIO_SET) {
        virtualBoard.updatePin(params.pin, params.value);
    }
});
```

**Estimated Effort:** 2-3 weeks
**Dependencies:** Phase 2
**Risk:** Low - mostly UI work

---

## Future Enhancements

### Phase 4: Advanced Features

**Sleep/Wake Simulation:**
```typescript
class PowerManager {
    private virtualClock: VirtualClock;

    async sleep(mode: 'light' | 'deep'): Promise<void> {
        // Pause virtual clock
        this.virtualClock.pause();

        // Simulate power states
        if (mode === 'deep') {
            // Stop most background tasks
            // Only wake on specific events
        }
    }

    wake(reason: 'timer' | 'pin' | 'usb'): void {
        this.virtualClock.resume();
    }
}
```

**Better Error Reporting:**
```typescript
class ErrorReporter {
    report(filename: string, error: PythonError): void {
        // Format like CircuitPython serial output
        console.group(`❌ Error in ${filename}`);
        console.log('Traceback (most recent call last):');
        error.traceback.forEach(frame => {
            console.log(`  File "${frame.file}", line ${frame.line}`);
            console.log(`    ${frame.code}`);
        });
        console.log(`${error.type}: ${error.message}`);
        console.groupEnd();

        // Also show in editor with red underlines
        this.highlightErrorInEditor(error);
    }
}
```

**Library Installation from CDN:**
```typescript
class LibraryManager {
    async installFromBundle(bundleName: string): Promise<void> {
        // Fetch from Adafruit bundles CDN
        const response = await fetch(
            `https://github.com/adafruit/Adafruit_CircuitPython_Bundle/` +
            `releases/latest/download/${bundleName}.mpy`
        );

        const content = await response.arrayBuffer();
        await this.filesystem.writeFile(
            `/lib/${bundleName}.mpy`,
            new Uint8Array(content)
        );
    }

    async search(query: string): Promise<Library[]> {
        // Search Adafruit library index
    }
}
```

**Project Templates:**
```typescript
const templates = {
    'blinky': {
        'code.py': `import board\nimport digitalio\n...`,
        'lib/': ['neopixel.mpy']
    },
    'display': {
        'code.py': `import board\nimport displayio\n...`,
        'lib/': ['adafruit_display_text/', 'adafruit_bitmap_font/']
    }
};

await createProjectFromTemplate('blinky');
```

**WebSerial API Integration:**
```typescript
// Connect to real CircuitPython boards via WebSerial
class HybridRuntime {
    async connectToPhysicalBoard(): Promise<void> {
        const port = await navigator.serial.requestPort();
        // Run code on real hardware for testing
    }

    async deployToBoard(): Promise<void> {
        // Write code.py and lib/ to physical board
    }
}
```

---

## Technical Considerations

### Memory Management
- **Current:** ~625KB total WASM build
- **With IndexedDB:** +50KB for DB abstraction
- **With Monaco:** +2-3MB for editor
- **Target:** Keep runtime < 1MB, editor separate

### Browser Compatibility
- **Required APIs:**
  - WebAssembly (widely supported)
  - IndexedDB (all modern browsers)
  - ES Modules (all modern browsers)
- **Optional APIs:**
  - Monaco Editor (bundle size consideration)
  - WebSerial (Chrome/Edge only - optional feature)

### Performance
- **File I/O:** IndexedDB is async, may need caching layer
- **Code Execution:** WASM performance is excellent
- **Virtual Timing:** Minimal overhead with shared memory

### Security
- **Sandboxing:** WASM provides good isolation
- **Storage Limits:** Respect browser quotas (~50-100MB)
- **Content Security:** Validate user code in sandbox

---

## Success Metrics

### Phase 1 Success:
- ✅ boot.py and code.py auto-execute on load
- ✅ Clear error messages in console
- ✅ 95%+ of simple CircuitPython examples work

### Phase 2 Success:
- ✅ Files persist across page reloads
- ✅ Can install and use libraries from /lib
- ✅ Projects can be exported/imported

### Phase 3 Success:
- ✅ Users can write and edit code without leaving browser
- ✅ Visual feedback from virtual hardware
- ✅ REPL works smoothly
- ✅ 90%+ user satisfaction in testing

---

## Open Questions

1. **Auto-reload behavior:** Should we auto-restart code.py on save like physical boards?
2. **Storage quota:** How to handle running out of IndexedDB space?
3. **Frozen modules:** Which libraries should be bundled vs. user-installed?
4. **Virtual hardware:** How detailed should pin/bus simulation be?
5. **Multi-tab coordination:** Should multiple tabs share the same filesystem?

---

## References

- **Renode Timing System:** `circuitpython/ports/renode/Simple32kHz.cs`
- **RP2040 Supervisor:** `circuitpython/ports/raspberrypi/supervisor/port.c`
- **Emscripten VFS:** https://emscripten.org/docs/api_reference/Filesystem-API.html
- **IndexedDB API:** https://developer.mozilla.org/en-US/docs/Web/API/IndexedDB_API
- **CircuitPython Workflow:** https://learn.adafruit.com/welcome-to-circuitpython

---

## Contributing

This roadmap is a living document. As the WASM port evolves, phases may be reordered, features added/removed, and timelines adjusted based on community feedback and technical discoveries.

**Last Updated:** 2025-01-22
**Status:** Phase 1 planning, Phase 0 (core features) complete
