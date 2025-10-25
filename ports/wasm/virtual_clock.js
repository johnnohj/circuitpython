/**
 * Virtual Clock System for CircuitPython WASM
 *
 * Simulates a 32kHz crystal oscillator (like Renode's Simple32kHz.cs)
 * Writes directly to WASM shared memory - no message passing needed!
 *
 * This is the "conductor" that controls the tempo of Python execution.
 */

// Timing modes
const TimeMode = {
    REALTIME: 0,      // 1:1 with wall clock (default for demos)
    MANUAL: 1,        // Step-by-step (for education/debugging)
    FAST_FORWARD: 2   // Skip delays (for testing)
};

class VirtualClock {
    constructor(wasmInstance, wasmMemory, verbose = false) {
        this.wasmMemory = wasmMemory;
        this.verbose = verbose;

        // Get pointer to shared virtual_clock_hw struct
        this.virtualHardwarePtr = wasmInstance.exports.get_virtual_clock_hw_ptr();

        // Create DataView for direct memory access
        this.hardware = new DataView(wasmMemory.buffer, this.virtualHardwarePtr);

        // Timing state
        this.mode = TimeMode.REALTIME;
        this.realtimeIntervalId = null;
        this.startWallTime = 0;
        this.pausedAt = 0;

        // Event timeline for debugging
        this.timeline = [];

        if (this.verbose) {
            if (this.verbose) console.error(`[VirtualClock] Initialized at memory offset ${this.virtualHardwarePtr}`);
        }
    }

    // ==========================================================================
    // SHARED MEMORY ACCESS - Direct reads/writes (like memory-mapped hardware)
    // ==========================================================================

    getTicks32kHz() {
        // Read uint64_t at offset 0
        const low = this.hardware.getUint32(0, true);
        const high = this.hardware.getUint32(4, true);
        return (BigInt(high) << 32n) | BigInt(low);
    }

    setTicks32kHz(ticks) {
        // Write uint64_t at offset 0
        this.hardware.setUint32(0, Number(ticks & 0xFFFFFFFFn), true);
        this.hardware.setUint32(4, Number(ticks >> 32n), true);

        // Update JS tick counter at offset 20
        const jsTicksLow = this.hardware.getUint32(20, true);
        const jsTicksHigh = this.hardware.getUint32(24, true);
        const jsTicks = (BigInt(jsTicksHigh) << 32n) | BigInt(jsTicksLow);
        const newJsTicks = jsTicks + 1n;
        this.hardware.setUint32(20, Number(newJsTicks & 0xFFFFFFFFn), true);
        this.hardware.setUint32(24, Number(newJsTicks >> 32n), true);
    }

    getCpuFrequency() {
        // Read uint32_t at offset 8
        return this.hardware.getUint32(8, true);
    }

    setCpuFrequency(hz) {
        // Write uint32_t at offset 8
        this.hardware.setUint32(8, hz, true);
    }

    setTimeMode(mode) {
        // Write uint8_t at offset 12
        this.hardware.setUint8(12, mode);
    }

    // ==========================================================================
    // MODE 1: REALTIME - Simulates real hardware 1:1 with wall clock
    // ==========================================================================

    startRealtime() {
        this.stopRealtime();
        this.mode = TimeMode.REALTIME;
        this.setTimeMode(TimeMode.REALTIME);
        this.startWallTime = performance.now();

        // Increment by 32 ticks every 1ms (32kHz = 32 ticks per ms)
        this.realtimeIntervalId = setInterval(() => {
            const currentTicks = this.getTicks32kHz();
            this.setTicks32kHz(currentTicks + 32n);
        }, 1);

        if (this.verbose) console.error('[VirtualClock] Started REALTIME mode (1:1 with wall clock)');
    }

    stopRealtime() {
        if (this.realtimeIntervalId !== null) {
            clearInterval(this.realtimeIntervalId);
            this.realtimeIntervalId = null;
        }
    }

    // ==========================================================================
    // MODE 2: MANUAL - Step-by-step control for education
    // ==========================================================================

    setManualMode() {
        this.stopRealtime();
        this.mode = TimeMode.MANUAL;
        this.setTimeMode(TimeMode.MANUAL);
        this.pausedAt = performance.now();
        if (this.verbose) console.error('[VirtualClock] Switched to MANUAL mode (step-by-step)');
    }

    /**
     * Advance virtual time by specified milliseconds
     * Perfect for educational "slow motion" debugging
     */
    advanceMs(milliseconds) {
        const ticks32kHz = BigInt(Math.floor(milliseconds * 32));
        const currentTicks = this.getTicks32kHz();
        const newTicks = currentTicks + ticks32kHz;
        this.setTicks32kHz(newTicks);

        this.timeline.push({
            time: newTicks,
            event: `Advanced ${milliseconds}ms (${ticks32kHz} ticks)`
        });

        if (this.verbose) console.error(`[VirtualClock] Advanced ${milliseconds}ms to ${this.getCurrentTimeMs()}ms`);
    }

    /**
     * Advance to a specific virtual time
     */
    advanceToMs(targetMs) {
        const currentMs = this.getCurrentTimeMs();
        if (targetMs > currentMs) {
            this.advanceMs(targetMs - currentMs);
        }
    }

    // ==========================================================================
    // MODE 3: FAST FORWARD - Skip delays for testing
    // ==========================================================================

    setFastForwardMode() {
        this.stopRealtime();
        this.mode = TimeMode.FAST_FORWARD;
        this.setTimeMode(TimeMode.FAST_FORWARD);
        if (this.verbose) console.error('[VirtualClock] Switched to FAST_FORWARD mode (skip delays)');
    }

    /**
     * In fast-forward mode, time.sleep() advances virtual time instantly
     * Called when WASM yields after detecting a sleep
     */
    onSleepDetected(sleepDurationMs) {
        if (this.mode === TimeMode.FAST_FORWARD) {
            this.advanceMs(sleepDurationMs);
            if (this.verbose) console.error(`[VirtualClock] Fast-forwarded through ${sleepDurationMs}ms sleep`);
        }
    }

    // ==========================================================================
    // QUERY METHODS
    // ==========================================================================

    getCurrentTimeMs() {
        const ticks = this.getTicks32kHz();
        // Convert 32kHz ticks to milliseconds
        return Number(ticks) / 32;
    }

    getCurrentTimeS() {
        return this.getCurrentTimeMs() / 1000;
    }

    getMode() {
        return this.mode;
    }

    getTimeline() {
        return [...this.timeline];
    }

    // ==========================================================================
    // CPU FREQUENCY CONTROL
    // ==========================================================================

    /**
     * Set virtual CPU frequency (affects instruction timing)
     * Python code: microcontroller.cpu.frequency = 48_000_000
     */
    setVirtualCpuFrequency(hz) {
        this.setCpuFrequency(hz);
        if (this.verbose) console.error(`[VirtualClock] CPU frequency set to ${hz / 1_000_000}MHz`);
    }

    getVirtualCpuFrequency() {
        return this.getCpuFrequency();
    }

    // ==========================================================================
    // EDUCATIONAL FEATURES
    // ==========================================================================

    /**
     * Record an event in the timeline
     * Example: "GPIO pin 5 set HIGH", "I2C write to 0x48"
     */
    recordEvent(event) {
        const currentTicks = this.getTicks32kHz();
        this.timeline.push({ time: currentTicks, event });
    }

    /**
     * Get formatted timeline for display
     */
    getFormattedTimeline() {
        return this.timeline.map(entry => {
            const timeMs = Number(entry.time) / 32;
            return `T+${timeMs.toFixed(3)}ms: ${entry.event}`;
        });
    }

    /**
     * Clear timeline (for new run)
     */
    clearTimeline() {
        this.timeline = [];
    }

    /**
     * Reset virtual time to zero
     */
    reset() {
        this.setTicks32kHz(0n);
        this.clearTimeline();
        if (this.verbose) console.error('[VirtualClock] Reset to T=0');
    }

    // ==========================================================================
    // STATISTICS
    // ==========================================================================

    getStatistics() {
        // Read WASM yields count at offset 16
        const yieldsLow = this.hardware.getUint32(16, true);
        const yieldsHigh = this.hardware.getUint32(16, true);
        const wasmYields = (BigInt(yieldsHigh) << 32n) | BigInt(yieldsLow);

        // Read JS ticks count at offset 20
        const ticksLow = this.hardware.getUint32(20, true);
        const ticksHigh = this.hardware.getUint32(24, true);
        const jsTicks = (BigInt(ticksHigh) << 32n) | BigInt(ticksLow);

        return {
            virtualTimeMs: this.getCurrentTimeMs(),
            cpuFrequencyMHz: this.getCpuFrequency() / 1_000_000,
            wasmYields,
            jsTickUpdates: jsTicks,
            timelineEvents: this.timeline.length
        };
    }
}

// Make available globally for concatenated build
if (typeof globalThis !== 'undefined') {
    globalThis.VirtualClock = VirtualClock;
    globalThis.TimeMode = TimeMode;
}

// Export for use in other modules (both CommonJS and ES modules)
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { VirtualClock, TimeMode };
}

// ES module export
export { VirtualClock, TimeMode };

/**
 * Example usage for educators:
 *
 * // Real-time demo (default)
 * clock.startRealtime();
 *
 * // Step through code
 * clock.setManualMode();
 * clock.advanceMs(0.5);  // See exactly what happens in 0.5ms
 *
 * // Fast testing
 * clock.setFastForwardMode();
 * // time.sleep(10) completes instantly!
 */
