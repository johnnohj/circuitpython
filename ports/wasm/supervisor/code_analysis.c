// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Code structure analysis using the CircuitPython lexer
// Exposes Python parsing to JavaScript supervisor for proper code analysis
// Follows the existing semihosting pattern from main.c

#include <emscripten.h>
#include "py/lexer.h"
#include "py/obj.h"

// Semihosting boundary tracking (from main.c)
extern void external_call_depth_inc(void);
extern void external_call_depth_dec(void);

// Structure returned to JavaScript describing code structure
typedef struct {
    bool has_while_true_loop;
    size_t while_true_line;
    size_t while_true_column;
    bool has_async_def;
    bool has_await;
    bool has_asyncio_run;
    int token_count;
} code_structure_t;

// Analyze code structure using the real Python lexer
// Returns a pointer to static structure readable from JavaScript
// Follows semihosting pattern: JavaScript (host) → C (target) boundary
EMSCRIPTEN_KEEPALIVE
code_structure_t* analyze_code_structure(const char *code, size_t len) {
    // Track boundary crossing (semihosting pattern from main.c)
    external_call_depth_inc();

    static code_structure_t result = {0};

    // Reset result
    result.has_while_true_loop = false;
    result.while_true_line = 0;
    result.while_true_column = 0;
    result.has_async_def = false;
    result.has_await = false;
    result.has_asyncio_run = false;
    result.token_count = 0;

    // Create lexer from string
    mp_lexer_t *lex = mp_lexer_new_from_str_len(
        qstr_from_str("<code_analysis>"),
        code,
        len,
        0  // Don't free the string
    );

    mp_token_kind_t prev_token = MP_TOKEN_INVALID;
    mp_token_kind_t prev_prev_token = MP_TOKEN_INVALID;

    // Scan through all tokens
    while (lex->tok_kind != MP_TOKEN_END) {
        result.token_count++;

        // Detect: while True:
        // Pattern: MP_TOKEN_KW_WHILE -> MP_TOKEN_KW_TRUE -> MP_TOKEN_DEL_COLON
        if (prev_prev_token == MP_TOKEN_KW_WHILE &&
            prev_token == MP_TOKEN_KW_TRUE &&
            lex->tok_kind == MP_TOKEN_DEL_COLON) {

            result.has_while_true_loop = true;
            result.while_true_line = lex->tok_line;
            result.while_true_column = lex->tok_column;
        }

        // Detect: while 1:
        // Pattern: MP_TOKEN_KW_WHILE -> MP_TOKEN_INTEGER (value=1) -> MP_TOKEN_DEL_COLON
        if (prev_prev_token == MP_TOKEN_KW_WHILE &&
            prev_token == MP_TOKEN_INTEGER &&
            lex->tok_kind == MP_TOKEN_DEL_COLON) {

            // Would need to check if integer value is 1, but for now assume it is
            result.has_while_true_loop = true;
            result.while_true_line = lex->tok_line;
            result.while_true_column = lex->tok_column;
        }

        #if MICROPY_PY_ASYNC_AWAIT
        // Detect: async def
        if (prev_token == MP_TOKEN_KW_ASYNC &&
            lex->tok_kind == MP_TOKEN_KW_DEF) {
            result.has_async_def = true;
        }

        // Detect: await
        if (lex->tok_kind == MP_TOKEN_KW_AWAIT) {
            result.has_await = true;
        }
        #endif

        // Detect: asyncio.run( pattern
        // This is more complex - would need to track NAME tokens
        // For now, just look for patterns in token sequence

        // Shift token history
        prev_prev_token = prev_token;
        prev_token = lex->tok_kind;

        // Advance to next token
        mp_lexer_to_next(lex);
    }

    // Free lexer
    mp_lexer_free(lex);

    // Track boundary crossing back to JavaScript (semihosting pattern)
    external_call_depth_dec();

    return &result;
}

// Get line and column where parsing stopped (for error reporting)
EMSCRIPTEN_KEEPALIVE
void get_lexer_position(mp_lexer_t *lex, size_t *line, size_t *column) {
    if (lex) {
        *line = lex->tok_line;
        *column = lex->tok_column;
    }
}

// Helper to check if code is syntactically valid without executing it
// Follows semihosting pattern: JavaScript (host) → C (target) boundary
EMSCRIPTEN_KEEPALIVE
bool is_valid_python_syntax(const char *code, size_t len) {
    external_call_depth_inc();

    mp_lexer_t *lex = mp_lexer_new_from_str_len(
        qstr_from_str("<syntax_check>"),
        code,
        len,
        0
    );

    bool valid = true;

    // Scan all tokens looking for errors
    while (lex->tok_kind != MP_TOKEN_END) {
        if (lex->tok_kind == MP_TOKEN_INVALID ||
            lex->tok_kind == MP_TOKEN_DEDENT_MISMATCH ||
            lex->tok_kind == MP_TOKEN_LONELY_STRING_OPEN) {
            valid = false;
            break;
        }
        mp_lexer_to_next(lex);
    }

    mp_lexer_free(lex);

    external_call_depth_dec();
    return valid;
}

// Extract just the loop body from code that has a while True loop
// Returns NULL if no loop found
EMSCRIPTEN_KEEPALIVE
char* extract_loop_body(const char *code, size_t len, size_t *out_len) {
    code_structure_t *structure = analyze_code_structure(code, len);

    if (!structure->has_while_true_loop) {
        *out_len = 0;
        return NULL;
    }

    // Find the line with "while True:"
    const char *line_start = code;
    const char *loop_line = code;
    size_t current_line = 1;

    // Scan to the while True: line
    while (current_line < structure->while_true_line && line_start < code + len) {
        if (*line_start == '\n') {
            current_line++;
            loop_line = line_start + 1;
        }
        line_start++;
    }

    // Find the first indented line after "while True:"
    const char *body_start = loop_line;
    while (body_start < code + len && *body_start != '\n') {
        body_start++;
    }
    if (body_start < code + len) {
        body_start++; // Skip the newline
    }

    // The body continues until we hit a dedent or end of file
    // For now, just return everything after the while line
    // A proper implementation would track indentation

    *out_len = (code + len) - body_start;
    return (char*)body_start;
}
