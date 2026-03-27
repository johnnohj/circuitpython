/*
 * WASM machine module — memory-mapped access to MEMFS regions.
 *
 * On Linux, machine.mem8/16/32 reads /dev/mem via mmap to access
 * physical hardware registers.  On WASM, there's no physical memory —
 * but we have WASM linear memory, and MEMFS regions within it serve
 * the same purpose: named regions of bytes that represent hardware state.
 *
 * For now, mod_machine_mem_get_addr returns the address directly within
 * WASM linear memory.  This means machine.mem8[addr] reads/writes byte
 * at WASM linear memory address `addr` — useful for:
 *
 *   - Reading/writing MEMFS hw_state regions from Python
 *   - Inspecting GC heap, pystack, or other internal state
 *   - Future: mapped MEMFS device endpoints at known base addresses
 *
 * This is the same model as /dev/mem but without the open/mmap dance —
 * WASM linear memory IS the flat address space.
 *
 * This file is included by extmod/modmachine.c via MICROPY_PY_MACHINE_INCLUDEFILE.
 */

// On Linux this would be /dev/mem + mmap.  On WASM, linear memory is
// directly addressable — no file descriptor or mapping needed.

uintptr_t mod_machine_mem_get_addr(mp_obj_t addr_o, uint align) {
    uintptr_t addr = mp_obj_get_int_truncated(addr_o);
    if ((addr & (align - 1)) != 0) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("address %08x is not aligned to %d bytes"), addr, align);
    }
    // In WASM, all linear memory addresses are valid pointers.
    // The WASM runtime traps on out-of-bounds access, so no guard needed.
    return addr;
}

static void mp_machine_idle(void) {
    #ifdef MICROPY_UNIX_MACHINE_IDLE
    MICROPY_UNIX_MACHINE_IDLE
    #else
    // In the frame-budget model, idle could hint to the supervisor
    // that Python is voluntarily yielding its remaining budget.
    // For now, no-op.
    #endif
}
