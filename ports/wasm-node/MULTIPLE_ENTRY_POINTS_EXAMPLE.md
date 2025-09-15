# CircuitPython WebAssembly - Multiple Entry Points Usage Guide

## Overview

The CircuitPython WebAssembly port now provides specialized entry points for different runtime environments and use cases. Each entry point is optimized for specific scenarios while maintaining API compatibility.

## Available Entry Points

### 1. **Node.js Optimized** (`import { nodeCtPy }`)
**Best for:** Production hardware development, device control, file synchronization

### 2. **Browser Optimized** (`import { browserCtPy }`)  
**Best for:** Interactive learning, educational platforms, web-based development

### 3. **Web Worker Optimized** (`import { workerCtPy }`)
**Best for:** Non-blocking execution, parallel processing, background tasks

### 4. **Universal** (`import { universalCtPy }`)
**Best for:** Auto-detecting environment, maximum compatibility

### 5. **Minimal** (`import { minimalCtPy }`)
**Best for:** Code validation, minimal footprint, fast initialization

## Usage Examples

### Node.js Entry Point

```javascript
// examples/node-hardware-control.mjs
import { nodeCtPy } from 'circuitpython-wasm/node';

async function main() {
    // Initialize with Node.js optimizations
    const cp = await nodeCtPy({
        enableFileSync: true,
        enableDeviceDiscovery: true,
        autoConnect: true
    });
    
    // Execute hardware control code
    await cp.execute(`
import board
import digitalio
import time

led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

for i in range(10):
    led.value = True
    time.sleep(0.5)
    led.value = False
    time.sleep(0.5)
    print(f"Blink {i+1}")
    `);
    
    // Write code to physical device
    await cp.writeCodeToDevice('blink.py', code);
    
    // Get comprehensive status
    console.log('Status:', cp.getStatus());
}

main().catch(console.error);
```

### Browser Entry Point

```html
<!-- examples/browser-learning-platform.html -->
<!DOCTYPE html>
<html>
<head>
    <title>CircuitPython Learning Platform</title>
</head>
<body>
    <div id="circuitpy-app"></div>
    
    <script type="module">
        import { browserCtPy } from './node_modules/circuitpython-wasm/browser';
        
        async function initLearningPlatform() {
            // Initialize with browser optimizations
            const cp = await browserCtPy({
                visualizationTarget: document.getElementById('circuitpy-app'),
                enableTouchInterface: true,
                enableKeyboardShortcuts: true,
                theme: 'auto'
            });
            
            // Execute interactive code
            await cp.execute(`
import board
import digitalio

# Create interactive LED control
led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

print("LED control ready! Click pins to interact.")
            `);
            
            // Set up hardware interaction
            document.addEventListener('keydown', async (e) => {
                if (e.key === 'l') {
                    await cp.execute('led.value = not led.value');
                }
            });
        }
        
        initLearningPlatform().catch(console.error);
    </script>
</body>
</html>
```

### Web Worker Entry Point

```javascript
// examples/worker-parallel-processing.mjs
import { workerCtPy } from 'circuitpython-wasm/worker';

async function runParallelTasks() {
    // Initialize with worker optimizations
    const cp = await workerCtPy({
        enableParallelExecution: true,
        maxConcurrentTasks: 4,
        enableSharedMemory: true
    });
    
    // Execute computationally intensive code in worker
    const tasks = [];
    
    for (let i = 0; i < 10; i++) {
        tasks.push(cp.execute(`
# Parallel sensor data processing
import math
import time

def process_sensor_data(samples):
    results = []
    for sample in samples:
        # Complex processing
        processed = math.sin(sample * math.pi / 180) * 1000
        results.append(processed)
        time.sleep(0.001)  # Simulate processing time
    return results

# Process data batch ${i}
data = [x * 10 for x in range(100)]
result = process_sensor_data(data)
print(f"Batch ${i} processed: {len(result)} samples")
        `));
    }
    
    // Wait for all tasks to complete
    const results = await Promise.all(tasks);
    console.log('All parallel tasks completed:', results.length);
}

runParallelTasks().catch(console.error);
```

### Universal Entry Point  

```javascript
// examples/universal-cross-platform.mjs
import { universalCtPy } from 'circuitpython-wasm/universal';

async function crossPlatformApp() {
    // Automatically detects best implementation
    const cp = await universalCtPy({
        fallbackToVirtual: true,
        enableOptimalFeatures: true
    });
    
    // Get environment recommendations
    const recommendations = cp.getUsageRecommendations();
    console.log('Environment:', recommendations);
    
    // Execute code that works everywhere
    await cp.execute(`
import sys
import board
import digitalio

print(f"Running on: {sys.platform}")
print(f"CircuitPython version: {sys.version}")

# Hardware abstraction works across all environments
led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

# This works in Node.js, browser, worker, or virtual mode
for i in range(3):
    led.value = True
    print(f"LED on ({i+1})")
    time.sleep(0.5)
    led.value = False
    print(f"LED off ({i+1})")
    time.sleep(0.5)
    `);
    
    // Try to connect hardware (works if available)
    try {
        const connection = await cp.connectHardware();
        console.log('Hardware connected:', connection);
    } catch (error) {
        console.log('Hardware not available, using simulation');
    }
}

crossPlatformApp().catch(console.error);
```

### Minimal Entry Point

```javascript
// examples/minimal-validation.mjs
import { minimalCtPy, validateCode } from 'circuitpython-wasm/minimal';

async function codeValidationExample() {
    // Quick code validation (ultra-fast)
    const validation = await validateCode(`
import board
import digitalio

led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT
led.value = True
    `);
    
    console.log('Code validation:', validation);
    
    if (validation.valid) {
        // Initialize minimal interpreter
        const cp = await minimalCtPy({
            enableVirtualHardware: true,
            enableBasicModules: true
        });
        
        // Execute with minimal overhead
        const result = await cp.execute(`
import board
import digitalio
import time

led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

for i in range(5):
    led.value = not led.value
    time.sleep(0.2)
    print(f"Toggle {i+1}")
        `);
        
        console.log('Execution result:', result);
        console.log('Performance metrics:', cp.getMetrics());
    }
}

codeValidationExample().catch(console.error);
```

## Entry Point Comparison

| Feature | Node.js | Browser | Worker | Universal | Minimal |
|---------|---------|---------|--------|-----------|---------|
| **Hardware Access** | ✅ Native | ✅ WebSerial/USB | ❌ Virtual | ✅ Auto-detect | ❌ Virtual |
| **File System** | ✅ Full | ❌ Limited | ❌ None | ✅ If available | ❌ None |
| **Visualization** | ❌ CLI only | ✅ Full UI | ❌ None | ✅ If browser | ❌ None |
| **Parallel Execution** | ✅ Limited | ❌ Main thread | ✅ Full | ✅ If worker | ❌ Single thread |
| **Memory Usage** | ~16MB | ~8MB | ~12MB | Variable | ~2MB |
| **Init Time** | ~500ms | ~800ms | ~1000ms | Variable | ~150ms |
| **Use Case** | Production | Learning | Computation | Flexible | Validation |

## Advanced Usage Patterns

### Hybrid Approach - Multiple Entry Points

```javascript
// Use different entry points for different tasks
import { nodeCtPy } from 'circuitpython-wasm/node';
import { workerCtPy } from 'circuitpython-wasm/worker';
import { validateCode } from 'circuitpython-wasm/minimal';

class HybridCircuitPython {
    constructor() {
        this.validator = null;
        this.nodeInstance = null;
        this.workerInstance = null;
    }
    
    async init() {
        // Fast validation with minimal
        this.validator = await minimalCtPy({ virtualOnly: true });
        
        // Hardware control with Node.js
        this.nodeInstance = await nodeCtPy({ autoConnect: true });
        
        // Heavy computation with worker
        this.workerInstance = await workerCtPy({ 
            enableParallelExecution: true 
        });
    }
    
    async validateAndExecute(code) {
        // 1. Fast validation
        const validation = await validateCode(code);
        if (!validation.valid) {
            throw new Error(`Invalid code: ${validation.error}`);
        }
        
        // 2. Determine execution strategy
        if (code.includes('time.sleep') && code.includes('for')) {
            // Use worker for potentially long-running code
            return await this.workerInstance.execute(code);
        } else {
            // Use Node.js for hardware control
            return await this.nodeInstance.execute(code);
        }
    }
}
```

### Environment-Specific Conditional Loading

```javascript
// Dynamic loading based on environment
async function createOptimalCircuitPython() {
    if (typeof process !== 'undefined' && process.versions?.node) {
        // Node.js environment
        const { nodeCtPy } = await import('circuitpython-wasm/node');
        return await nodeCtPy({ enableFileSync: true });
        
    } else if (typeof Worker !== 'undefined') {
        // Browser with worker support
        const { workerCtPy } = await import('circuitpython-wasm/worker');
        return await workerCtPy({ enableParallelExecution: true });
        
    } else if (typeof navigator !== 'undefined') {
        // Browser main thread
        const { browserCtPy } = await import('circuitpython-wasm/browser');
        return await browserCtPy({ enableVisualization: true });
        
    } else {
        // Fallback to minimal
        const { minimalCtPy } = await import('circuitpython-wasm/minimal');
        return await minimalCtPy({ enableVirtualHardware: true });
    }
}
```

## Performance Optimization Tips

### Node.js Optimizations
```javascript
const cp = await nodeCtPy({
    heapSize: 32 * 1024 * 1024,  // More memory for complex projects
    syncInterval: 50,             // Faster hardware sync
    enableAdvancedLogging: false  // Reduce overhead
});
```

### Browser Optimizations
```javascript
const cp = await browserCtPy({
    heapSize: 4 * 1024 * 1024,   // Less memory for mobile
    enableLocalStorage: true,     // Persist state
    theme: 'dark'                // Reduce eye strain
});
```

### Worker Optimizations
```javascript
const cp = await workerCtPy({
    enableSharedMemory: true,     // Use SharedArrayBuffer
    maxConcurrentTasks: navigator.hardwareConcurrency,
    messageTimeout: 15000         // Longer timeout for heavy tasks
});
```

## Troubleshooting

### Common Issues and Solutions

1. **"Module not found" errors**
   ```javascript
   // Ensure correct import path
   import { nodeCtPy } from 'circuitpython-wasm/node';  // ✅ Correct
   import { nodeCtPy } from 'circuitpython-wasm';       // ❌ Wrong
   ```

2. **Hardware access denied**
   ```javascript
   // Request permissions explicitly in browser
   const cp = await browserCtPy({ autoRequestDeviceAccess: true });
   ```

3. **Worker communication timeout**
   ```javascript
   // Increase timeout for heavy computation
   const cp = await workerCtPy({ messageTimeout: 30000 });
   ```

4. **Memory issues**
   ```javascript
   // Use minimal for memory-constrained environments
   const cp = await minimalCtPy({ heapSize: 1024 * 1024 });
   ```

## Summary

The multiple entry point architecture provides:

- **Specialized optimization** for each runtime environment
- **Consistent API** across all entry points
- **Flexible deployment** options for different use cases
- **Performance tuning** for specific scenarios
- **Graceful fallbacks** when features aren't available

This enables CircuitPython WebAssembly to excel in each environment while maintaining the unified learning experience that makes CircuitPython ideal for hardware-centric education.