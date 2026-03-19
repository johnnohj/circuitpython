/**
 * libpyasync.js — Emscripten library for cooperative async scheduling
 *
 * Turns Python sleep/delay calls into real JS event loop yields with
 * proper timing.  Instead of busy-waiting in a tight loop (where the
 * VM hook fires every 64 bytecodes), Python code requests an async
 * delay and the VM yields immediately.  The JS stepping driver
 * (runStepped) then uses setTimeout(delay) before resuming.
 *
 * Flow:
 *   Python: time.sleep(0.5)
 *     → C: mp_hal_delay_ms(500)
 *       → C: mp_async_request_delay(500)
 *         → JS: Module._asyncDelayMs = 500
 *       → C: mp_tasks_request_yield()
 *         → VM yields at next branch point
 *     → JS: runStepped() reads Module._asyncDelayMs = 500
 *       → await setTimeout(500)   ← real JS event loop yield!
 *       → Module._asyncDelayMs = 0
 *       → resume VM
 *
 * This also works for asyncio.sleep():
 *   asyncio core detects dt > 0 (next task not ready)
 *     → calls _blinka.async_sleep(dt)
 *       → mp_async_request_delay(dt)
 *       → mp_tasks_request_yield()
 *     → asyncio yields to the stepping driver
 *     → runStepped() waits the real delay, resumes
 *
 * When not running in stepped mode (vm.run with timeout), the delay
 * is ignored — mp_hal_delay_ms falls back to busy-wait as before.
 */

mergeInto(LibraryManager.library, {

    /**
     * Initialize the async scheduling state.
     */
    mp_async_init: function() {
        if (Module._asyncInitialized) return;
        // Requested delay in ms (0 = no delay pending)
        Module._asyncDelayMs = 0;
        Module._asyncInitialized = true;
    },

    /**
     * Request an async delay.  Called from C (mp_hal_delay_ms, asyncio core).
     * Sets the delay and requests a VM yield so the stepping driver can
     * honour it with a real setTimeout.
     *
     * @param ms  Delay in milliseconds (0 = yield without delay)
     */
    mp_async_request_delay__deps: ['mp_async_init', 'mp_tasks_request_yield'],
    mp_async_request_delay: function(ms) {
        if (!Module._asyncInitialized) _mp_async_init();
        // Accumulate: if multiple delays requested before yield,
        // take the maximum (they overlap, not chain).
        if (ms > Module._asyncDelayMs) {
            Module._asyncDelayMs = ms;
        }
        // Only request a VM yield when in stepped mode (runStepped).
        // In non-stepped mode (vm.run), _vm_step_budget is undefined
        // and the busy-wait fallback in mp_hal_delay_ms / asyncio core
        // handles the delay.  Requesting a yield in non-stepped mode
        // causes mp_js_do_exec_abortable to misinterpret the yield as
        // normal completion, truncating execution.
        if (Module._vm_step_budget !== undefined) {
            _mp_tasks_request_yield();
        }
    },

    /**
     * Read and clear the pending async delay.
     * Called by runStepped() between VM steps.
     *
     * @returns {number} The pending delay in ms (0 = no delay)
     */
    mp_async_consume_delay__deps: ['mp_async_init'],
    mp_async_consume_delay: function() {
        if (!Module._asyncInitialized) return 0;
        var ms = Module._asyncDelayMs;
        Module._asyncDelayMs = 0;
        return ms;
    },

    /**
     * Check if an async delay is pending (non-blocking query).
     * @returns {number} 1 if delay pending, 0 otherwise
     */
    mp_async_has_delay__deps: ['mp_async_init'],
    mp_async_has_delay: function() {
        if (!Module._asyncInitialized) return 0;
        return Module._asyncDelayMs > 0 ? 1 : 0;
    },
});
