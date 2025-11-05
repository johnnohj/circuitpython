/**
 * SupervisorController - Manages CircuitPython runtime lifecycle
 *
 * This is the JS-side counterpart to supervisor/port.c
 * It coordinates between the C supervisor and JS virtual hardware
 *
 * The supervisor is responsible for:
 * - Runtime initialization and lifecycle management
 * - Running user code (code.py, REPL)
 * - Background tasks coordination
 * - Soft reset and safe mode handling
 * - Autoreload functionality
 */

export class SupervisorController {
  /**
   * Create a new supervisor controller
   * @param {Object} wasmModule - The loaded WASM module
   * @param {CircuitPythonBoard} board - The parent board instance
   */
  constructor(wasmModule, board) {
    this.module = wasmModule
    this.board = board

    // Runtime state
    this.state = {
      running: false,
      inREPL: false,
      safeMode: false,
      autoreload: true,
      nextCodeRun: null
    }

    // Background task management
    this._backgroundTaskHandle = null
    this._shutdownRequested = false
  }

  /**
   * Initialize supervisor (matches port_init() in C)
   * This sets up the CircuitPython runtime and prepares for code execution
   */
  async initialize() {
    console.log('[Supervisor] Initializing...')

    // Call C-side port_init
    // Returns non-zero if safe mode should be entered
    let safeMode = 0
    try {
      if (this.module.ccall) {
        safeMode = this.module.ccall('port_init', 'number', [], [])
      }
    } catch (e) {
      console.warn('[Supervisor] port_init not available yet:', e.message)
    }

    this.state.safeMode = safeMode !== 0

    if (this.state.safeMode) {
      console.log('[Supervisor] Entered safe mode')
    }

    // Set up background tasks coordination
    this._startBackgroundLoop()

    this.state.running = true
    console.log('[Supervisor] Initialization complete')
  }

  /**
   * Enter REPL mode (matches CircuitPython REPL behavior)
   * This initializes the interactive Python prompt
   */
  enterREPL() {
    if (this.state.inREPL) {
      console.log('[Supervisor] Already in REPL mode')
      return
    }

    console.log('[Supervisor] Entering REPL mode')

    // Initialize REPL (calls mp_js_repl_init from library_supervisor.js)
    try {
      if (this.module.ccall) {
        this.module.ccall('mp_js_repl_init', 'null', [], [])
      }
    } catch (e) {
      console.warn('[Supervisor] mp_js_repl_init not available:', e.message)
    }

    this.state.inREPL = true

    // Connect REPL to serial interface
    if (this.board.serial) {
      this.board.serial.enableREPL()
    }

    console.log('[Supervisor] REPL ready')
  }

  /**
   * Run Python code (like typing in REPL or running code.py)
   *
   * @param {string} code - Python code to execute
   * @param {Object} options - Execution options
   * @param {boolean} options.background - Run in background (async)
   * @returns {Promise<any>} Execution result
   */
  async runCode(code, options = {}) {
    if (!this.state.running) {
      throw new Error('Supervisor not running')
    }

    console.log('[Supervisor] Running Python code...')

    try {
      // Use the module's runPythonAsync if available
      if (this.module.runPythonAsync) {
        const result = await this.module.runPythonAsync(code)
        return result
      } else {
        console.warn('[Supervisor] runPythonAsync not available')
        return null
      }
    } catch (error) {
      console.error('[Supervisor] Code execution error:', error)
      throw error
    }
  }

  /**
   * Soft reset (like Ctrl+D or supervisor.reload())
   * This resets the Python VM and all hardware to initial state
   */
  async reset() {
    console.log('[Supervisor] Performing soft reset...')

    // 1. Reset all hardware state
    if (this.board.gpio) {
      this.board.gpio.resetAll()
    }

    if (this.board.i2c) {
      this.board.i2c.resetAll()
    }

    if (this.board.spi) {
      this.board.spi.resetAll()
    }

    if (this.board.display) {
      this.board.display.reset()
    }

    // 2. Call C-side reset_port()
    try {
      if (this.module.ccall) {
        this.module.ccall('reset_port', 'null', [], [])
      }
    } catch (e) {
      console.warn('[Supervisor] reset_port not available:', e.message)
    }

    // 3. Restart REPL if it was active
    if (this.state.inREPL) {
      this.state.inREPL = false
      // Small delay to let reset complete
      await new Promise(resolve => setTimeout(resolve, 100))
      this.enterREPL()
    }

    console.log('[Supervisor] Soft reset complete')
  }

  /**
   * Enter safe mode
   * In safe mode, user code doesn't run and only basic functionality is available
   */
  async enterSafeMode() {
    console.log('[Supervisor] Entering safe mode...')

    this.state.safeMode = true
    this.state.autoreload = false

    // Call C-side reset_into_safe_mode() if available
    try {
      if (this.module.ccall) {
        this.module.ccall('reset_into_safe_mode', 'null', [], [])
      }
    } catch (e) {
      console.warn('[Supervisor] reset_into_safe_mode not available:', e.message)
    }

    // Reset hardware to safe defaults
    await this.reset()

    console.log('[Supervisor] Safe mode active')
  }

  /**
   * Exit safe mode
   */
  exitSafeMode() {
    if (!this.state.safeMode) {
      return
    }

    console.log('[Supervisor] Exiting safe mode...')
    this.state.safeMode = false
    this.state.autoreload = true
  }

  /**
   * Enable/disable autoreload
   * When enabled, code.py automatically reruns when modified
   *
   * @param {boolean} enabled - Whether autoreload should be enabled
   */
  setAutoreload(enabled) {
    this.state.autoreload = enabled
    console.log(`[Supervisor] Autoreload ${enabled ? 'enabled' : 'disabled'}`)
  }

  /**
   * Check if code should be run (for autoreload)
   * @private
   */
  _checkCodeRun() {
    // This would check if code.py has been modified
    // For now, this is a placeholder for future autoreload implementation
    return false
  }

  /**
   * Background task loop (matches RUN_BACKGROUND_TASKS in CircuitPython)
   * This runs continuously while the board is active and handles:
   * - Background callbacks from Python
   * - Hardware state updates
   * - Autoreload checks
   * @private
   */
  _startBackgroundLoop() {
    const runBackgroundTasks = () => {
      // Stop if shutdown requested
      if (this._shutdownRequested || !this.state.running) {
        return
      }

      try {
        // 1. Call C-side background callback processing
        if (this.module._background_callback_pending) {
          const pending = this.module._background_callback_pending()
          if (pending) {
            this.module._background_callback_run_all?.()
          }
        }

        // 2. Update hardware state (sync between C and JS)
        if (this.board.gpio) {
          this.board.gpio.updateState()
        }

        // 3. Check for autoreload
        if (this.state.autoreload && !this.state.inREPL) {
          if (this._checkCodeRun()) {
            this.state.nextCodeRun = Date.now()
          }
        }

        // 4. Update display if present
        if (this.board.display) {
          this.board.display.updateFrame()
        }
      } catch (e) {
        console.error('[Supervisor] Background task error:', e)
      }

      // Schedule next iteration
      this._backgroundTaskHandle = requestAnimationFrame(runBackgroundTasks)
    }

    // Start the loop
    this._backgroundTaskHandle = requestAnimationFrame(runBackgroundTasks)
    console.log('[Supervisor] Background tasks started')
  }

  /**
   * Stop background tasks
   * @private
   */
  _stopBackgroundLoop() {
    if (this._backgroundTaskHandle !== null) {
      cancelAnimationFrame(this._backgroundTaskHandle)
      this._backgroundTaskHandle = null
      console.log('[Supervisor] Background tasks stopped')
    }
  }

  /**
   * Shutdown the supervisor
   * This stops all background tasks and cleans up resources
   */
  async shutdown() {
    console.log('[Supervisor] Shutting down...')

    this._shutdownRequested = true
    this.state.running = false

    // Stop background tasks
    this._stopBackgroundLoop()

    // Give any in-flight operations time to complete
    await new Promise(resolve => setTimeout(resolve, 100))

    console.log('[Supervisor] Shutdown complete')
  }

  /**
   * Get supervisor runtime information
   * This mirrors the information available via Python's supervisor.runtime
   *
   * @returns {Object} Runtime information
   */
  getRuntimeInfo() {
    return {
      serial_connected: this.board.serial?.isConnected() || false,
      serial_bytes_available: this.board.serial?.getInputBufferSize() || 0,
      usb_connected: true, // Always true for WASM (virtual USB)
      running: this.state.running,
      in_repl: this.state.inREPL,
      safe_mode: this.state.safeMode,
      autoreload: this.state.autoreload
    }
  }

  /**
   * Expose supervisor state to Python (for supervisor module)
   * This allows Python's supervisor.runtime to access JS state
   */
  exposeToP ython() {
    const runtimeInfo = this.getRuntimeInfo()

    // Set state in WASM module if available
    if (this.module.setSupervisorState) {
      this.module.setSupervisorState(runtimeInfo)
    }
  }
}
