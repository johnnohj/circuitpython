/**
 * Default board configuration for CircuitPython WebAssembly port
 * 
 * This demonstrates JavaScript semihosting - JavaScript code providing
 * board definitions and hardware abstractions for CircuitPython.
 * 
 * Includes UART Tx/Rx and Console connections via FFI
 */

// CircuitPython WebAssembly loader will be available as global loadCircuitPython

// JavaScript hardware abstraction layer for UART/Console connections
class WebAssemblyVirtualPin {
    constructor(name, number, capabilities = ['digital_io']) {
        this.name = name;
        this.number = number;
        this.capabilities = capabilities;
        this.mode = null;
        this.value = false;
        this.pullup = false;
        
        // UART/Console connection tracking
        this.uart_connected = false;
        this.console_connected = false;
        
        console.log(`WebAssembly virtual pin ${name} (${number}) created with capabilities:`, capabilities);
    }
    
    // CircuitPython digitalio.DigitalInOut compatibility
    init(mode = 'input') {
        this.mode = mode;
        console.log(`Pin ${this.name} initialized as ${mode}`);
        
        if (mode === 'output') {
            console.log(`🔧 Hardware: Configure pin ${this.number} as output`);
        } else if (mode === 'input') {
            console.log(`🔧 Hardware: Configure pin ${this.number} as input`);
        }
        
        // Special UART/Console connection logic
        if (this.capabilities.includes('uart')) {
            if (this.name.includes('UART_TX') || this.name.includes('CONSOLE_TX')) {
                this.setupUartTx();
            } else if (this.name.includes('UART_RX') || this.name.includes('CONSOLE_RX')) {
                this.setupUartRx();
            }
        }
        
        return this; // For method chaining
    }
    
    // UART Tx setup - connects to JavaScript stdout
    setupUartTx() {
        console.log(`📡 Setting up UART Tx on ${this.name} - connecting to JavaScript stdout`);
        this.uart_connected = true;
        
        // Override write method to connect to stdout
        this.write = (data) => {
            if (typeof data === 'string') {
                process.stdout.write(data);
            } else {
                // Handle bytes/array data
                process.stdout.write(Buffer.from(data));
            }
        };
    }
    
    // UART Rx setup - connects to JavaScript stdin
    setupUartRx() {
        console.log(`📡 Setting up UART Rx on ${this.name} - connecting to JavaScript stdin`);
        this.uart_connected = true;
        
        // Create input buffer for this pin
        this.inputBuffer = [];
        this.inputIndex = 0;
        
        // Connect to stdin (if available)
        if (typeof process !== 'undefined' && process.stdin) {
            process.stdin.setEncoding('utf8');
            process.stdin.on('data', (chunk) => {
                // Add incoming data to this pin's buffer
                for (let i = 0; i < chunk.length; i++) {
                    this.inputBuffer.push(chunk.charCodeAt(i));
                }
            });
        }
        
        // Override read method to get data from stdin
        this.read = () => {
            if (this.inputIndex < this.inputBuffer.length) {
                return this.inputBuffer[this.inputIndex++];
            }
            return -1; // No data available
        };
        
        // Check if data is available
        this.available = () => {
            return this.inputIndex < this.inputBuffer.length;
        };
    }
    
    get_value() {
        // For UART pins, return data availability
        if (this.uart_connected && this.capabilities.includes('uart')) {
            if (this.name.includes('RX') && this.available) {
                return this.available();
            }
        }
        
        console.log(`📖 Reading pin ${this.name}: ${this.value}`);
        return this.value;
    }
    
    set_value(newValue) {
        this.value = !!newValue;
        console.log(`📝 Writing pin ${this.name}: ${this.value}`);
        
        // For UART Tx pins, send data
        if (this.uart_connected && this.name.includes('TX') && this.write) {
            if (this.value) {
                this.write('1');
            } else {
                this.write('0');
            }
        }
        
        if (this.name === 'LED') {
            // Visual feedback for LED
            console.log(this.value ? '💡 LED ON' : '🔲 LED OFF');
        }
    }
    
    // UART specific methods
    uart_write(data) {
        if (!this.uart_connected || !this.name.includes('TX')) {
            throw new Error(`Pin ${this.name} is not configured for UART Tx`);
        }
        
        console.log(`📤 UART Tx ${this.name}: ${data}`);
        if (this.write) {
            this.write(data);
        }
    }
    
    uart_read() {
        if (!this.uart_connected || !this.name.includes('RX')) {
            throw new Error(`Pin ${this.name} is not configured for UART Rx`);
        }
        
        if (this.read) {
            const data = this.read();
            if (data !== -1) {
                console.log(`📥 UART Rx ${this.name}: ${String.fromCharCode(data)}`);
            }
            return data;
        }
        return -1;
    }
    
    // PWM support for capable pins
    set_pwm_duty_cycle(duty) {
        if (!this.capabilities.includes('pwm')) {
            throw new Error(`Pin ${this.name} does not support PWM`);
        }
        console.log(`🔄 PWM pin ${this.name}: ${duty}% duty cycle`);
    }
    
    // Analog read for analog pins
    read_analog() {
        if (!this.capabilities.includes('analog_in')) {
            throw new Error(`Pin ${this.name} does not support analog input`);
        }
        const value = Math.random() * 65535; // 16-bit ADC simulation
        console.log(`📊 Analog read pin ${this.name}: ${Math.round(value)}`);
        return Math.round(value);
    }
}

// Default board configuration object for WebAssembly port
const webAssemblyDefaultBoard = {
    name: "CircuitPython WebAssembly Port Default",
    manufacturer: "JavaScript Semihosting",
    
    // Board initialization
    init() {
        console.log("🚀 Initializing CircuitPython WebAssembly port with default configuration");
        console.log("   JavaScript semihosting: Active");
        console.log("   UART/Console connections: Ready");
        console.log("   Virtual peripherals: Online");
        console.log("   FFI bridges: Connected");
    },
    
    deinit() {
        console.log("🛑 Shutting down CircuitPython WebAssembly port board");
    },
    
    requestsSafeMode() {
        // Could check for error conditions
        return false;
    },
    
    pinFreeRemaining() {
        // All virtual pins are always available
        return true;
    },
    
    // Pin definitions with UART/Console connections
    pins: [
        // UART pins for console communication
        new WebAssemblyVirtualPin('CONSOLE_TX', 0, ['digital_io', 'uart']),
        new WebAssemblyVirtualPin('CONSOLE_RX', 1, ['digital_io', 'uart']),
        new WebAssemblyVirtualPin('UART_TX', 2, ['digital_io', 'uart']),  
        new WebAssemblyVirtualPin('UART_RX', 3, ['digital_io', 'uart']),
        
        // Standard digital pins
        new WebAssemblyVirtualPin('D4', 4, ['digital_io']),
        new WebAssemblyVirtualPin('D5', 5, ['digital_io', 'pwm']),
        new WebAssemblyVirtualPin('D6', 6, ['digital_io', 'pwm']),
        new WebAssemblyVirtualPin('D7', 7, ['digital_io']),
        
        // Analog pins
        new WebAssemblyVirtualPin('A0', 14, ['digital_io', 'analog_in']),
        new WebAssemblyVirtualPin('A1', 15, ['digital_io', 'analog_in']),
        new WebAssemblyVirtualPin('A2', 16, ['digital_io', 'analog_in']),
        new WebAssemblyVirtualPin('A3', 17, ['digital_io', 'analog_in']),
        
        // I2C pins
        new WebAssemblyVirtualPin('SDA', 18, ['digital_io', 'i2c']),
        new WebAssemblyVirtualPin('SCL', 19, ['digital_io', 'i2c']),
        
        // SPI pins
        new WebAssemblyVirtualPin('MOSI', 20, ['digital_io', 'spi']),
        new WebAssemblyVirtualPin('MISO', 21, ['digital_io', 'spi']),
        new WebAssemblyVirtualPin('SCK', 22, ['digital_io', 'spi']),
        
        // Common aliases
        new WebAssemblyVirtualPin('LED', 25, ['digital_io']),      // Built-in LED
        new WebAssemblyVirtualPin('BUTTON', 26, ['digital_io']),   // User button
    ]
};

// Usage example
async function runWebAssemblyExample() {
    console.log("🔧 Loading CircuitPython WebAssembly port with default board configuration...");
    
    const circuitPython = await globalThis.loadCircuitPython({
        heapsize: 2 * 1024 * 1024,
        boardConfiguration: webAssemblyDefaultBoard,
        stdout: (data) => {
            // Color-code Python output differently
            console.log(`🐍 ${data.trimEnd()}`);
        }
    });
    
    console.log("✅ CircuitPython loaded with JavaScript semihosting!");
    console.log("🎯 Running example Python code with UART/Console connections...");
    
    // Example Python code that uses the JavaScript-backed pins including UART
    const pythonCode = `
import board
import time

# Test UART/Console connections
print("Setting up UART connections...")
console_tx = board.CONSOLE_TX
console_rx = board.CONSOLE_RX
uart_tx = board.UART_TX
uart_rx = board.UART_RX

console_tx.init('output')
console_rx.init('input')
uart_tx.init('output')
uart_rx.init('input')

print("UART connections established!")

# Use the LED pin (backed by JavaScript)
print("Setting up LED pin...")
led = board.LED
led.init('output')

# Blink LED and test UART
for i in range(3):
    print(f"Blink {i+1}/3")
    led.set_value(True)
    time.sleep(0.5)
    led.set_value(False) 
    time.sleep(0.5)
    
    # Test UART Tx
    uart_tx.uart_write(f"Hello from UART {i+1}\\n")

print("Demo complete!")

# Test analog pin
print("Testing analog pin A0...")
analog_pin = board.A0
value = analog_pin.read_analog()
print(f"Analog reading: {value}")

print("JavaScript semihosting with UART/Console FFI connections working!")
`;
    
    try {
        circuitPython.runPython(pythonCode);
        console.log("🎉 JavaScript semihosting with UART/Console demo completed successfully!");
    } catch (error) {
        console.error("❌ Error running Python code:", error);
    }
}

// Export for use in other modules
export { webAssemblyDefaultBoard, WebAssemblyVirtualPin, runWebAssemblyExample };

// Run demo if this module is loaded directly
if (typeof window === 'undefined' && import.meta.url === `file://${process.argv[1]}`) {
    runWebAssemblyExample().catch(console.error);
}