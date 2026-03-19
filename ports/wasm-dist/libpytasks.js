/**
 * libpytasks.js — Emscripten library for background task scheduling
 *
 * Formalizes the "between VM steps" work that currently lives ad-hoc in
 * mp_hal_hook() and api.js runStepped().  Tasks are JS functions registered
 * by name that run at each poll point (every 64 bytecodes via the VM hook,
 * and between VM steps in stepped execution).
 *
 * Built-in tasks registered at init:
 *   'broadcast_flush' — calls mp_bc_out_flush() to drain the ring buffer
 *   'register_sync'   — calls hw_reg_sync_from_bc_in() via the C export
 *
 * Custom tasks can be added from JS:
 *   Module.tasks.register('my_task', () => { ... }, { interval: 100 });
 *
 * The task queue is also accessible from C via mp_tasks_poll() which runs
 * all due tasks and returns the count executed.
 */

mergeInto(LibraryManager.library, {

    /**
     * Initialize the task queue with built-in tasks.
     */
    mp_tasks_init__deps: ['mp_bc_out_flush'],
    mp_tasks_init: function() {
        if (Module._tasksInitialized) return;

        // Task registry: { name → { fn, interval, lastRun } }
        Module._taskRegistry = {};
        // Yield request flag: when set, mp_vm_should_yield returns 1
        Module._taskYieldRequested = false;
        Module._tasksInitialized = true;

        // Built-in task: flush broadcast ring buffer
        Module._taskRegistry['broadcast_flush'] = {
            fn: function() { _mp_bc_out_flush(); },
            interval: 0,   // run every poll
            lastRun: 0,
        };

        // Note: 'register_sync' is registered later by api.js after
        // hw_reg_sync_from_bc_in is available as an export.
    },

    /**
     * Poll all registered tasks.  Called from mp_hal_hook() (every 64
     * bytecodes) and from the JS stepping driver between VM steps.
     *
     * Returns the number of tasks that ran.
     */
    mp_tasks_poll__deps: ['mp_tasks_init'],
    mp_tasks_poll: function() {
        if (!Module._tasksInitialized) _mp_tasks_init();
        const now = Date.now();
        let count = 0;
        const registry = Module._taskRegistry;
        for (const name in registry) {
            const task = registry[name];
            if (task.interval === 0 || (now - task.lastRun) >= task.interval) {
                try {
                    task.fn();
                } catch (e) {
                    console.warn('libpytasks: task', name, 'error:', e);
                }
                task.lastRun = now;
                count++;
            }
        }
        return count;
    },

    /**
     * Request the VM to yield at the next opportunity.
     * Sets a flag that mp_vm_should_yield() checks (in libpyvm.js).
     */
    mp_tasks_request_yield__deps: ['mp_tasks_init'],
    mp_tasks_request_yield: function() {
        if (!Module._tasksInitialized) _mp_tasks_init();
        Module._taskYieldRequested = true;
    },

    /**
     * Check and clear the yield request flag.
     * Called by mp_vm_should_yield() in libpyvm.js.
     * Returns 1 if yield was requested, 0 otherwise.
     */
    mp_tasks_yield_requested__deps: [],
    mp_tasks_yield_requested: function() {
        if (Module._taskYieldRequested) {
            Module._taskYieldRequested = false;
            return 1;
        }
        return 0;
    },
});
