/*
 * main_opfs.c — OPFS-backed executor for WASI CircuitPython
 *
 * The VM runs normally on the C stack — no yield mechanism needed.
 * Background tasks run via the standard CircuitPython infrastructure:
 *
 *   MICROPY_VM_HOOK_LOOP → RUN_BACKGROUND_TASKS
 *     → background_callback_run_all()
 *       → port_background_task() [supervisor/port.c: simulates tick ISR]
 *       → [queued callbacks]
 *         → supervisor_background_tick()
 *           → port_background_tick() → opfs_background_tick() [this file]
 *
 * opfs_background_tick() is where we:
 *   - Flush /dev/repl stdout ring to file
 *   - Check /dev/repl for Ctrl-C from JS
 *   - Periodically checkpoint state to /state/ for crash recovery
 *
 * The worker runs the program to completion (or forever, for code.py
 * with a while True loop). JS on the main thread reads OPFS device
 * files to render UI — never calls into WASM.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/cstack.h"
#include "py/mphal.h"
#include "py/bc.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"

#if MICROPY_ENABLE_PYSTACK
#include "py/pystack.h"
#endif

#include "opfs_state.h"
#include "dev_repl.h"

// ---- Configuration ----

#ifndef MICROPY_GC_HEAP_SIZE
#define MICROPY_GC_HEAP_SIZE (256 * 1024)
#endif

#ifndef OPFS_PYSTACK_SIZE
#define OPFS_PYSTACK_SIZE (8 * 1024)
#endif

#ifndef OPFS_STATE_DIR
#define OPFS_STATE_DIR "/state"
#endif

// Checkpoint every N background task calls
#ifndef OPFS_CHECKPOINT_INTERVAL
#define OPFS_CHECKPOINT_INTERVAL 1000
#endif

// ---- Static buffers (fixed addresses for pointer stability) ----

static char heap[MICROPY_GC_HEAP_SIZE];

#if MICROPY_ENABLE_PYSTACK
static mp_obj_t pystack_buf[OPFS_PYSTACK_SIZE / sizeof(mp_obj_t)];
#endif

// ---- Port-specific state ----

static const char *_state_dir = OPFS_STATE_DIR;
static int _repl_initialized = 0;
static int _checkpoint_counter = 0;

// ---- opfs_background_tick() ----
// Called from supervisor/port.c:port_background_tick() which fires
// via the supervisor_tick → supervisor_background_tick callback chain.
// Services OPFS device files: /dev/repl, state checkpointing.

void opfs_background_tick(void) {
    // Flush /dev/repl stdout ring to file
    if (_repl_initialized) {
        dev_repl_flush();

        // Check for Ctrl-C from JS
        if (dev_repl_check_interrupt()) {
            mp_sched_keyboard_interrupt();
        }
    }

    // Periodic state checkpoint for crash recovery
    if (++_checkpoint_counter >= OPFS_CHECKPOINT_INTERVAL) {
        _checkpoint_counter = 0;
        opfs_save_heap(_state_dir);
        opfs_save_vm(_state_dir);
        opfs_save_pystack(_state_dir);
    }
}

// ---- Execute: compile and run a program ----

static int run_source(const char *src, size_t len) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, src, len, false);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);

        // Execute — runs on C stack normally, with background tasks
        // firing at every branch point via MICROPY_VM_HOOK_LOOP
        mp_call_function_0(module_fun);

        nlr_pop();
        return 0;
    } else {
        mp_obj_print_exception(&mp_stderr_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        return -1;
    }
}

// ---- Main ----

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source.py> [state_dir]\n", argv[0]);
        fprintf(stderr, "       %s -c 'code' [state_dir]\n", argv[0]);
        return 1;
    }

    // Parse args
    const char *src = NULL;
    size_t src_len = 0;
    char *src_alloc = NULL;

    if (strcmp(argv[1], "-c") == 0 && argc >= 3) {
        src = argv[2];
        src_len = strlen(src);
        if (argc >= 4) _state_dir = argv[3];
    } else {
        if (argc >= 3) _state_dir = argv[2];
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) { fprintf(stderr, "Cannot open %s\n", argv[1]); return 1; }
        struct stat st;
        fstat(fd, &st);
        src_len = st.st_size;
        src_alloc = malloc(src_len + 1);
        read(fd, src_alloc, src_len);
        src_alloc[src_len] = '\0';
        close(fd);
        src = src_alloc;
    }

    // Init VM
    mp_cstack_init_with_sp_here(16 * 1024);
    gc_init(heap, heap + MICROPY_GC_HEAP_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    mp_pystack_init(pystack_buf, pystack_buf + OPFS_PYSTACK_SIZE / sizeof(mp_obj_t));
    #endif

    mp_init();

    // Mount VFS
    #if MICROPY_VFS_POSIX
    {
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
        while (MP_STATE_VM(vfs_cur)->next != NULL) {
            MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_cur)->next;
        }
    }
    #endif

    // sys.path is initialized by mp_init() via runtime.c
    // Just append /lib to it
    #if MICROPY_PY_SYS_PATH
    if (mp_sys_path != MP_OBJ_NULL) {
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/lib")));
    }
    #endif

    // Create state directory and save initial state
    mkdir(_state_dir, 0777);
    opfs_save_heap(_state_dir);
    opfs_save_vm(_state_dir);
    opfs_save_pystack(_state_dir);

    // Initialize /dev/repl (USB serial equivalent)
    {
        char repl_path[128];
        snprintf(repl_path, sizeof(repl_path), "%s/../dev/repl", _state_dir);
        char dev_dir[128];
        snprintf(dev_dir, sizeof(dev_dir), "%s/../dev", _state_dir);
        mkdir(dev_dir, 0777);
        if (dev_repl_init(repl_path) == 0) {
            _repl_initialized = 1;
        }
    }

    // Run the program
    fprintf(stderr, "[opfs] running %zu bytes...\n", src_len);
    int ret = run_source(src, src_len);

    // Final checkpoint
    opfs_save_heap(_state_dir);
    opfs_save_vm(_state_dir);
    opfs_save_pystack(_state_dir);

    opfs_exec_meta_t meta = {
        .magic = OPFS_STATE_MAGIC,
        .version = OPFS_STATE_VERSION,
        .state = (ret == 0) ? OPFS_EXEC_COMPLETED : OPFS_EXEC_ERROR,
        .pause_reason = (ret == 0) ? OPFS_PAUSE_NONE : OPFS_PAUSE_EXCEPTION,
        .step_count = 1,
        .sleep_until_ms = 0,
    };
    opfs_save_exec(_state_dir, &meta);

    fprintf(stderr, "[opfs] %s, state saved to %s\n",
            ret == 0 ? "completed" : "error", _state_dir);

    if (src_alloc) free(src_alloc);
    mp_deinit();
    return ret ? 1 : 0;
}

// ---- Required stubs ----

void nlr_jump_fail(void *val) {
    (void)val;
    fprintf(stderr, "FATAL: uncaught NLR\n");
    exit(1);
}

void gc_collect(void) {
    gc_collect_start();
    #include "shared/runtime/gchelper.h"
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)func;
    fprintf(stderr, "Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    exit(1);
}
#endif
