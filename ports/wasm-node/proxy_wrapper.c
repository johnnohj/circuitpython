/*
 * Proxy wrapper for safe initialization and access
 * Prevents memory access errors during early initialization
 */

#include <stdbool.h>
#include <stdint.h>
#include "proxy_c.h"
#include "py/runtime.h"

static bool proxy_initialized = false;

// Check if proxy system is initialized
bool proxy_c_is_initialized(void) {
    return proxy_initialized;
}

// Safe wrapper for proxy_c_init
void proxy_c_init_safe(void) {
    if (!proxy_initialized) {
        // Additional safety check: ensure MicroPython VM is initialized
        extern mp_state_ctx_t mp_state_ctx;
        if (mp_state_ctx.vm.mp_loaded_modules_dict.map.table != NULL) {
            proxy_c_init();
            proxy_initialized = true;
        }
        // If MicroPython not ready yet, initialization will be attempted again later
    }
}

// Safe wrapper for proxy_c_to_js_has_attr
bool proxy_c_to_js_has_attr_safe(uint32_t c_ref, const char *attr_in) {
    if (!proxy_initialized || c_ref == (uint32_t)-1) {
        return false;
    }
    return proxy_c_to_js_has_attr(c_ref, attr_in);
}

// Safe wrapper for proxy_c_to_js_lookup_attr
void proxy_c_to_js_lookup_attr_safe(uint32_t c_ref, const char *attr_in, uint32_t *out) {
    if (!proxy_initialized || c_ref == (uint32_t)-1) {
        // Return undefined reference
        out[0] = 0;
        out[1] = MP_OBJ_JSPROXY_REF_UNDEFINED;
        out[2] = 0;
        return;
    }
    proxy_c_to_js_lookup_attr(c_ref, attr_in, out);
}