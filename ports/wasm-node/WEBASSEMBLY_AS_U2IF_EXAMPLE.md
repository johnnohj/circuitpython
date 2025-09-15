# WebAssembly as U2IF Bridge - Complete Example

## The Revolutionary Approach

Instead of running code on the physical device, we run it in WebAssembly with hardware operations forwarded to the real device. This enables:

- **Full debugging in browser** - breakpoints, variable inspection, step-through
- **Enhanced error messages** - better than device limitations
- **Hybrid simulation** - some pins virtual, some physical
- **Real-time visualization** - see exactly what's happening
- **Unlimited code space** - not constrained by device memory

## Architecture Overview

```
┌─────────────────────┐    ┌──────────────────┐    ┌─────────────────────┐
│   Browser/WASM      │    │   Bridge Layer   │    │  Physical Device    │
│                     │    │                  │    │                     │
│ CircuitPython Code  │ ─► │ Command Queue    │ ─► │  Hardware Actions   │
│ ├─ Debugging        │    │ ├─ digital_write │    │  ├─ LED Control     │
│ ├─ Visualization    │    │ ├─ analog_read   │    │  ├─ Sensor Reading  │
│ └─ Error Handling   │    │ └─ pwm_write     │    │  └─ Motor Control   │
│                     │    │                  │    │                     │
│ Virtual Sensors ◄───┼────┼─ State Sync     │ ◄─ │  Real Sensors       │
└─────────────────────┘    └──────────────────┘    └─────────────────────┘
```

## Usage Example

### HTML Integration

```html
<!DOCTYPE html>
<html>
<head>
    <title>WebAssembly U2IF Bridge Demo</title>
</head>
<body>
    <h1>🔗 WebAssembly as Hardware Bridge</h1>
    
    <div class="controls">
        <button onclick="connectDevice()">🔌 Connect Physical Device</button>
        <button onclick="runDemo()">▶️ Run Demo Code</button>
        <div id="status">Ready</div>
    </div>
    
    <div class="code-area">
        <textarea id="python-code" rows="15">
import board
import digitalio
import analogio
import time

# Set up LED (will control physical LED)
led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

# Set up button (will read from physical button)
button = digitalio.DigitalInOut(board.BUTTON)
button.direction = digitalio.Direction.INPUT
button.pull = digitalio.Pull.UP

# Set up sensor (will read from physical sensor)
sensor = analogio.AnalogIn(board.A0)

print("Hardware bridge demo starting...")

while True:
    # Read real button
    if not button.value:
        print("Button pressed!")
        
        # Blink LED on physical device
        for i in range(5):
            led.value = True
            time.sleep(0.2)
            led.value = False
            time.sleep(0.2)
    
    # Read real sensor
    sensor_value = sensor.value
    voltage = sensor_value * 3.3 / 65535
    print(f"Sensor: {voltage:.2f}V")
    
    # Control LED brightness based on sensor
    led_brightness = sensor_value / 65535
    # This would use PWM in real implementation
    led.value = sensor_value > 32767
    
    time.sleep(0.1)
        </textarea>
    </div>
    
    <div class="visualization">
        <h3>Real-time Hardware State</h3>
        <div id="pin-states"></div>
        <div id="debug-info"></div>
    </div>

    <script type="module">
        import { createCircuitPython } from './circuitpython-bridge.js';
        import WebAssemblyU2IFBridge from './webassembly-u2if-bridge.js';
        
        let circuitPython = null;
        let bridge = null;
        
        // Initialize the system
        async function init() {
            // Create bridge
            bridge = new WebAssemblyU2IFBridge({
                enableLogging: true,
                enableBidirectional: true
            });
            
            // Make bridge globally accessible
            window.wasmU2IFBridge = bridge;
            
            // Initialize CircuitPython with bridge integration
            circuitPython = await createCircuitPython({
                onOutput: (text) => {
                    console.log('[CircuitPython]', text);
                    updateStatus(text);
                },
                onError: (text) => {
                    console.error('[CircuitPython Error]', text);
                    updateStatus(`Error: ${text}`, 'error');
                }
            });
            
            // Install hardware shims
            bridge.createHardwareShims(circuitPython);
            
            updateStatus('✅ System ready - connect a physical device to begin');
        }
        
        // Connect to physical device
        async function connectDevice() {
            try {
                updateStatus('🔌 Connecting to physical device...');
                
                const connectionType = await bridge.connect();
                
                updateStatus(`✅ Connected via ${connectionType}!`, 'success');
                
                // Start monitoring bridge status
                setInterval(updateBridgeStatus, 1000);
                
            } catch (error) {
                updateStatus(`❌ Connection failed: ${error.message}`, 'error');
            }
        }
        
        // Run the demo code
        async function runDemo() {
            if (!bridge.isConnected) {
                updateStatus('⚠️ Connect a physical device first', 'warning');
                return;
            }
            
            const code = document.getElementById('python-code').value;
            
            try {
                updateStatus('🏃 Running code on WebAssembly (hardware via bridge)...');
                
                const result = await circuitPython.execute(code);
                
                if (result.success) {
                    updateStatus('✅ Code running successfully');
                } else {
                    updateStatus(`❌ Code failed: ${result.error}`, 'error');
                }
                
            } catch (error) {
                updateStatus(`💥 Error: ${error.message}`, 'error');
            }
        }
        
        // Update bridge status display
        function updateBridgeStatus() {
            if (!bridge) return;
            
            const status = bridge.getStatus();
            const statusHtml = `
                <h4>Bridge Status</h4>
                <div>Connected: ${status.connected ? '✅' : '❌'}</div>
                <div>Connection: ${status.connectionType || 'None'}</div>
                <div>Command Queue: ${status.queueLength} pending</div>
                <div>Processing: ${status.processingCommands ? '🏃' : '⏸️'}</div>
                <div>Bidirectional Sync: ${status.bidirectionalSync ? '🔄' : '➡️'}</div>
                <div>Virtual States: ${status.virtualStates}</div>
                <div>Physical States: ${status.physicalStates}</div>
            `;
            
            document.getElementById('debug-info').innerHTML = statusHtml;
            
            // Update pin states
            updatePinStates();
        }
        
        // Update pin state visualization
        function updatePinStates() {
            if (!bridge) return;
            
            const pinStatesHtml = Array.from(bridge.virtualState.entries())
                .map(([key, value]) => {
                    const [type, pin] = key.split('_');
                    return `<div class="pin-state">
                        <span class="pin-name">${pin}</span>
                        <span class="pin-type">(${type})</span>
                        <span class="pin-value">${typeof value === 'number' ? value.toFixed(3) : value}</span>
                    </div>`;
                })
                .join('');
            
            document.getElementById('pin-states').innerHTML = pinStatesHtml || '<div>No pin activity yet</div>';
        }
        
        // Utility functions
        function updateStatus(message, type = 'info') {
            const statusElement = document.getElementById('status');
            statusElement.textContent = message;
            statusElement.className = `status ${type}`;
        }
        
        // Make functions global
        window.connectDevice = connectDevice;
        window.runDemo = runDemo;
        
        // Initialize on page load
        init();
    </script>
    
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .controls { margin: 20px 0; }
        .controls button { margin: 5px; padding: 10px; }
        .status { padding: 10px; margin: 10px 0; border-radius: 5px; }
        .status.success { background: #d4edda; color: #155724; }
        .status.error { background: #f8d7da; color: #721c24; }
        .status.warning { background: #fff3cd; color: #856404; }
        .code-area textarea { width: 100%; font-family: monospace; }
        .visualization { margin-top: 20px; padding: 20px; background: #f8f9fa; }
        .pin-state { display: inline-block; margin: 5px; padding: 5px; background: white; border-radius: 3px; }
        .pin-name { font-weight: bold; }
        .pin-type { color: #666; font-size: 0.8em; }
        .pin-value { color: #007bff; }
    </style>
</body>
</html>
```

## Key Benefits Demonstrated

### 1. **Full Debugging Capability**
```javascript
// Set breakpoints in WebAssembly code
debugger; // This works!

// Inspect variables in real-time
console.log('Sensor value:', sensorValue);

// Step through code while hardware responds
```

### 2. **Enhanced Error Messages**
```python
# Instead of cryptic device errors, get detailed context:
try:
    led.value = True
except Exception as e:
    # Bridge provides enhanced error with suggestions:
    # "Pin LED may not be configured as output. Try setting led.direction = digitalio.Direction.OUTPUT first."
```

### 3. **Hybrid Virtual/Physical**
```python
# Read from physical sensor
real_temperature = sensor.value

# Use virtual logic/processing  
if real_temperature > threshold:
    # Control physical LED
    led.value = True
    
    # Virtual calculation
    fan_speed = calculate_fan_speed(real_temperature)
    
    # Control physical fan via PWM
    fan.duty_cycle = int(fan_speed * 65535)
```

### 4. **Real-time State Monitoring**
- See pin states change in browser as hardware responds
- Monitor command queue and execution timing
- Debug synchronization issues visually

## Advanced Features

### Command Queue Optimization
```javascript
// The bridge can optimize commands:
// Multiple rapid LED toggles get batched
// Analog reads are cached and refreshed at optimal intervals
// Command priorities ensure critical operations happen first
```

### Bidirectional Synchronization  
```javascript
// Physical button presses update virtual state
// Virtual code can read real sensor values
// Hybrid scenarios work seamlessly
```

### Error Recovery
```javascript
// If physical device disconnects:
// - Virtual simulation takes over
// - User gets clear notification  
// - Code continues running with simulated hardware
// - Reconnection restores physical control
```

## Comparison: Traditional vs Bridge Approach

| Aspect | Traditional U2IF | WebAssembly Bridge |
|--------|------------------|-------------------|
| Code Location | Physical Device | Browser/WASM |
| Debugging | Limited/Serial | Full Browser DevTools |
| Memory Limits | Device RAM | Browser Memory |
| Error Messages | Basic | Enhanced with Context |
| Visualization | External Tools | Built-in Real-time |
| Hybrid Mode | Not Possible | Virtual + Physical |
| Educational Value | Hardware Focus | Hardware + Software |

## Summary

The WebAssembly-as-U2IF approach revolutionizes hardware learning by:

1. **🧠 Running code in full-featured environment** (WebAssembly)
2. **🔗 Forwarding hardware operations** to physical device  
3. **👁️ Providing real-time visualization** and debugging
4. **🎓 Enabling hybrid learning scenarios** (virtual + physical)
5. **🔄 Supporting seamless mode switching** based on available hardware

This creates the **best of both worlds**: sophisticated software development tools combined with authentic hardware interaction - perfect for hardware-centric Python learning.