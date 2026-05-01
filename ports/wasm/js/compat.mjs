/**
 * compat.mjs — Compatibility shim for board.mjs.
 *
 * Maps old export names (cp_init, wasm_frame) to wasm-tmp exports
 * (chassis_init, chassis_frame).  Most exports now exist natively
 * in C and are passed through directly.
 *
 * Usage: wrap the WASM instance exports before passing to board.mjs:
 *   const exports = shimExports(instance.exports);
 */

export function shimExports(raw) {
    return new Proxy(raw, {
        get(target, prop) {
            switch (prop) {
                // Name mappings (old → new)
                case 'cp_init':
                    return () => target.chassis_init();
                case 'wasm_frame':
                    return (nowMs, budgetMs) => {
                        const now_us = (nowMs * 1000) | 0;
                        const budget_us = (budgetMs * 1000) | 0;
                        return target.chassis_frame(now_us, budget_us);
                    };
                case 'cp_hw_step':
                    return (nowMs) => {
                        const now_us = (nowMs * 1000) | 0;
                        return target.chassis_frame(now_us, 2000);
                    };

                // Multi-context stubs (not yet implemented in wasm-tmp)
                case 'cp_context_active':
                    return () => 0;
                case 'cp_context_max':
                    return () => 8;
                case 'cp_context_meta_size':
                    return () => 16;
                case 'cp_context_meta_addr':
                    return (id) => 0;
                case 'cp_context_save':
                case 'cp_context_restore':
                case 'cp_context_destroy':
                    return (id) => {};
                case 'cp_run':
                    return () => -4;
                case 'cp_wake':
                    return (id) => {};
                case 'cp_set_c_driven_loop':
                    return (v) => {};

                // Everything else — pass through to real exports
                default:
                    return target[prop];
            }
        }
    });
}
