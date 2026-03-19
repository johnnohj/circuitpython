/**
 * libpyvm.js — Emscripten library for step-wise Python VM execution
 *
 * Provides the JS-side implementation of mp_vm_should_yield(), which the
 * VM dispatch loop (py/vm.c) calls at every branch point when
 * MICROPY_VM_YIELD_ENABLED is set.
 *
 * The JS driver controls execution by setting a step budget before each
 * mp_vm_step() call.  When the budget is exhausted, the VM saves its state
 * into the heap-allocated code_state chain (stackless mode) and returns
 * MP_VM_RETURN_YIELD.  The driver can then do background work (register
 * sync, bc_out drain, event loop yield) before resuming.
 *
 * Usage from JS:
 *   Module._vm_step_budget = 256;       // set budget
 *   const status = Module._mp_vm_step();  // execute up to 256 branch points
 *   // status: 0 = normal, 1 = yielded (suspended), 2 = exception
 */

mergeInto(LibraryManager.library, {

    /**
     * Called from vm.c at every branch point (pending_exception_check).
     * Returns 1 (true) when the step budget is exhausted → VM suspends.
     * Returns 0 (false) to continue executing.
     *
     * The budget counts branch points, not bytecodes — roughly one per
     * loop iteration, if/else, or function call.  This gives predictable
     * granularity regardless of straight-line code length.
     */
    mp_vm_should_yield__deps: ['mp_tasks_yield_requested'],
    mp_vm_should_yield: function() {
        // Check if a task (e.g. libpyasync) explicitly requested a yield
        if (_mp_tasks_yield_requested()) {
            return 1;
        }
        // No step budget set → run-to-completion (backward compat with vm.run)
        if (Module._vm_step_budget === undefined) {
            return 0;
        }
        // Budget remaining → continue
        if (Module._vm_step_budget > 0) {
            Module._vm_step_budget--;
            return 0;
        }
        // Budget exhausted → yield.
        // Reset to undefined so a subsequent mp_js_run() (non-stepping)
        // doesn't accidentally yield.
        Module._vm_step_budget = undefined;
        return 1;
    },
});
