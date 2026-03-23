/*
 * WASI port main entry point
 *
 * Based on unix port main.c, stripped of signals, CLI complexity,
 * platform-specific code. Runs a blocking REPL on stdin/stdout.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/cstack.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "genhdr/mpversion.h"

#if MICROPY_USE_READLINE == 1
#include "shared/readline/readline.h"
#endif

#if CIRCUITPY_DISPLAYIO
#include "board_display.h"
#endif

// Heap size for GC
#ifndef MICROPY_GC_HEAP_SIZE
#define MICROPY_GC_HEAP_SIZE (512 * 1024)
#endif

static int handle_uncaught_exception(mp_obj_base_t *exc) {
    if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(exc->type), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
        mp_obj_t exit_val = mp_obj_exception_get_value(MP_OBJ_FROM_PTR(exc));
        mp_int_t val = 0;
        if (exit_val != mp_const_none && !mp_obj_get_int_maybe(exit_val, &val)) {
            val = 1;
        }
        return 0x100 | (val & 255);
    }
    mp_obj_print_exception(&mp_stderr_print, MP_OBJ_FROM_PTR(exc));
    return 1;
}

static int execute_from_str(const char *str, mp_parse_input_kind_t input_kind, bool is_repl) {
    mp_hal_set_interrupt_char(CHAR_CTRL_C);

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, str, strlen(str), false);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, is_repl);
        mp_call_function_0(module_fun);
        mp_hal_set_interrupt_char(-1);
        mp_handle_pending(true);
        nlr_pop();
        return 0;
    } else {
        mp_hal_set_interrupt_char(-1);
        mp_handle_pending(false);
        return handle_uncaught_exception(nlr.ret_val);
    }
}

#if MICROPY_USE_READLINE == 1

static int do_repl(void) {
    mp_hal_stdout_tx_str(MICROPY_BANNER_NAME_AND_VERSION);
    mp_hal_stdout_tx_str("; " MICROPY_BANNER_MACHINE);
    mp_hal_stdout_tx_str("\nUse Ctrl-D to exit, Ctrl-E for paste mode\n");

    vstr_t line;
    vstr_init(&line, 16);
    for (;;) {
        mp_hal_stdio_mode_raw();

    input_restart:
        vstr_reset(&line);
        int ret = readline(&line, mp_repl_get_ps1());

        if (ret == CHAR_CTRL_C) {
            mp_hal_stdout_tx_str("\r\n");
            goto input_restart;
        } else if (ret == CHAR_CTRL_D) {
            printf("\n");
            mp_hal_stdio_mode_orig();
            vstr_clear(&line);
            return 0;
        } else if (line.len == 0) {
            if (ret != 0) {
                printf("\n");
            }
            goto input_restart;
        } else {
            while (mp_repl_continue_with_input(vstr_null_terminated_str(&line))) {
                vstr_add_byte(&line, '\n');
                ret = readline(&line, mp_repl_get_ps2());
                if (ret == CHAR_CTRL_C) {
                    printf("\n");
                    goto input_restart;
                } else if (ret == CHAR_CTRL_D) {
                    break;
                }
            }
        }

        mp_hal_stdio_mode_orig();

        ret = execute_from_str(vstr_null_terminated_str(&line), MP_PARSE_SINGLE_INPUT, true);
        if (ret & 0x100) {
            vstr_clear(&line);
            return ret;
        }
    }
}

#else

static int do_repl(void) {
    mp_hal_stdout_tx_str(MICROPY_BANNER_NAME_AND_VERSION);
    mp_hal_stdout_tx_str("; " MICROPY_BANNER_MACHINE);
    mp_hal_stdout_tx_str("\nUse Ctrl-D to exit\n");

    char buf[256];
    for (;;) {
        mp_hal_stdout_tx_str(">>> ");
        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            return 0;
        }
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        int ret = execute_from_str(buf, MP_PARSE_SINGLE_INPUT, true);
        if (ret & 0x100) {
            return ret;
        }
    }
}

#endif

MP_NOINLINE int main_(int argc, char **argv);

int main(int argc, char **argv) {
    mp_cstack_init_with_sp_here(64000);
    return main_(argc, argv);
}

MP_NOINLINE int main_(int argc, char **argv) {
    // Use static heap to avoid malloc issues
    static char heap[MICROPY_GC_HEAP_SIZE];
    gc_init(heap, heap + MICROPY_GC_HEAP_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    static mp_obj_t pystack[1024];
    mp_pystack_init(pystack, &pystack[MP_ARRAY_SIZE(pystack)]);
    #endif

    mp_init();

    #if CIRCUITPY_DISPLAYIO
    // Initialize the framebuffer display and supervisor terminal.
    // This creates the "built-in display" that renders the REPL
    // terminal with the Blinka logo and built-in font, just like
    // a CircuitPython board with a built-in screen.
    board_display_init();
    #endif

    #if MICROPY_VFS_POSIX
    {
        // Mount WASI preopened filesystem at root
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

    // Parse --micropypath from args (before the script filename).
    // Using args instead of env avoids wasi-libc environ buffer issues.
    const char *micropypath = NULL;
    int script_arg = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--micropypath") == 0 && i + 1 < argc) {
            micropypath = argv[++i];
        } else if (argv[i][0] != '-') {
            script_arg = i;
            break;
        }
    }

    // sys.path: mp_init() already sets ["", ".frozen"].
    // Add /lib, then colon-separated entries from --micropypath.
    #if MICROPY_PY_SYS_PATH
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/lib")));
    if (micropypath != NULL) {
        const char *p = micropypath;
        while (*p) {
            const char *end = strchr(p, ':');
            if (end == NULL) {
                end = p + strlen(p);
            }
            if (end > p && !(end - p == 7 && memcmp(p, ".frozen", 7) == 0)) {
                char entry[end - p + 1];
                memcpy(entry, p, end - p);
                entry[end - p] = '\0';
                mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str(entry)));
            }
            p = *end ? end + 1 : end;
        }
    }
    #endif

    // sys.argv: populate from WASI args (script + user args, not our flags)
    #if MICROPY_PY_SYS_ARGV
    for (int i = script_arg; i < argc && i > 0; i++) {
        mp_obj_list_append(mp_sys_argv, MP_OBJ_NEW_QSTR(qstr_from_str(argv[i])));
    }
    #endif

    int ret = 0;
    if (script_arg > 0) {
        // Replace sys.path[0] ("") with script's directory so that
        // imported module __file__ attributes get absolute paths.
        #if MICROPY_PY_SYS_PATH
        {
            const char *script = argv[script_arg];
            const char *last_slash = strrchr(script, '/');
            if (last_slash) {
                size_t dir_len = last_slash - script;
                char dir[dir_len + 1];
                memcpy(dir, script, dir_len);
                dir[dir_len] = '\0';
                mp_obj_t *path_items;
                size_t path_len;
                mp_obj_list_get(mp_sys_path, &path_len, &path_items);
                if (path_len > 0) {
                    path_items[0] = MP_OBJ_NEW_QSTR(qstr_from_str(dir));
                } else {
                    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str(dir)));
                }
            }
        }
        #endif
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_lexer_t *lex = mp_lexer_new_from_file(qstr_from_str(argv[script_arg]));
            qstr source_name = lex->source_name;
            mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
            mp_obj_t mod = mp_compile(&pt, source_name, false);
            mp_call_function_0(mod);
            nlr_pop();
        } else {
            ret = handle_uncaught_exception(nlr.ret_val);
        }
    } else {
        ret = do_repl();
    }

    mp_deinit();

    return ret & 0xff;
}

void nlr_jump_fail(void *val) {
    (void)val;
    fprintf(stderr, "FATAL: uncaught NLR\n");
    exit(1);
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)func;
    fprintf(stderr, "Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    exit(1);
}
#endif
