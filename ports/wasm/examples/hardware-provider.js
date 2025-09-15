// Hardware Provider for CircuitPython WASM
// This implements the Module.hardwareProvider interface that the js_provider expects

class HardwareProvider {
    constructor() {
        // Virtual pin states for the Generic Metro board
        this.pinStates = {};
        this.pinDirections = {};
        this.analogValues = {};
        this.pullModes = {};
        
        // Initialize default states
        this.initializeDefaults();
        
        // Callbacks for UI updates
        this.onPinChange = null;
        this.onDirectionChange = null;
        this.onAnalogChange = null;
    }
    
    initializeDefaults() {
        // Set default states for special pins
        this.pinStates['BUTTON'] = 1;  // Not pressed (pull-up)
        this.pinStates['LED'] = 0;     // Off
        this.pinStates['D13'] = 0;     // LED alias
        
        // Initialize analog pins with mid-range values
        for (let i = 0; i < 6; i++) {
            this.analogValues[`A${i}`] = 512;  // 10-bit ADC middle value
        }
    }
    
    // Digital I/O operations
    digitalSetDirection(pinName, isOutput) {
        this.pinDirections[pinName] = isOutput;
        console.log(`Pin ${pinName} direction: ${isOutput ? 'OUTPUT' : 'INPUT'}`);
        
        if (this.onDirectionChange) {
            this.onDirectionChange(pinName, isOutput);
        }
    }
    
    digitalSetDirectionByNumber(pinNumber, isOutput) {
        // Map number to name if needed
        const pinName = `D${pinNumber}`;
        this.digitalSetDirection(pinName, isOutput);
    }
    
    digitalSetValue(pinName, value) {
        this.pinStates[pinName] = value ? 1 : 0;
        
        // Handle LED alias
        if (pinName === 'LED' || pinName === 'D13') {
            this.pinStates['LED'] = value ? 1 : 0;
            this.pinStates['D13'] = value ? 1 : 0;
        }
        
        console.log(`Pin ${pinName} = ${value ? 'HIGH' : 'LOW'}`);
        
        if (this.onPinChange) {
            this.onPinChange(pinName, value);
        }
    }
    
    digitalSetValueByNumber(pinNumber, value) {
        const pinName = `D${pinNumber}`;
        this.digitalSetValue(pinName, value);
    }
    
    digitalGetValue(pinName) {
        // Special handling for button
        if (pinName === 'BUTTON') {
            // Check if there's a global button state override
            if (typeof window !== 'undefined' && window.buttonPressed !== undefined) {
                return window.buttonPressed ? 0 : 1;  // Inverted for pull-up
            }
        }
        
        return this.pinStates[pinName] || 0;
    }
    
    digitalGetValueByNumber(pinNumber) {
        const pinName = `D${pinNumber}`;
        return this.digitalGetValue(pinName);
    }
    
    // Analog operations
    analogRead(pinName) {
        // Return stored analog value or generate one
        if (this.analogValues[pinName] !== undefined) {
            return this.analogValues[pinName];
        }
        
        // For pins that can be analog, return a simulated value
        if (pinName.startsWith('A')) {
            const value = Math.floor(Math.random() * 1024);  // 10-bit ADC
            this.analogValues[pinName] = value;
            return value;
        }
        
        return 0;
    }
    
    analogReadByNumber(pinNumber) {
        // Assume analog pins start at A0
        if (pinNumber >= 14 && pinNumber <= 19) {
            const pinName = `A${pinNumber - 14}`;
            return this.analogRead(pinName);
        }
        return 0;
    }
    
    analogWrite(pinName, value) {
        // PWM output simulation
        this.analogValues[pinName] = value;
        console.log(`PWM on ${pinName} = ${value}`);
        
        if (this.onAnalogChange) {
            this.onAnalogChange(pinName, value);
        }
    }
    
    analogWriteByNumber(pinNumber, value) {
        const pinName = `D${pinNumber}`;
        this.analogWrite(pinName, value);
    }
    
    // Utility methods for testing
    setAnalogValue(pinName, value) {
        this.analogValues[pinName] = value;
    }
    
    setButtonState(pressed) {
        this.pinStates['BUTTON'] = pressed ? 0 : 1;  // Inverted for pull-up
    }
    
    getLEDState() {
        return this.pinStates['LED'] || 0;
    }
    
    // Get current board state as JSON
    getBoardState() {
        return {
            pins: this.pinStates,
            directions: this.pinDirections,
            analog: this.analogValues,
            pulls: this.pullModes
        };
    }
}

// Export for use in modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = GenericMetroHardwareProvider;
}

// Also make available globally for browser use
if (typeof window !== 'undefined') {
    window.GenericMetroHardwareProvider = GenericMetroHardwareProvider;
}