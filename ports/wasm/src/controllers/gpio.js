/**
 * GPIOController - Manages all GPIO pins
 *
 * This bridges Python code (digitalio, analogio) with JS virtual hardware.
 * It provides pin management and state synchronization between C and JS.
 *
 * Handles:
 * - Digital input/output (digitalio module)
 * - Analog input/output (analogio module)
 * - Pin state synchronization
 * - Event callbacks for UI visualization
 */

export class GPIOController {
  /**
   * Create a new GPIO controller
   * @param {Object} wasmModule - The loaded WASM module
   * @param {CircuitPythonBoard} board - The parent board instance
   */
  constructor(wasmModule, board) {
    this.module = wasmModule
    this.board = board

    // Pin management
    this.pins = new Map()  // pin number → Pin object
    this.virtualState = new Map()  // pin number → {value, direction, pull, etc}

    // Update tracking
    this.lastUpdateTime = 0
    this.updateInterval = 16  // ~60fps for smooth visualization
  }

  /**
   * Get a pin object (creates if doesn't exist)
   * @param {number} number - Pin number (GPIO0, GPIO1, etc.)
   * @returns {Pin} Pin object
   */
  getPin(number) {
    if (!this.pins.has(number)) {
      const pin = new Pin(number, this)
      this.pins.set(number, pin)
      this.virtualState.set(number, {
        value: false,
        direction: 'input',
        pull: null,
        analogValue: 0
      })
    }
    return this.pins.get(number)
  }

  /**
   * Get all active pins
   * @returns {Map<number, Pin>} Map of pin number to Pin object
   */
  getAllPins() {
    return new Map(this.pins)
  }

  /**
   * Update pin state from C side
   * Called by background loop to sync state between Python and JS
   *
   * This allows Python code to update pins and JS to react (e.g., for visualization)
   */
  updateState() {
    // Throttle updates to avoid excessive calls
    const now = Date.now()
    if (now - this.lastUpdateTime < this.updateInterval) {
      return
    }
    this.lastUpdateTime = now

    // Read pin states from WASM memory
    // For each active pin, check if C-side value has changed
    for (const [pinNum, pin] of this.pins) {
      try {
        // Try to read digital value from C side
        if (this.module.ccall) {
          const value = this.module.ccall(
            'common_hal_digitalio_digitalinout_get_value',
            'number',
            ['number'],
            [pinNum]
          )

          // Update pin if value changed
          if (value !== pin._lastCValue) {
            pin._updateFromC(value !== 0)
            pin._lastCValue = value
          }
        }
      } catch (e) {
        // Pin may not have digital IO initialized, which is fine
      }
    }
  }

  /**
   * Reset all pins (called during soft reset)
   * This returns all pins to their default input state
   */
  resetAll() {
    console.log('[GPIO] Resetting all pins...')

    // Call C-side reset if available
    try {
      if (this.module.ccall) {
        // This resets the C-side GPIO state
        this.module.ccall('reset_port', 'null', [], [])
      }
    } catch (e) {
      console.warn('[GPIO] reset_port not available:', e.message)
    }

    // Reset JS-side pin state
    for (const pin of this.pins.values()) {
      pin._reset()
    }

    // Reset virtual state
    for (const [pinNum, state] of this.virtualState) {
      state.value = false
      state.direction = 'input'
      state.pull = null
      state.analogValue = 0
    }

    console.log('[GPIO] Reset complete')
  }

  /**
   * Set a pin's value (for output pins)
   * This can be called from JS to simulate input to CircuitPython
   *
   * @param {number} pinNum - Pin number
   * @param {boolean} value - Pin value (true = high, false = low)
   */
  setPinValue(pinNum, value) {
    const pin = this.getPin(pinNum)
    pin.setValue(value)
  }

  /**
   * Get a pin's value (for input pins)
   *
   * @param {number} pinNum - Pin number
   * @returns {boolean} Pin value
   */
  getPinValue(pinNum) {
    const pin = this.getPin(pinNum)
    return pin.getValue()
  }

  /**
   * Set pin direction
   *
   * @param {number} pinNum - Pin number
   * @param {string} direction - 'input' or 'output'
   */
  setPinDirection(pinNum, direction) {
    const state = this.virtualState.get(pinNum)
    if (state) {
      state.direction = direction
    }

    const pin = this.getPin(pinNum)
    pin.setDirection(direction)
  }

  /**
   * Set pin pull resistor
   *
   * @param {number} pinNum - Pin number
   * @param {string|null} pull - 'up', 'down', or null
   */
  setPinPull(pinNum, pull) {
    const state = this.virtualState.get(pinNum)
    if (state) {
      state.pull = pull
    }
  }

  /**
   * Get virtual pin state (for UI/visualization)
   *
   * @param {number} pinNum - Pin number
   * @returns {Object} Pin state object
   */
  getVirtualState(pinNum) {
    return this.virtualState.get(pinNum) || {
      value: false,
      direction: 'input',
      pull: null,
      analogValue: 0
    }
  }
}

/**
 * Pin - Represents a single GPIO pin
 *
 * This provides a high-level interface to a pin that matches
 * the Python digitalio.DigitalInOut API
 */
export class Pin {
  /**
   * Create a new pin
   * @param {number} number - Pin number
   * @param {GPIOController} controller - Parent controller
   */
  constructor(number, controller) {
    this.number = number
    this.controller = controller

    // Private state (use _ prefix to distinguish from proxy-accessed properties)
    this._value = false
    this._direction = 'input'
    this._pull = null
    this._driveMode = 'push-pull'

    // Event callbacks
    this.callbacks = []

    // C-side tracking
    this._lastCValue = null
    this._initialized = false
  }

  // =========================================================================
  // Property Getters/Setters (used by JsProxy system)
  // =========================================================================
  // When C code calls store_attr(js_pin->ref, "value", value_out),
  // the proxy system calls these setters, which fire onChange callbacks

  /**
   * Get pin value
   * @returns {boolean} Pin value
   */
  get value() {
    return this._value
  }

  /**
   * Set pin value - triggers onChange callbacks
   * @param {boolean} val - Pin value
   */
  set value(val) {
    const boolValue = Boolean(val)
    if (this._value !== boolValue) {
      this._value = boolValue

      // Fire onChange callbacks (automatic event notification!)
      this._notifyChange()
    }
  }

  /**
   * Get pin direction
   * @returns {string} 'input' or 'output'
   */
  get direction() {
    return this._direction
  }

  /**
   * Set pin direction
   * @param {string} dir - 'input' or 'output'
   */
  set direction(dir) {
    if (dir !== 'input' && dir !== 'output') {
      throw new Error(`Invalid direction: ${dir}`)
    }
    this._direction = dir
  }

  /**
   * Get pull resistor configuration
   * @returns {string|null} Pull configuration
   */
  get pull() {
    return this._pull
  }

  /**
   * Set pull resistor
   * @param {string|null} p - 'up', 'down', 'none', or null
   */
  set pull(p) {
    // Normalize 'none' to null
    if (p === 'none') {
      p = null
    }

    if (p !== 'up' && p !== 'down' && p !== null) {
      throw new Error(`Invalid pull mode: ${p}`)
    }

    this._pull = p

    // Apply pull to value if input
    if (this._direction === 'input' && p !== null) {
      this.value = p === 'up'  // Uses setter, fires callbacks
    }
  }

  /**
   * Get drive mode
   * @returns {string} 'push-pull' or 'open-drain'
   */
  get driveMode() {
    return this._driveMode
  }

  /**
   * Set drive mode
   * @param {string} mode - 'push-pull' or 'open-drain'
   */
  set driveMode(mode) {
    if (mode !== 'push-pull' && mode !== 'open-drain') {
      throw new Error(`Invalid drive mode: ${mode}`)
    }
    this._driveMode = mode
  }

  /**
   * Set pin direction
   * @param {string} direction - 'input' or 'output'
   */
  setDirection(direction) {
    if (direction !== 'input' && direction !== 'output') {
      throw new Error(`Invalid direction: ${direction}`)
    }

    this.direction = direction

    // Notify change
    this._notifyChange()
  }

  /**
   * Get pin direction
   * @returns {string} 'input' or 'output'
   */
  getDirection() {
    return this.direction
  }

  /**
   * Set pin value (for output pins)
   * @param {boolean} value - Pin value (true = high, false = low)
   */
  setValue(value) {
    const boolValue = Boolean(value)

    if (this.value !== boolValue) {
      this.value = boolValue

      // Sync to C-side memory structure (same approach as library_digitalio.js)
      try {
        // Get GPIO state pointer and write directly to memory
        if (this.controller.module?.ccall) {
          const gpioStatePtr = this.controller.module.ccall('get_gpio_state_ptr', 'number', [], [])
          if (gpioStatePtr && this.number < 64) {
            // Write to 'value' field (first byte of 8-byte pin structure)
            const offset = gpioStatePtr + this.number * 8
            this.controller.module.HEAPU8[offset] = boolValue ? 1 : 0
          }
        }
      } catch (e) {
        // C memory access not available, which is fine
      }

      // Update virtual state
      const state = this.controller.virtualState.get(this.number)
      if (state) {
        state.value = boolValue
      }

      // Trigger callbacks for visualization
      this._notifyChange()
    }
  }

  /**
   * Get pin value (for input pins)
   * @returns {boolean} Pin value
   */
  getValue() {
    // Try to read from C-side memory structure
    try {
      if (this.controller.module?.ccall) {
        const gpioStatePtr = this.controller.module.ccall('get_gpio_state_ptr', 'number', [], [])
        if (gpioStatePtr && this.number < 64) {
          // Read 'value' field (first byte of 8-byte pin structure)
          const offset = gpioStatePtr + this.number * 8
          const cValue = this.controller.module.HEAPU8[offset]
          this.value = cValue !== 0
        }
      }
    } catch (e) {
      // Use cached value if C memory not available
    }

    return this.value
  }

  /**
   * Set pin pull resistor
   * @param {string|null} pull - 'up', 'down', or null (no pull)
   */
  setPull(pull) {
    if (pull !== 'up' && pull !== 'down' && pull !== null) {
      throw new Error(`Invalid pull mode: ${pull}`)
    }

    this.pull = pull

    // Update virtual state
    const state = this.controller.virtualState.get(this.number)
    if (state) {
      state.pull = pull
    }

    // Apply pull to value if input
    if (this.direction === 'input' && pull !== null) {
      this.value = pull === 'up'
      this._notifyChange()
    }
  }

  /**
   * Get pin pull resistor configuration
   * @returns {string|null} Pull configuration
   */
  getPull() {
    return this.pull
  }

  /**
   * Toggle pin value (convenience method)
   */
  toggle() {
    this.setValue(!this.value)
  }

  /**
   * Listen for pin changes (for UI visualization)
   * @param {Function} callback - Callback function(value: boolean)
   * @returns {Function} Unregister function
   */
  onChange(callback) {
    if (typeof callback !== 'function') {
      throw new Error('Callback must be a function')
    }

    this.callbacks.push(callback)

    // Return unregister function
    return () => {
      const idx = this.callbacks.indexOf(callback)
      if (idx >= 0) {
        this.callbacks.splice(idx, 1)
      }
    }
  }

  /**
   * Update pin value from C side (called by controller)
   * @param {boolean} value - New value from C
   * @private
   */
  _updateFromC(value) {
    if (this.value !== value) {
      this.value = value

      // Update virtual state
      const state = this.controller.virtualState.get(this.number)
      if (state) {
        state.value = value
      }

      this._notifyChange()
    }
  }

  /**
   * Notify all callbacks of pin change
   * @private
   */
  _notifyChange() {
    for (const cb of this.callbacks) {
      try {
        cb(this.value)
      } catch (e) {
        console.error(`[GPIO] Error in pin ${this.number} callback:`, e)
      }
    }
  }

  /**
   * Reset pin to default state (called during soft reset)
   * @private
   */
  _reset() {
    this.direction = 'input'
    this.value = false
    this.pull = null
    this._lastCValue = null
    this._notifyChange()
  }

  /**
   * Get pin info for debugging
   * @returns {Object} Pin information
   */
  getInfo() {
    return {
      number: this.number,
      value: this.value,
      direction: this.direction,
      pull: this.pull,
      hasCallbacks: this.callbacks.length > 0
    }
  }
}
