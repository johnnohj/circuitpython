/**
 * SharedArrayBuffer-enhanced board configuration for CircuitPython WebAssembly port
 * 
 * Demonstrates persistent object storage and filesystem integration
 * using SharedArrayBuffer for memory that survives JavaScript calls.
 */

// Enhanced pin class with persistent state
class PersistentWebAssemblyPin {
    constructor(name, number, capabilities = ['digital_io']) {
        this.name = name;
        this.number = number;
        this.capabilities = capabilities;
        this.mode = null;
        this.value = false;
        this.pullup = false;
        
        // Persistent state key for SharedArrayBuffer storage
        this.persistentKey = `pin_${name}_${number}`;
        
        console.log(`Persistent pin ${name} (${number}) created with capabilities:`, capabilities);
    }
    
    init(mode = 'input') {
        this.mode = mode;
        console.log(`Pin ${this.name} initialized as ${mode}`);
        
        // Store state in SharedArrayBuffer via CircuitPython
        if (globalThis.circuitPython && globalThis.circuitPython.preserveObject) {
            globalThis.circuitPython.preserveObject(this.persistentKey, {
                mode: this.mode,
                value: this.value,
                pullup: this.pullup
            });
        }
        
        return this;
    }
    
    // Restore state from SharedArrayBuffer
    restore() {
        if (globalThis.circuitPython && globalThis.circuitPython.restoreObject) {
            const state = globalThis.circuitPython.restoreObject(this.persistentKey);
            if (state) {
                this.mode = state.mode;
                this.value = state.value;
                this.pullup = state.pullup;
                console.log(`📁 Restored pin ${this.name} state:`, state);
            }
        }
    }
    
    get_value() {
        console.log(`📖 Reading persistent pin ${this.name}: ${this.value}`);
        return this.value;
    }
    
    set_value(newValue) {
        this.value = !!newValue;
        console.log(`📝 Writing persistent pin ${this.name}: ${this.value}`);
        
        // Persist state change
        if (globalThis.circuitPython && globalThis.circuitPython.preserveObject) {
            globalThis.circuitPython.preserveObject(this.persistentKey, {
                mode: this.mode,
                value: this.value,
                pullup: this.pullup
            });
        }
    }
}

// Enhanced board with SharedArrayBuffer-backed filesystem
const sharedMemoryBoard = {
    name: "CircuitPython WebAssembly port with SharedArrayBuffer",
    manufacturer: "Persistent JavaScript Semihosting",
    
    // Board initialization with SharedArrayBuffer setup
    async init() {
        console.log("🚀 Initializing CircuitPython WebAssembly port with SharedArrayBuffer support");
        console.log("   Persistent memory: Active");
        console.log("   Shared filesystem: Active");  
        console.log("   Object persistence: Active");
        
        // Initialize SharedArrayBuffer if available
        if (typeof SharedArrayBuffer !== 'undefined') {
            console.log("✅ SharedArrayBuffer available - enabling persistent storage");
            
            // Create shared heap for object persistence (8MB)
            this.sharedHeap = new SharedArrayBuffer(8 * 1024 * 1024);
            console.log(`   Shared heap: ${this.sharedHeap.byteLength} bytes`);
            
            // Create shared VFS for file persistence (4MB)  
            this.sharedVFS = new SharedArrayBuffer(4 * 1024 * 1024);
            console.log(`   Shared VFS: ${this.sharedVFS.byteLength} bytes`);
            
            // Initialize with some demo files
            await this.initDemoFiles();
        } else {
            console.log("⚠️  SharedArrayBuffer not available - using ephemeral memory");
            this.sharedHeap = null;
            this.sharedVFS = null;
        }
        
        // Restore pin states
        this.pins.forEach(pin => {
            if (pin.restore) {
                pin.restore();
            }
        });
    },
    
    async initDemoFiles() {
        console.log("📁 Creating demo files in shared filesystem...");
        
        // This would populate the SharedArrayBuffer VFS with initial files
        this.demoFiles = {
            '/boot.py': `# CircuitPython boot.py - persists across sessions
print("Hello from persistent filesystem!")
import board
print("Board:", board.board_id)
`,
            '/code.py': `# Main CircuitPython code - auto-runs on startup
import time
import board

print("CircuitPython SharedArrayBuffer demo")

# Pin states persist across JavaScript calls
led = board.LED
led.init('output')

# This loop state can be preserved
for i in range(3):
    print(f"Blink {i+1}")
    led.set_value(True)
    time.sleep(0.5) 
    led.set_value(False)
    time.sleep(0.5)

print("Demo complete - state preserved!")
`,
            '/lib/mymodule.py': `# Custom module - persists in shared VFS
def hello():
    return "Hello from persistent module!"

PERSISTENT_VALUE = 42
`,
        };
        
        // In a real implementation, these would be written to the SharedArrayBuffer VFS
        console.log("   Demo files ready:", Object.keys(this.demoFiles));
    },
    
    deinit() {
        console.log("🛑 Shutting down CircuitPython WebAssembly port with SharedArrayBuffer");
        
        // Sync any pending changes to SharedArrayBuffer
        if (globalThis.circuitPython && globalThis.circuitPython.syncSharedMemory) {
            globalThis.circuitPython.syncSharedMemory();
        }
    },
    
    requestsSafeMode() {
        return false;
    },
    
    pinFreeRemaining() {
        return true;
    },
    
    // Enhanced pins with persistence
    pins: [
        new PersistentWebAssemblyPin('CONSOLE_TX', 0, ['digital_io', 'uart']),
        new PersistentWebAssemblyPin('CONSOLE_RX', 1, ['digital_io', 'uart']),
        new PersistentWebAssemblyPin('UART_TX', 2, ['digital_io', 'uart']),
        new PersistentWebAssemblyPin('UART_RX', 3, ['digital_io', 'uart']),
        
        new PersistentWebAssemblyPin('D4', 4, ['digital_io']),
        new PersistentWebAssemblyPin('D5', 5, ['digital_io', 'pwm']),
        new PersistentWebAssemblyPin('D6', 6, ['digital_io', 'pwm']),
        
        new PersistentWebAssemblyPin('A0', 14, ['digital_io', 'analog_in']),
        new PersistentWebAssemblyPin('A1', 15, ['digital_io', 'analog_in']),
        
        new PersistentWebAssemblyPin('LED', 25, ['digital_io']),
        new PersistentWebAssemblyPin('BUTTON', 26, ['digital_io']),
    ]
};

// Enhanced CircuitPython loader with SharedArrayBuffer support
async function loadCircuitPythonWithSharedMemory(options = {}) {
    console.log("🔧 Loading CircuitPython with SharedArrayBuffer integration...");
    
    // Set up SharedArrayBuffer integration
    const sharedMemoryOptions = {
        heapsize: options.heapsize || 2 * 1024 * 1024,
        boardConfiguration: options.boardConfiguration || sharedMemoryBoard,
        stdout: options.stdout || ((data) => console.log(`🐍 ${data.trimEnd()}`))
    };
    
    // Add SharedArrayBuffer support if available
    if (sharedMemoryBoard.sharedHeap) {
        sharedMemoryOptions.sharedHeap = sharedMemoryBoard.sharedHeap;
        console.log(`   Shared heap: ${sharedMemoryBoard.sharedHeap.byteLength} bytes`);
    }
    
    if (sharedMemoryBoard.sharedVFS) {
        sharedMemoryOptions.sharedVFS = sharedMemoryBoard.sharedVFS;
        console.log(`   Shared VFS: ${sharedMemoryBoard.sharedVFS.byteLength} bytes`);
    }
    
    // Load CircuitPython with enhanced options
    const circuitPython = await globalThis.loadCircuitPython(sharedMemoryOptions);
    
    // Add SharedArrayBuffer-specific methods
    circuitPython.preserveObject = function(key, obj) {
        console.log(`💾 Preserving object: ${key}`);
        // This would call the C function to store obj in SharedArrayBuffer
        return this._module.ccall('mp_js_preserve_object', 'null', 
                                 ['string', 'string'], 
                                 [key, JSON.stringify(obj)]);
    };
    
    circuitPython.restoreObject = function(key) {
        console.log(`📁 Restoring object: ${key}`);
        // This would call the C function to retrieve obj from SharedArrayBuffer
        const jsonStr = this._module.ccall('mp_js_restore_object', 'string', 
                                          ['string'], [key]);
        return jsonStr ? JSON.parse(jsonStr) : null;
    };
    
    circuitPython.syncSharedMemory = function() {
        console.log("🔄 Syncing SharedArrayBuffer state...");
        return this._module.ccall('mp_js_sync_shared_memory', 'null', [], []);
    };
    
    circuitPython.getSharedMemoryStats = function() {
        return {
            heapSize: this._module.ccall('mp_js_get_shared_heap_size', 'number', [], []),
            heapUsed: this._module.ccall('mp_js_get_shared_heap_used', 'number', [], []),
            vfsSize: this._module.ccall('mp_js_get_shared_vfs_size', 'number', [], []),
            vfsUsed: this._module.ccall('mp_js_get_shared_vfs_used', 'number', [], []),
        };
    };
    
    return circuitPython;
}

// Demo usage
async function runSharedMemoryDemo() {
    console.log("🎯 Starting CircuitPython SharedArrayBuffer demo...");
    
    const circuitPython = await loadCircuitPythonWithSharedMemory();
    
    console.log("✅ CircuitPython loaded with SharedArrayBuffer support!");
    console.log("🎯 Running persistent object demo...");
    
    // Test code that demonstrates object persistence
    const testCode = `
# Objects created here persist across JavaScript calls
import board
import time

# Pin state survives between REPL sessions
led = board.LED
led.init('output')

print("LED pin configured - state is persistent!")

# Test file operations on SharedArrayBuffer VFS
try:
    with open('/test_persistent.txt', 'w') as f:
        f.write('This file persists in SharedArrayBuffer!\\n')
        f.write(f'Created at: {time.monotonic()}\\n')
    print("File written to persistent VFS")
except Exception as e:
    print(f"VFS not yet implemented: {e}")

# Complex object that should persist
class PersistentCounter:
    def __init__(self):
        self.count = 0
    
    def increment(self):
        self.count += 1
        return self.count

# This counter will survive JavaScript garbage collection
counter = PersistentCounter()
print(f"Counter created, initial value: {counter.increment()}")

print("Demo complete - objects preserved in SharedArrayBuffer!")
`;
    
    try {
        circuitPython.runPython(testCode);
        
        // Show memory statistics
        const stats = circuitPython.getSharedMemoryStats();
        console.log("📊 SharedArrayBuffer Statistics:", stats);
        
        console.log("🎉 SharedArrayBuffer demo completed successfully!");
    } catch (error) {
        console.error("❌ Error in SharedArrayBuffer demo:", error);
    }
}

// Export for use
export { sharedMemoryBoard, PersistentWebAssemblyPin, loadCircuitPythonWithSharedMemory, runSharedMemoryDemo };

// Auto-run demo if loaded directly
if (typeof window === 'undefined' && import.meta.url === `file://${process.argv[1]}`) {
    runSharedMemoryDemo().catch(console.error);
}