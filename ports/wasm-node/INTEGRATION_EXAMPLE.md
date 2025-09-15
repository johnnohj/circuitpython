# CircuitPython Hardware Learning Platform - Complete Integration Example

## Usage Example: LED Blink with Real Hardware Support

This example demonstrates the complete hardware learning architecture, including virtual simulation, real hardware bridging, and educational feedback.

### HTML Integration

```html
<!DOCTYPE html>
<html>
<head>
    <title>CircuitPython Hardware Learning Platform</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .container { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
        .code-panel { background: #f5f5f5; padding: 15px; border-radius: 5px; }
        .hardware-panel { background: #e8f4fd; padding: 15px; border-radius: 5px; }
        .virtual-board { border: 2px solid #333; padding: 20px; margin: 10px 0; }
        .pin { display: inline-block; width: 20px; height: 20px; border-radius: 50%; margin: 2px; }
        .pin.high { background-color: #00ff00; }
        .pin.low { background-color: #666666; }
        .status { background: #fff3cd; padding: 10px; margin: 10px 0; border-radius: 5px; }
        .error { background: #f8d7da; color: #721c24; }
        .success { background: #d4edda; color: #155724; }
    </style>
</head>
<body>
    <h1>🐍 CircuitPython Hardware Learning Platform</h1>
    
    <div class="container">
        <!-- Code Panel -->
        <div class="code-panel">
            <h3>Python Code Editor</h3>
            <textarea id="code-editor" rows="15" cols="50">
import board
import digitalio
import time

# Set up LED pin
led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

# Blink LED
for i in range(10):
    led.value = True
    time.sleep(0.5)
    led.value = False 
    time.sleep(0.5)
    
print(f"Blinked LED {i+1} times!")
            </textarea>
            <br><br>
            <button onclick="runCode()">▶️ Run Code</button>
            <button onclick="connectHardware()">🔌 Connect Hardware</button>
            <button onclick="resetBoard()">🔄 Reset</button>
            
            <div id="output-area">
                <h4>Output:</h4>
                <pre id="output"></pre>
            </div>
        </div>
        
        <!-- Hardware Panel -->
        <div class="hardware-panel">
            <h3>Hardware Visualization</h3>
            
            <div class="status" id="connection-status">
                📱 Status: Virtual Mode (No hardware connected)
            </div>
            
            <div class="virtual-board" id="virtual-board">
                <h4>Virtual CircuitPython Board</h4>
                <!-- Pins will be dynamically generated -->
            </div>
            
            <div id="hardware-status">
                <h4>Pin States:</h4>
                <div id="pin-status"></div>
            </div>
            
            <div id="debug-info">
                <h4>Debug Information:</h4>
                <pre id="debug-output"></pre>
            </div>
        </div>
    </div>

    <!-- Load the platform -->
    <script type="module">
        import { createCircuitPython } from './circuitpython-bridge.js';
        import { BoardShadowRuntime } from './board-shadow-runtime.js';
        
        // Global state
        let circuitPython = null;
        let boardShadow = null;
        let isRunning = false;
        
        // Initialize the platform
        async function initializePlatform() {
            try {
                updateStatus('🚀 Initializing CircuitPython...', 'info');
                
                // Initialize CircuitPython WASM
                circuitPython = await createCircuitPython({
                    onOutput: (text) => {
                        appendOutput(text);
                    },
                    onError: (text) => {
                        appendOutput(`Error: ${text}`, 'error');
                    }
                });
                
                // Initialize Board Shadow Runtime
                boardShadow = new BoardShadowRuntime({
                    boardType: 'pico',
                    enableLogging: true
                });
                
                // Set up board visualization
                initializeVirtualBoard();
                
                // Set up pin change monitoring
                setupPinMonitoring();
                
                updateStatus('✅ CircuitPython Ready! Virtual board active.', 'success');
                
            } catch (error) {
                updateStatus(`❌ Initialization failed: ${error.message}`, 'error');
                console.error('Platform initialization error:', error);
            }
        }
        
        // Initialize virtual board visualization
        function initializeVirtualBoard() {
            const boardElement = document.getElementById('virtual-board');
            const pins = ['GP0', 'GP1', 'GP2', 'GP3', 'GP4', 'GP5', 'LED'];
            
            pins.forEach(pinId => {
                const pinElement = document.createElement('div');
                pinElement.className = 'pin low';
                pinElement.id = `pin-${pinId}`;
                pinElement.title = `Pin ${pinId}`;
                
                const label = document.createElement('span');
                label.textContent = pinId;
                label.style.marginLeft = '5px';
                
                const container = document.createElement('div');
                container.style.display = 'inline-block';
                container.style.margin = '5px';
                container.appendChild(pinElement);
                container.appendChild(label);
                
                boardElement.appendChild(container);
            });
        }
        
        // Set up pin monitoring
        function setupPinMonitoring() {
            const pins = ['GP0', 'GP1', 'GP2', 'GP3', 'GP4', 'GP5', 'LED'];
            
            pins.forEach(pinId => {
                boardShadow.addPinListener(pinId, (value, source) => {
                    updatePinVisualization(pinId, value, source);
                    updateDebugInfo();
                });
                
                boardShadow.watchPin(pinId);
            });
        }
        
        // Update pin visualization
        function updatePinVisualization(pinId, value, source) {
            const pinElement = document.getElementById(`pin-${pinId}`);
            if (pinElement) {
                pinElement.className = `pin ${value ? 'high' : 'low'}`;
            }
            
            // Update pin status display
            updatePinStatusDisplay();
        }
        
        // Update pin status display
        function updatePinStatusDisplay() {
            const statusElement = document.getElementById('pin-status');
            const hardware = boardShadow.getHardwareStatus();
            
            let statusHTML = '';
            for (const [pinId, state] of boardShadow.shadowState.entries()) {
                const stateText = state.value ? 'HIGH' : 'LOW';
                const sourceText = state.source.toUpperCase();
                statusHTML += `<div>${pinId}: ${stateText} (${sourceText})</div>`;
            }
            
            statusElement.innerHTML = statusHTML || 'No pin activity yet';
        }
        
        // Update debug information
        function updateDebugInfo() {
            const debugElement = document.getElementById('debug-output');
            const status = boardShadow.getHardwareStatus();
            
            const debugInfo = {
                'Sync Mode': status.syncMode,
                'Physical Board': status.physicalBoard || 'None',
                'Active Pins': status.totalPins,
                'Watched Pins': status.watchedPins.length,
                'Last Sync': new Date(status.lastSync).toLocaleTimeString()
            };
            
            debugElement.textContent = JSON.stringify(debugInfo, null, 2);
        }
        
        // Run CircuitPython code
        async function runCode() {
            if (isRunning) {
                updateStatus('⏳ Code already running...', 'info');
                return;
            }
            
            const code = document.getElementById('code-editor').value;
            if (!code.trim()) {
                updateStatus('❌ No code to run', 'error');
                return;
            }
            
            isRunning = true;
            clearOutput();
            updateStatus('🏃 Running code...', 'info');
            
            try {
                // Enhance the code with board shadow integration
                const enhancedCode = injectBoardShadowIntegration(code);
                
                // Execute the code
                const result = await circuitPython.execute(enhancedCode);
                
                if (result.success) {
                    appendOutput('✅ Code executed successfully');
                    updateStatus('✅ Code completed', 'success');
                } else {
                    appendOutput(`❌ Execution failed: ${result.error}`, 'error');
                    updateStatus('❌ Code failed', 'error');
                }
                
            } catch (error) {
                appendOutput(`💥 Error: ${error.message}`, 'error');
                updateStatus('❌ Execution error', 'error');
            } finally {
                isRunning = false;
            }
        }
        
        // Inject board shadow integration into user code
        function injectBoardShadowIntegration(userCode) {
            const integration = `
# Board Shadow Integration
import js
_board_shadow = js.boardShadow

class ShadowDigitalInOut:
    def __init__(self, pin):
        self.pin = pin
        self._direction = None
        self._value = False
        
    @property 
    def direction(self):
        return self._direction
        
    @direction.setter
    def direction(self, dir):
        self._direction = dir
        
    @property
    def value(self):
        return self._value
        
    @value.setter 
    def value(self, val):
        self._value = bool(val)
        # Update board shadow
        _board_shadow.setPin(str(self.pin), val)

# Monkey patch digitalio
import digitalio
digitalio.DigitalInOut = ShadowDigitalInOut

# Add sleep integration
import time
_original_sleep = time.sleep
def shadow_sleep(seconds):
    _original_sleep(seconds)
    # Allow UI updates during sleep
    
time.sleep = shadow_sleep

# User code starts here:
${userCode}
`;
            return integration;
        }
        
        // Connect to physical hardware
        async function connectHardware() {
            try {
                updateStatus('🔌 Attempting hardware connection...', 'info');
                
                const connectionType = await boardShadow.connectPhysicalBoard();
                
                if (connectionType === 'virtual') {
                    updateStatus('📱 No physical hardware found. Using virtual mode.', 'info');
                } else {
                    updateStatus(`✅ Connected via ${connectionType.toUpperCase()}!`, 'success');
                }
                
                updateDebugInfo();
                
            } catch (error) {
                updateStatus(`❌ Connection failed: ${error.message}`, 'error');
            }
        }
        
        // Reset board state
        function resetBoard() {
            clearOutput();
            
            // Reset all pins to low
            const pins = ['GP0', 'GP1', 'GP2', 'GP3', 'GP4', 'GP5', 'LED'];
            pins.forEach(pinId => {
                boardShadow.setPin(pinId, 0);
            });
            
            updateStatus('🔄 Board reset', 'info');
            updateDebugInfo();
        }
        
        // Utility functions
        function updateStatus(message, type = 'info') {
            const statusElement = document.getElementById('connection-status');
            statusElement.textContent = message;
            statusElement.className = `status ${type}`;
        }
        
        function appendOutput(text, type = 'normal') {
            const outputElement = document.getElementById('output');
            const line = document.createElement('div');
            line.textContent = text;
            line.className = type;
            outputElement.appendChild(line);
            outputElement.scrollTop = outputElement.scrollHeight;
        }
        
        function clearOutput() {
            document.getElementById('output').innerHTML = '';
        }
        
        // Make functions globally available
        window.runCode = runCode;
        window.connectHardware = connectHardware;  
        window.resetBoard = resetBoard;
        window.boardShadow = boardShadow;
        
        // Initialize when page loads
        initializePlatform();
    </script>
</body>
</html>
```

## Learning Features Demonstrated

### 1. **Hardware-Centric Learning**
- Visual pin state representation
- Real-time hardware state updates
- Connection to actual CircuitPython devices

### 2. **Progressive Learning Path**
- Start with virtual simulation
- Connect real hardware when available
- Identical code works in both modes

### 3. **Educational Debugging**
- Pin state history tracking
- Hardware connection status
- Detailed error messages with context

### 4. **Real Hardware Integration**

#### WebSerial Connection Example:
```javascript
// Automatic hardware detection and connection
const connectionType = await boardShadow.connectPhysicalBoard();

if (connectionType === 'webserial') {
    console.log('Connected to real CircuitPython device!');
    // Code now runs on actual hardware
}
```

#### U2IF Connection Example:
```javascript
// Direct hardware control via USB
if (connectionType === 'u2if') {
    // Ultra-low latency hardware access
    await boardShadow.setPin('LED', 1);
    const sensorValue = await boardShadow.readAnalog('A0');
}
```

### 5. **Seamless Mode Switching**
- Virtual → Physical: Code continues working
- Physical → Virtual: Graceful fallback
- Hybrid mode: Some pins virtual, some physical

## Advanced Usage: Classroom Integration

### Teacher Dashboard
```html
<!-- Monitor multiple student boards -->
<div id="student-boards">
    <!-- Real-time view of all student hardware states -->
</div>

<script type="module">
    // Connect to multiple student boards
    const studentBoards = await connectToClassroom();
    
    studentBoards.forEach(board => {
        board.onStateChange = (studentId, pinId, value) => {
            updateStudentDisplay(studentId, pinId, value);
        };
    });
</script>
```

### Guided Learning Mode
```javascript
class LearningAssistant {
    constructor(boardShadow) {
        this.boardShadow = boardShadow;
        this.currentLesson = null;
    }
    
    async startLesson(lessonType) {
        switch (lessonType) {
            case 'blink':
                return this.guideBlink();
            case 'button':
                return this.guideButtonInput();
            case 'sensor':
                return this.guideSensorReading();
        }
    }
    
    async guideBlink() {
        return {
            instructions: "Let's make an LED blink!",
            template: `import board
import digitalio
import time

# Your code here`,
            expectedPins: ['LED'],
            validation: (pinStates) => {
                // Check if LED pin is being toggled
                const ledHistory = this.boardShadow.getPinHistory('LED', 5000);
                return ledHistory.length >= 2; // At least two state changes
            }
        };
    }
}
```

## Summary: Complete Learning Ecosystem

This integration provides:

1. **✅ Genuine CircuitPython Interpreter** - Real code validation
2. **✅ Virtual Hardware Simulation** - Learn without devices  
3. **✅ Real Hardware Bridging** - WebUSB/WebSerial connectivity
4. **✅ Board Shadow Runtime** - Unified virtual/physical state
5. **✅ Educational Debugging** - Learning-focused error handling
6. **✅ Progressive Learning** - Virtual → Physical pathway
7. **✅ Classroom Integration** - Multi-student monitoring

The architecture successfully bridges browser-based development with real microcontroller programming, enabling hardware-centric Python learning that scales from individual exploration to classroom deployment.