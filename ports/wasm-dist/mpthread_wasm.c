/*
 * mpthread_wasm.c — WASM-dist thread implementation
 *
 * Maps MicroPython's _thread API to WebWorker spawning via BroadcastChannel.
 * Each mp_thread_create() call serializes a worker_spawn message to /dev/bc_out;
 * PythonHost.js reads this on the next mp_js_hook() and spawns a new Worker.
 *
 * Within a single Worker, Python is single-threaded — mutexes are no-ops.
 */

#include "py/mpconfig.h"

#if MICROPY_PY_THREAD

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "py/mpthread.h"
#include "py/mpstate.h"
#include "py/obj.h"
#include "py/objfun.h"
#include "py/bc.h"
#include "py/runtime.h"
#include "vdev.h"
#include "memfs_state.h"

/* ---- Thread state ---- */

/* Each WASM worker has exactly one Python thread; state is static. */
static mp_state_thread_t *_thread_state = NULL;
static mp_uint_t _thread_id_counter = 1;

mp_state_thread_t *mp_thread_get_state(void) {
    return _thread_state ? _thread_state : &mp_state_ctx.thread;
}

void mp_thread_set_state(mp_state_thread_t *state) {
    _thread_state = state;
}

mp_uint_t mp_thread_get_id(void) {
    return 1; /* always thread 1 inside a worker */
}

void mp_thread_start(void) {
    /* no-op: worker is already running */
}

void mp_thread_finish(void) {
    /* no-op: worker exits naturally */
}

/* ---- Thread creation → Worker spawn ---- */

/*
 * Append a JSON-escaped string to a vstr.
 * Escapes: " → \", \ → \\, control chars → \n \r \t
 */
static void vstr_add_json_str(vstr_t *v, const char *s) {
    for (; *s; s++) {
        switch (*s) {
            case '"':  vstr_add_str(v, "\\\""); break;
            case '\\': vstr_add_str(v, "\\\\"); break;
            case '\n': vstr_add_str(v, "\\n");  break;
            case '\r': vstr_add_str(v, "\\r");  break;
            case '\t': vstr_add_str(v, "\\t");  break;
            default:   vstr_add_byte(v, *s);    break;
        }
    }
}

/*
 * Serialize a _thread.start_new_thread() call to /dev/bc_out so that
 * PythonHost.js can spawn a WebWorker on the next mp_js_hook().
 *
 * Message format (NDJSON line):
 *   {"type":"worker_spawn","payload":{
 *     "workerId":"exec-thread-N",
 *     "func":"<qualname>","module":"<module>",
 *     "args":[...],
 *     "run_code":"import <module>; <module>.<func>(*<args>)"
 *   }}
 *
 * Phase 1: only top-level named functions are supported.
 * The spawned worker loads the module, looks up the function by qualname,
 * and calls it with the deserialized args.
 */
mp_uint_t mp_thread_create(void *(*entry)(void *), void *arg, size_t *stack_size) {
    (void)entry;   /* we don't run the entry function locally */
    (void)stack_size;

    mp_uint_t id = _thread_id_counter++;

    /* arg is thread_entry_args_t from py/modthread.c (must match exactly) */
    typedef struct {
        mp_obj_dict_t *dict_locals;
        mp_obj_dict_t *dict_globals;
        size_t stack_size;
        mp_obj_t fun;
        size_t n_args;
        size_t n_kw;
        mp_obj_t args[];
    } thread_entry_args_t;

    thread_entry_args_t *th = (thread_entry_args_t *)arg;

    /* Extract function identity.
     * MicroPython functions have __name__ but not __qualname__ or __module__.
     * Module name is obtained from the function's bytecode context globals. */
    const char *func_name = "unknown";
    const char *module_name = "__main__";

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        /* __name__ works for all Python functions */
        mp_obj_t nm = mp_load_attr(th->fun, MP_QSTR___name__);
        if (mp_obj_is_str(nm)) { func_name = mp_obj_str_get_str(nm); }

        /* Module name: read __name__ from the bytecode function's globals dict */
        if (mp_obj_is_type(th->fun, &mp_type_fun_bc)) {
            mp_obj_fun_bc_t *fbc = MP_OBJ_TO_PTR(th->fun);
            if (fbc->context && fbc->context->module.globals) {
                mp_obj_t key = MP_OBJ_NEW_QSTR(MP_QSTR___name__);
                mp_map_elem_t *e = mp_map_lookup(
                    &fbc->context->module.globals->map, key, MP_MAP_LOOKUP);
                if (e && mp_obj_is_str(e->value)) {
                    module_name = mp_obj_str_get_str(e->value);
                }
            }
        }
        nlr_pop();
    } else { /* ignore attribute errors */ }

    /* Build args JSON array (also used in run_code) */
    vstr_t args_json;
    vstr_init(&args_json, 64);
    vstr_add_byte(&args_json, '[');
    for (size_t i = 0; i < th->n_args; i++) {
        if (i > 0) { vstr_add_byte(&args_json, ','); }
        mp_obj_to_json_str(th->args[i], &args_json, 4);
    }
    vstr_add_byte(&args_json, ']');
    vstr_add_byte(&args_json, '\0'); /* null-terminate for use in snprintf */

    /* Build run_code: "import module; module.func(*args)" or "func(*args)" */
    char run_code[512];
    if (strcmp(module_name, "__main__") == 0) {
        snprintf(run_code, sizeof(run_code), "%s(*%s)", func_name, args_json.buf);
    } else {
        snprintf(run_code, sizeof(run_code),
            "import %s; %s.%s(*%s)", module_name, module_name, func_name, args_json.buf);
    }

    /* Generate a worker ID for this thread */
    char worker_id[32];
    snprintf(worker_id, sizeof(worker_id), "exec-thread-%u", (unsigned)id);

    /* Build the complete JSON message with payload wrapper */
    vstr_t msg;
    vstr_init(&msg, 256);
    vstr_add_str(&msg, "{\"type\":\"worker_spawn\",\"payload\":{\"workerId\":\"");
    vstr_add_str(&msg, worker_id);
    vstr_add_str(&msg, "\",\"func\":\"");
    vstr_add_json_str(&msg, func_name);
    vstr_add_str(&msg, "\",\"module\":\"");
    vstr_add_json_str(&msg, module_name);
    vstr_add_str(&msg, "\",\"args\":");
    /* args_json.len includes null terminator; write len-1 bytes */
    for (size_t i = 0; i < args_json.len - 1; i++) {
        vstr_add_byte(&msg, args_json.buf[i]);
    }
    vstr_add_str(&msg, ",\"run_code\":\"");
    vstr_add_json_str(&msg, run_code);
    vstr_add_str(&msg, "\"}}");

    vstr_clear(&args_json);

    /* Enqueue to broadcast ring buffer — mp_tasks_poll() will flush it */
    extern void mp_bc_out_enqueue(const char *json, size_t len);
    mp_bc_out_enqueue(msg.buf, msg.len);

    vstr_clear(&msg);
    return id;
}

/* ---- Mutex stubs (no-op: single-threaded per worker) ---- */

void mp_thread_mutex_init(mp_thread_mutex_t *mutex) {
    mutex->locked = 0;
}

int mp_thread_mutex_lock(mp_thread_mutex_t *mutex, int wait) {
    (void)wait;
    if (mutex->locked) {
        return 0; /* would block — signal failure rather than deadlock */
    }
    mutex->locked = 1;
    return 1;
}

void mp_thread_mutex_unlock(mp_thread_mutex_t *mutex) {
    mutex->locked = 0;
}

#if MICROPY_PY_THREAD_RECURSIVE_MUTEX
void mp_thread_recursive_mutex_init(mp_thread_recursive_mutex_t *mutex) {
    mutex->locked = 0;
    mutex->count = 0;
}

int mp_thread_recursive_mutex_lock(mp_thread_recursive_mutex_t *mutex, int wait) {
    (void)wait;
    mutex->count++;
    mutex->locked = 1;
    return 1;
}

void mp_thread_recursive_mutex_unlock(mp_thread_recursive_mutex_t *mutex) {
    if (mutex->count > 0) {
        mutex->count--;
    }
    if (mutex->count == 0) {
        mutex->locked = 0;
    }
}
#endif /* MICROPY_PY_THREAD_RECURSIVE_MUTEX */

#endif /* MICROPY_PY_THREAD */
