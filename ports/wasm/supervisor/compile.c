/*
 * supervisor/compile.c — Unified compilation service.
 *
 * All paths that turn source text into a runnable code_state go through
 * here: REPL expressions, code.py, JS-injected snippets, CLI mode.
 *
 * Returns a code_state allocated on pystack, ready for either:
 *   - vm_yield_step() (browser mode — yield-driven stepping)
 *   - mp_call_function_0() (CLI mode — blocking execution)
 *
 * Errors are printed to mp_plat_print and NULL is returned.
 */

#include <stdio.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/mphal.h"

#include "supervisor/compile.h"

/*
 * Compile source text to a ready-to-run code_state on pystack.
 *
 *   src      — source text (need not be NUL-terminated; len is authoritative)
 *   len      — length of source text
 *   mode     — MP_PARSE_SINGLE_INPUT (REPL), FILE_INPUT (exec), EVAL_INPUT
 *
 * Returns NULL on compile error (already printed).
 */
mp_code_state_t *cp_compile_str(const char *src, size_t len,
                                 mp_parse_input_kind_t mode) {
    mp_obj_t module_fun = MP_OBJ_NULL;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, mode);
        module_fun = mp_compile(&parse_tree, source_name,
            mode == MP_PARSE_SINGLE_INPUT);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        return NULL;
    }

    mp_code_state_t *cs = mp_obj_fun_bc_prepare_codestate(
        module_fun, 0, 0, NULL);
    if (cs == NULL) {
        fprintf(stderr, "[compile] cannot create code state\n");
        return NULL;
    }
    cs->prev = NULL;
    return cs;
}

/*
 * Compile a .py file to a ready-to-run code_state on pystack.
 *
 *   path — filesystem path (e.g. "/code.py")
 *
 * Returns NULL if file doesn't exist or has compile errors.
 */
mp_code_state_t *cp_compile_file(const char *path) {
    mp_obj_t module_fun = MP_OBJ_NULL;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_file(qstr_from_str(path));
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        module_fun = mp_compile(&parse_tree, source_name, false);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        return NULL;
    }

    mp_code_state_t *cs = mp_obj_fun_bc_prepare_codestate(
        module_fun, 0, 0, NULL);
    if (cs == NULL) {
        fprintf(stderr, "[compile] cannot create code state for %s\n", path);
        return NULL;
    }
    cs->prev = NULL;
    return cs;
}
