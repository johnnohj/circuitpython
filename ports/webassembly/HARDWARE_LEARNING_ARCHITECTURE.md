# CircuitPython WebAssembly Hardware Learning Architecture

## Vision: Hardware-Centric Python Learning Platform

This architecture enables web-based CircuitPython learning through real hardware interaction, combining:
- Genuine CircuitPython interpreter for code validation
- Virtual board simulation for offline development
- Real hardware bridging via WebUSB/WebSerial
- Board shadow runtime reflecting actual hardware state

## Multi-Module Architecture

### Core Modules

```
circuitpython-core.wasm (150KB)
├── Python 3.x interpreter
├── Core runtime & GC
├── Basic types & builtins
└── Essential error handling

circuitpython-interpreter.wasm (100KB) 
├── REPL system
├── Code execution engine
├── Import mechanism
└── Minimal debug support

circuitpython-hardware.wasm (200KB)
├── board module with pin definitions
├── digitalio, analogio, busio
├── Hardware abstraction layer
└── Pin state management
```

### Hardware Bridge Modules

```
circuitpython-semihost.wasm (80KB)
├── WebUSB interface layer
├── WebSerial communication
├── U2IF protocol support  
└── File system bridging

circuitpython-blinka.wasm (120KB)
├── Blinka-style hardware abstraction
├── Cross-platform pin mapping
├── Virtual hardware simulation
└── State synchronization
```

### Learning Support Modules

```
circuitpython-simulator.wasm (180KB)
├── Virtual board environments
├── Pin state visualization  
├── Hardware behavior modeling
└── Circuit simulation basics

circuitpython-debugger.wasm (90KB)
├── Variable inspection
├── Execution tracing
├── Hardware state monitoring
└── Learning-focused feedback
```

## Board Shadow Runtime Design

### Core Concept
A "shadow" of the physical board state maintained in the browser that:
- Mirrors real pin states from connected devices
- Provides fallback simulation when no hardware present  
- Enables hybrid physical/virtual development
- Tracks changes bidirectionally

### Implementation

```javascript
class BoardShadowRuntime {
    constructor() {
        this.physicalBoard = null;      // WebUSB/Serial connection
        this.virtualBoard = null;       // Simulated board state
        this.shadowState = new Map();   // Unified pin/peripheral state
        this.syncMode = 'hybrid';       // physical, virtual, or hybrid
    }
    
    // Unified pin interface
    setPin(pinId, value, source = 'code') {
        // Update shadow state
        this.shadowState.set(pinId, { value, source, timestamp: Date.now() });
        
        // Sync to physical if available
        if (this.physicalBoard && source !== 'physical') {
            this.physicalBoard.setPin(pinId, value);
        }
        
        // Update virtual representation
        if (this.virtualBoard) {
            this.virtualBoard.setPin(pinId, value);
        }
        
        // Notify listeners (UI, debugger, etc.)
        this.notifyPinChange(pinId, value, source);
    }
    
    getPin(pinId) {
        return this.shadowState.get(pinId)?.value ?? 0;
    }
    
    // Hardware discovery and connection
    async connectPhysicalBoard() {
        // Try U2IF first
        try {
            this.physicalBoard = await U2IFBoard.connect();
            this.syncMode = 'hybrid';
            return 'u2if';
        } catch (e) {}
        
        // Try WebSerial 
        try {
            this.physicalBoard = await WebSerialBoard.connect();
            this.syncMode = 'hybrid';
            return 'webserial';
        } catch (e) {}
        
        // Fall back to virtual only
        this.syncMode = 'virtual';
        return 'virtual';
    }
}
```

## Hardware Interface Implementations

### WebUSB U2IF Bridge

```javascript
class U2IFBoard {
    static async connect() {
        const device = await navigator.usb.requestDevice({
            filters: [{ vendorId: 0x239A }] // Adafruit VID
        });
        
        await device.open();
        await device.selectConfiguration(1);
        await device.claimInterface(0);
        
        return new U2IFBoard(device);
    }
    
    async setPin(pinId, value) {
        const command = new Uint8Array([
            0x01,           // GPIO command
            pinId,          // Pin number
            value ? 1 : 0   // Value
        ]);
        
        await this.device.transferOut(1, command);
    }
    
    async readPin(pinId) {
        const command = new Uint8Array([0x02, pinId]); // Read GPIO
        await this.device.transferOut(1, command);
        
        const result = await this.device.transferIn(1, 1);
        return result.data.getUint8(0);
    }
}
```

### WebSerial CircuitPython Bridge

```javascript
class WebSerialBoard {
    static async connect() {
        const port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 });
        
        const board = new WebSerialBoard(port);
        await board.initialize();
        return board;
    }
    
    async initialize() {
        // Send Ctrl+C to interrupt REPL
        await this.send(new Uint8Array([0x03]));
        
        // Switch to raw REPL mode
        await this.send(new Uint8Array([0x01])); 
        
        // Set up pin control functions
        await this.executeCode(`
import board
import digitalio
_pins = {}

def _set_pin(pin_name, value):
    if pin_name not in _pins:
        pin = getattr(board, pin_name)
        _pins[pin_name] = digitalio.DigitalInOut(pin)
        _pins[pin_name].direction = digitalio.Direction.OUTPUT
    _pins[pin_name].value = value

def _get_pin(pin_name):
    if pin_name not in _pins:
        pin = getattr(board, pin_name)  
        _pins[pin_name] = digitalio.DigitalInOut(pin)
        _pins[pin_name].direction = digitalio.Direction.INPUT
    return _pins[pin_name].value
        `);
    }
    
    async setPin(pinId, value) {
        await this.executeCode(`_set_pin('${pinId}', ${value})`);
    }
}
```

## Virtual Board Simulator

### Pin State Visualization

```javascript
class VirtualBoardVisualizer {
    constructor(boardType = 'pico') {
        this.canvas = document.createElement('canvas');
        this.ctx = this.canvas.getContext('2d');
        this.boardLayout = this.getBoardLayout(boardType);
        this.pinStates = new Map();
    }
    
    getBoardLayout(boardType) {
        const layouts = {
            'pico': {
                pins: [
                    { name: 'GP0', x: 20, y: 50, type: 'digital' },
                    { name: 'GP1', x: 20, y: 70, type: 'digital' },
                    { name: 'GP2', x: 20, y: 90, type: 'digital' },
                    // ... more pins
                ],
                size: { width: 400, height: 200 }
            },
            'feather': {
                // Adafruit Feather layout
            }
        };
        return layouts[boardType] || layouts.pico;
    }
    
    updatePin(pinName, value) {
        this.pinStates.set(pinName, value);
        this.redraw();
    }
    
    redraw() {
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        
        // Draw board outline
        this.ctx.strokeRect(10, 10, this.boardLayout.size.width, this.boardLayout.size.height);
        
        // Draw pins
        for (const pin of this.boardLayout.pins) {
            const state = this.pinStates.get(pin.name) || 0;
            
            // Pin representation
            this.ctx.beginPath();
            this.ctx.arc(pin.x, pin.y, 5, 0, 2 * Math.PI);
            this.ctx.fillStyle = state ? '#00ff00' : '#666666';
            this.ctx.fill();
            
            // Pin label
            this.ctx.fillStyle = 'black';
            this.ctx.fillText(pin.name, pin.x + 8, pin.y + 3);
        }
    }
}
```

## Learning-Focused Features

### Beginner-Friendly Error Messages

```javascript
class LearningErrorHandler {
    static enhanceError(error, context) {
        const commonErrors = {
            'NameError': {
                pattern: /'(.+)' is not defined/,
                help: (match) => `The variable '${match[1]}' hasn't been created yet. Did you forget to import a module or define a variable?`,
                examples: [`import board`, `led = board.LED`]
            },
            'AttributeError': {
                pattern: /'(.+)' object has no attribute '(.+)'/,
                help: (match) => `The ${match[1]} doesn't have a ${match[2]} property. Check the documentation for available options.`,
                suggestions: (match) => this.suggestSimilarAttributes(match[1], match[2])
            },
            'RuntimeError': {
                pattern: /Pin .+ is already in use/,
                help: () => `This pin is being used by another part of your code. Each pin can only be used once at a time.`,
                solution: `Use pin.deinit() to free the pin, or choose a different pin.`
            }
        };
        
        // Enhanced error with learning context
        return {
            original: error,
            explanation: this.getExplanation(error, commonErrors),
            suggestions: this.getSuggestions(error, context),
            documentation: this.getRelevantDocs(error)
        };
    }
}
```

### Hardware State Debugging

```javascript
class HardwareDebugger {
    constructor(shadowRuntime) {
        this.shadow = shadowRuntime;
        this.watchedPins = new Set();
        this.history = [];
    }
    
    watchPin(pinId) {
        this.watchedPins.add(pinId);
        this.shadow.addPinListener(pinId, (value, source) => {
            this.history.push({
                pin: pinId,
                value,
                source,
                timestamp: Date.now(),
                stackTrace: this.getCodeLocation()
            });
            
            this.notifyWatchers(pinId, value, source);
        });
    }
    
    getPinHistory(pinId, timeRange = 5000) {
        const cutoff = Date.now() - timeRange;
        return this.history
            .filter(entry => entry.pin === pinId && entry.timestamp > cutoff)
            .sort((a, b) => b.timestamp - a.timestamp);
    }
    
    generateHardwareReport() {
        return {
            connectedBoard: this.shadow.physicalBoard ? 'Connected' : 'Virtual Only',
            pinStates: Array.from(this.shadow.shadowState.entries()),
            recentActivity: this.history.slice(-10),
            recommendations: this.generateRecommendations()
        };
    }
}
```

## Build System Integration

### Multi-Target Makefile

```makefile
# Separate build targets for different use cases

.PHONY: minimal interpreter hardware semihost simulator all-modules

# Minimal interpreter - just Python + basic CircuitPython
minimal:
	$(MAKE) VARIANT=minimal BUILD=build-minimal \
		SRC_MODULES=core \
		SIZE_TARGET=150KB

# Full interpreter with REPL and debugging  
interpreter:
	$(MAKE) VARIANT=interpreter BUILD=build-interpreter \
		SRC_MODULES="core repl debug" \
		SIZE_TARGET=250KB

# Hardware abstraction layer
hardware:
	$(MAKE) VARIANT=hardware BUILD=build-hardware \
		SRC_MODULES="board digitalio analogio busio" \
		EMSCRIPTEN_FEATURES="-s SIDE_MODULE=1" \
		SIZE_TARGET=200KB

# Semihosting and bridge modules
semihost:
	$(MAKE) VARIANT=semihost BUILD=build-semihost \
		SRC_MODULES="webusb webserial u2if blinka" \
		EMSCRIPTEN_FEATURES="-s SIDE_MODULE=1" \
		SIZE_TARGET=200KB

# All modules
all-modules: minimal interpreter hardware semihost simulator
	@echo "Built all CircuitPython WebAssembly modules"
```

This architecture provides:

1. **Genuine CircuitPython** - Real interpreter for code validation
2. **Hardware Learning** - Virtual boards + real device integration  
3. **Beginner-Friendly** - Enhanced errors, visual debugging, guided learning
4. **Modular Design** - Load only needed components
5. **Real Hardware Bridge** - WebUSB/WebSerial to actual devices
6. **Board Shadow** - Unified virtual/physical state management

The key insight: This isn't just a web-based Python interpreter—it's a **hardware learning platform** that bridges the gap between browser-based development and real microcontroller programming.