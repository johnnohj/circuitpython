/*
 * chassis/vm_abort.c — VM abort-resume bridge.
 *
 * Implements the two functions called from VM hooks:
 *   wasm_vm_hook_loop()  — budget check at every backwards branch
 *   wasm_wfe()           — cooperative yield (asyncio sleep, time.sleep)
 *
 * Both trigger mp_sched_vm_abort() which fires nlr_jump_abort() at the
 * next mp_handle_pending() call.  The abort lands at the nlr_set_abort
 * point in chassis_frame().
 *
 * By the time mp_sched_vm_abort() is called, MICROPY_VM_HOOK_LOOP has
 * already saved ip/sp to code_state (see mpconfigport.h).  The C stack
 * is destroyed by nlr_jump_abort, but code_state is on pystack (in
 * port_mem), so resume is safe.
 */

#include <stdint.h>
#include "py/mpconfig.h"
#include "py/runtime.h"
#include "port_memory.h"
#include "budget.h"

/*
 * wasm_vm_hook_loop — called at every backwards branch in vm.c.
 *
 * IMPORTANT: ip/sp have ALREADY been saved to code_state by the
 * MICROPY_VM_HOOK_LOOP macro before this function is called.
 * See mpconfigport.h:
 *   #define MICROPY_VM_HOOK_LOOP \
 *       code_state->ip = ip; \
 *       code_state->sp = sp; \
 *       wasm_vm_hook_loop();
 */
void wasm_vm_hook_loop(void) {
    #if MICROPY_ENABLE_VM_ABORT
    if (budget_soft_expired()) {
        port_mem.vm_abort_reason = VM_ABORT_BUDGET;
        mp_sched_vm_abort();
    }
    #endif
}

/*
 * wasm_wfe — Wait For Event.  Called from mp_event_wait_ms /
 * mp_event_wait_indefinite when the VM is idle.
 *
 * Stores the wakeup deadline in port_mem so JS knows when to call
 * chassis_frame again, then triggers abort to return to JS.
 */
void wasm_wfe(int timeout_ms) {
    #if MICROPY_ENABLE_VM_ABORT
    if (timeout_ms < 0) {
        port_mem.wakeup_ms = 0;  /* indefinite — wake on next event */
    } else {
        /* Use budget's clock for consistency */
        port_mem.wakeup_ms = (uint32_t)(budget_elapsed_us() / 1000) + (uint32_t)timeout_ms;
    }
    port_mem.vm_abort_reason = VM_ABORT_WFE;
    mp_sched_vm_abort();
    #else
    (void)timeout_ms;
    #endif
}

/*
 * mp_hal_delay_ms — override for the WASM port.
 *
 * Instead of busy-waiting, triggers a cooperative yield via WFE.
 * The VM will resume on the next chassis_frame after the deadline.
 */
void mp_hal_delay_ms(mp_uint_t ms) {
    wasm_wfe((int)ms);
    /* If vm_abort is enabled, the above triggers nlr_jump_abort and
     * we never reach here during normal operation.  In CLI mode
     * (no abort), fall through — the caller will busy-wait. */
}
