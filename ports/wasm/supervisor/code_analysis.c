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
#include "py/nlr.h"

// Semihosting boundary tracking (from main.c)
extern void external_call_depth_inc(void);
extern void external_call_depth_dec(void);

// Loop type enumeration
typedef enum {
    LOOP_WHILE_TRUE = 0,      // while True:
    LOOP_WHILE_NUMERIC = 1,   // while 1:
    LOOP_WHILE_GENERAL = 2,   // while <condition>:
    LOOP_FOR_GENERAL = 3,     // for x in y:
    LOOP_FOR_RANGE = 4,       // for i in range(...):
} loop_type_t;

// Information about a single loop
typedef struct loop_info {
    loop_type_t loop_type;
    size_t line;
    size_t column;
    bool needs_instrumentation;  // Should this loop be instrumented for yielding?
} loop_info_t;

// Structure returned to JavaScript describing code structure
typedef struct code_structure {
    loop_info_t loops[16];  // Support up to 16 loops
    int loop_count;

    // Legacy fields for backward compatibility
    bool has_while_true_loop;
    size_t while_true_line;
    size_t while_true_column;

    // Async detection
    bool has_async_def;
    bool has_await;
    bool has_asyncio_run;
    int token_count;
} code_structure_t;

// Forward declarations for export registry
code_structure_t* analyze_code_structure(const char *code, size_t len);
bool is_valid_python_syntax(const char *code, size_t len);
char* extract_loop_body(const char *code, size_t len, size_t *out_len);

// Helper function to add a loop to the result structure
static void add_loop(code_structure_t *result, loop_type_t type, size_t line, size_t column) {
    if (result->loop_count >= 16) {
        return;  // Max loops reached
    }

    loop_info_t *loop = &result->loops[result->loop_count++];
    loop->loop_type = type;
    loop->line = line;
    loop->column = column;

    // Determine if this loop needs instrumentation
    // Instrument: while True, while 1, general while, and general for loops
    // Don't instrument: for i in range(n) with known finite n
    loop->needs_instrumentation = (type != LOOP_FOR_RANGE);

    // Update legacy fields for backward compatibility
    if (type == LOOP_WHILE_TRUE && !result->has_while_true_loop) {
        result->has_while_true_loop = true;
        result->while_true_line = line;
        result->while_true_column = column;
    }
}

// Analyze code structure using the real Python lexer
// Returns a pointer to static structure readable from JavaScript
// Follows semihosting pattern: JavaScript (host) → C (target) boundary
EMSCRIPTEN_KEEPALIVE
__attribute__((used))
code_structure_t* analyze_code_structure(const char *code, size_t len) {
    // Track boundary crossing (semihosting pattern from main.c)
    external_call_depth_inc();

    static code_structure_t result = {0};

    // Reset result
    result.loop_count = 0;
    result.has_while_true_loop = false;
    result.while_true_line = 0;
    result.while_true_column = 0;
    result.has_async_def = false;
    result.has_await = false;
    result.has_asyncio_run = false;
    result.token_count = 0;

    // Protect lexer operations with exception handling
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Create lexer from string
        // Use predefined qstr to avoid memory allocation during qstr creation
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_,  // "<stdin>" qstr
            code,
            len,
            0  // Don't free the string
        );

        mp_token_kind_t prev_token = MP_TOKEN_INVALID;

        // Track when we're inside a while statement
        bool in_while_loop = false;
        bool while_is_true = false;  // true if "while True:"
        bool while_is_numeric = false;  // true if "while 1:"
        size_t while_loop_line = 0;
        size_t while_loop_column = 0;

        // Track when we're inside a for statement
        bool in_for_loop = false;
        size_t for_loop_line = 0;
        size_t for_loop_column = 0;

        // Scan through all tokens
        while (lex->tok_kind != MP_TOKEN_END) {
            result.token_count++;

            // Detect start of while loop
            if (lex->tok_kind == MP_TOKEN_KW_WHILE) {
                in_while_loop = true;
                while_is_true = false;
                while_is_numeric = false;
                while_loop_line = lex->tok_line;
                while_loop_column = lex->tok_column;
            }

            // Check if while loop condition is True
            if (in_while_loop && prev_token == MP_TOKEN_KW_WHILE && lex->tok_kind == MP_TOKEN_KW_TRUE) {
                while_is_true = true;
            }

            // Check if while loop condition is numeric (1)
            if (in_while_loop && prev_token == MP_TOKEN_KW_WHILE && lex->tok_kind == MP_TOKEN_INTEGER) {
                while_is_numeric = true;
            }

            // When we hit the colon ending a while loop, add it
            if (in_while_loop && lex->tok_kind == MP_TOKEN_DEL_COLON) {
                if (while_is_true) {
                    add_loop(&result, LOOP_WHILE_TRUE, while_loop_line, while_loop_column);
                } else if (while_is_numeric) {
                    add_loop(&result, LOOP_WHILE_NUMERIC, while_loop_line, while_loop_column);
                } else {
                    add_loop(&result, LOOP_WHILE_GENERAL, while_loop_line, while_loop_column);
                }
                in_while_loop = false;
            }

            // Detect start of for loop: "for" keyword
            if (lex->tok_kind == MP_TOKEN_KW_FOR) {
                in_for_loop = true;
                for_loop_line = lex->tok_line;
                for_loop_column = lex->tok_column;
            }

            // When we hit the colon ending a for loop, add it
            if (in_for_loop && lex->tok_kind == MP_TOKEN_DEL_COLON) {
                // For now, treat all for loops as general
                // TODO: Detect range() specifically to mark as LOOP_FOR_RANGE
                add_loop(&result, LOOP_FOR_GENERAL, for_loop_line, for_loop_column);
                in_for_loop = false;
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
            prev_token = lex->tok_kind;

            // Advance to next token
            mp_lexer_to_next(lex);
        }

        // Free lexer
        mp_lexer_free(lex);
        nlr_pop();

    } else {
        // Exception during lexing - return result with zero values
        // (already reset at function start)
    }

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

    bool valid = true;

    // Protect lexer operations with exception handling
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_,  // Use predefined qstr
            code,
            len,
            0
        );

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
        nlr_pop();

    } else {
        // Exception during lexing - consider invalid syntax
        valid = false;
    }

    external_call_depth_dec();
    return valid;
}

// Registry to prevent linker from stripping exported functions
// This creates references that prevent dead code elimination
__attribute__((used))
static void* code_analysis_exports[] = {
    (void*)analyze_code_structure,
    (void*)is_valid_python_syntax,
    (void*)extract_loop_body,
    NULL
};

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
