/**
 * DisplayController - Manages display output using HTML Canvas
 *
 * This bridges CircuitPython's displayio module with Canvas rendering.
 * It provides a virtual display that can be visualized in the browser.
 *
 * Handles:
 * - Canvas initialization and management
 * - Pixel buffer synchronization
 * - displayio Group rendering
 * - Frame updates and refresh
 */

export class DisplayController {
  /**
   * Create a new display controller
   * @param {Object} wasmModule - The loaded WASM module
   * @param {CircuitPythonBoard} board - The parent board instance
   * @param {string} canvasId - Canvas element ID
   */
  constructor(wasmModule, board, canvasId) {
    this.module = wasmModule
    this.board = board
    this.canvasId = canvasId

    // Canvas elements
    this.canvas = null
    this.ctx = null

    // Display configuration
    this.width = 320
    this.height = 240
    this.rotation = 0
    this.scale = 1

    // Frame buffer
    this.frameBuffer = null
    this.dirty = false

    // Update tracking
    this.lastFrameTime = 0
    this.frameInterval = 16  // ~60fps

    // Animation frame handle
    this.animationFrameHandle = null
  }

  /**
   * Initialize the display
   * Finds the canvas element and sets up rendering
   */
  async initialize() {
    console.log(`[Display] Initializing on canvas: ${this.canvasId}`)

    // Find canvas element
    this.canvas = document.getElementById(this.canvasId)
    if (!this.canvas) {
      throw new Error(`Canvas element '${this.canvasId}' not found`)
    }

    // Get 2D context
    this.ctx = this.canvas.getContext('2d')
    if (!this.ctx) {
      throw new Error('Failed to get 2D context')
    }

    // Set canvas size
    this.canvas.width = this.width
    this.canvas.height = this.height

    // Initialize frame buffer
    this.frameBuffer = this.ctx.createImageData(this.width, this.height)

    // Clear canvas
    this.clear()

    console.log(`[Display] Initialized ${this.width}x${this.height} display`)
  }

  /**
   * Clear the display
   * @param {number} color - RGB565 color (default: black)
   */
  clear(color = 0x0000) {
    if (!this.frameBuffer) return

    // Convert RGB565 to RGB888
    const r = ((color >> 11) & 0x1F) * 255 / 31
    const g = ((color >> 5) & 0x3F) * 255 / 63
    const b = (color & 0x1F) * 255 / 31

    // Fill frame buffer
    const data = this.frameBuffer.data
    for (let i = 0; i < data.length; i += 4) {
      data[i] = r      // Red
      data[i + 1] = g  // Green
      data[i + 2] = b  // Blue
      data[i + 3] = 255  // Alpha
    }

    this.dirty = true
  }

  /**
   * Set a pixel color
   * @param {number} x - X coordinate
   * @param {number} y - Y coordinate
   * @param {number} color - RGB565 color
   */
  setPixel(x, y, color) {
    if (!this.frameBuffer) return
    if (x < 0 || x >= this.width || y < 0 || y >= this.height) return

    // Convert RGB565 to RGB888
    const r = ((color >> 11) & 0x1F) * 255 / 31
    const g = ((color >> 5) & 0x3F) * 255 / 63
    const b = (color & 0x1F) * 255 / 31

    // Set pixel in frame buffer
    const offset = (y * this.width + x) * 4
    const data = this.frameBuffer.data
    data[offset] = r
    data[offset + 1] = g
    data[offset + 2] = b
    data[offset + 3] = 255

    this.dirty = true
  }

  /**
   * Get a pixel color
   * @param {number} x - X coordinate
   * @param {number} y - Y coordinate
   * @returns {number} RGB565 color
   */
  getPixel(x, y) {
    if (!this.frameBuffer) return 0
    if (x < 0 || x >= this.width || y < 0 || y >= this.height) return 0

    const offset = (y * this.width + x) * 4
    const data = this.frameBuffer.data

    const r = data[offset]
    const g = data[offset + 1]
    const b = data[offset + 2]

    // Convert RGB888 to RGB565
    const r5 = Math.floor(r * 31 / 255)
    const g6 = Math.floor(g * 63 / 255)
    const b5 = Math.floor(b * 31 / 255)

    return (r5 << 11) | (g6 << 5) | b5
  }

  /**
   * Fill a rectangle
   * @param {number} x - X coordinate
   * @param {number} y - Y coordinate
   * @param {number} width - Rectangle width
   * @param {number} height - Rectangle height
   * @param {number} color - RGB565 color
   */
  fillRect(x, y, width, height, color) {
    for (let dy = 0; dy < height; dy++) {
      for (let dx = 0; dx < width; dx++) {
        this.setPixel(x + dx, y + dy, color)
      }
    }
  }

  /**
   * Blit a bitmap to the display
   * @param {number} x - X coordinate
   * @param {number} y - Y coordinate
   * @param {Uint16Array} bitmap - RGB565 bitmap data
   * @param {number} width - Bitmap width
   * @param {number} height - Bitmap height
   */
  blit(x, y, bitmap, width, height) {
    let idx = 0
    for (let dy = 0; dy < height; dy++) {
      for (let dx = 0; dx < width; dx++) {
        this.setPixel(x + dx, y + dy, bitmap[idx++])
      }
    }
  }

  /**
   * Update frame (refresh display)
   * This should be called periodically to sync frame buffer to canvas
   */
  updateFrame() {
    if (!this.dirty || !this.frameBuffer || !this.ctx) {
      return
    }

    // Throttle updates
    const now = Date.now()
    if (now - this.lastFrameTime < this.frameInterval) {
      return
    }

    // Put image data to canvas
    this.ctx.putImageData(this.frameBuffer, 0, 0)

    this.dirty = false
    this.lastFrameTime = now
  }

  /**
   * Force immediate refresh
   */
  refresh() {
    if (!this.frameBuffer || !this.ctx) return

    this.ctx.putImageData(this.frameBuffer, 0, 0)
    this.dirty = false
  }

  /**
   * Set display rotation
   * @param {number} rotation - Rotation in degrees (0, 90, 180, 270)
   */
  setRotation(rotation) {
    this.rotation = rotation
    console.log(`[Display] Rotation set to ${rotation}°`)

    // TODO: Implement rotation transformation
  }

  /**
   * Set display scale
   * @param {number} scale - Scale factor
   */
  setScale(scale) {
    this.scale = scale

    if (this.canvas) {
      this.canvas.style.width = `${this.width * scale}px`
      this.canvas.style.height = `${this.height * scale}px`
    }

    console.log(`[Display] Scale set to ${scale}x`)
  }

  /**
   * Get display dimensions
   * @returns {Object} Width and height
   */
  getDimensions() {
    return {
      width: this.width,
      height: this.height
    }
  }

  /**
   * Set display dimensions
   * @param {number} width - Display width
   * @param {number} height - Display height
   */
  setDimensions(width, height) {
    this.width = width
    this.height = height

    if (this.canvas) {
      this.canvas.width = width
      this.canvas.height = height
    }

    if (this.ctx) {
      this.frameBuffer = this.ctx.createImageData(width, height)
    }

    console.log(`[Display] Dimensions set to ${width}x${height}`)
  }

  /**
   * Reset display (called during soft reset)
   */
  reset() {
    console.log('[Display] Resetting...')

    // Clear display
    this.clear()

    // Refresh immediately
    this.refresh()

    console.log('[Display] Reset complete')
  }

  /**
   * Cleanup display controller
   */
  cleanup() {
    console.log('[Display] Cleaning up...')

    // Stop any pending animation frames
    if (this.animationFrameHandle !== null) {
      cancelAnimationFrame(this.animationFrameHandle)
      this.animationFrameHandle = null
    }

    // Clear canvas
    if (this.ctx && this.canvas) {
      this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height)
    }

    // Clear references
    this.canvas = null
    this.ctx = null
    this.frameBuffer = null

    console.log('[Display] Cleanup complete')
  }

  /**
   * Get frame buffer for direct access
   * This allows displayio to write directly to the buffer
   *
   * @returns {ImageData} Frame buffer
   */
  getFrameBuffer() {
    return this.frameBuffer
  }

  /**
   * Mark frame buffer as dirty (needs refresh)
   */
  markDirty() {
    this.dirty = true
  }

  /**
   * Check if display is dirty
   * @returns {boolean} True if needs refresh
   */
  isDirty() {
    return this.dirty
  }

  /**
   * Get display info for debugging
   * @returns {Object} Display information
   */
  getInfo() {
    return {
      canvasId: this.canvasId,
      width: this.width,
      height: this.height,
      rotation: this.rotation,
      scale: this.scale,
      dirty: this.dirty,
      hasCanvas: this.canvas !== null,
      hasContext: this.ctx !== null
    }
  }
}
