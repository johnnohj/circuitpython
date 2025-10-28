// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Board serial implementation for WASM port
// Provides string-based serial I/O for easier JavaScript integration

#include <string.h>
#include <emscripten.h>
#include "py/ringbuf.h"
#include "supervisor/shared/serial.h"

// Ring buffer for input from JavaScript
#define SERIAL_INPUT_BUFFER_SIZE 256
static uint8_t serial_input_buffer_data[SERIAL_INPUT_BUFFER_SIZE];
static ringbuf_t serial_input_buffer;
static bool serial_initialized = false;

// JavaScript callback for output (set via EM_JS)
static void (*js_output_callback)(const char *text, uint32_t length) = NULL;

// Initialize the serial system
void board_serial_init(void) {
    if (!serial_initialized) {
        ringbuf_init(&serial_input_buffer, serial_input_buffer_data, SERIAL_INPUT_BUFFER_SIZE);
        serial_initialized = true;
    }
}

// ============================================================================
// Board serial functions (called by supervisor/shared/serial.c)
// ============================================================================

char board_serial_read(void) {
    if (!serial_initialized) {
        return -1;
    }

    if (ringbuf_num_filled(&serial_input_buffer) > 0) {
        return ringbuf_get(&serial_input_buffer);
    }
    return -1;
}

uint32_t board_serial_bytes_available(void) {
    if (!serial_initialized) {
        return 0;
    }
    return ringbuf_num_filled(&serial_input_buffer);
}

bool board_serial_connected(void) {
    // In WASM, serial is always "connected" once initialized
    // JavaScript handles the actual I/O, so we just need to indicate availability
    return serial_initialized;
}

void board_serial_write_substring(const char *text, uint32_t length) {
    // Send output to JavaScript callback if registered
    if (js_output_callback != NULL) {
        js_output_callback(text, length);
    }
}

// ============================================================================
// JavaScript-callable functions
// ============================================================================

// Write a string from JavaScript to the input buffer (for REPL input)
EMSCRIPTEN_KEEPALIVE
int board_serial_write_input(const char *text, uint32_t length) {
    if (!serial_initialized) {
        board_serial_init();
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < length; i++) {
        if (ringbuf_num_empty(&serial_input_buffer) > 0) {
            ringbuf_put(&serial_input_buffer, text[i]);
            written++;
        } else {
            // Buffer full, return partial write count
            break;
        }
    }
    return written;
}

// Write a single character from JavaScript to the input buffer
EMSCRIPTEN_KEEPALIVE
int board_serial_write_input_char(char c) {
    if (!serial_initialized) {
        board_serial_init();
    }

    if (ringbuf_num_empty(&serial_input_buffer) > 0) {
        ringbuf_put(&serial_input_buffer, c);
        return 1;
    }
    return 0;
}

// Clear the input buffer
EMSCRIPTEN_KEEPALIVE
void board_serial_clear_input(void) {
    if (!serial_initialized) {
        return;
    }
    ringbuf_clear(&serial_input_buffer);
}

// Get the number of bytes available in the input buffer
EMSCRIPTEN_KEEPALIVE
uint32_t board_serial_input_available(void) {
    if (!serial_initialized) {
        return 0;
    }
    return ringbuf_num_filled(&serial_input_buffer);
}

// ============================================================================
// JavaScript-side output callback registration
// ============================================================================

// This EM_JS function is called from JavaScript to register an output callback
EM_JS(void, register_serial_output_callback_internal, (void), {
    // This function is called from C to set up the JavaScript side
    // The actual callback is registered via Module.setSerialOutputCallback()
});

// Register output callback from JavaScript
EMSCRIPTEN_KEEPALIVE
void board_serial_set_output_callback(void (*callback)(const char *, uint32_t)) {
    js_output_callback = callback;
}

// ============================================================================
// Helper: Process REPL with string input
// ============================================================================

// Process a string through the REPL (much easier than char-by-char!)
EMSCRIPTEN_KEEPALIVE
int board_serial_repl_process_string(const char *input, uint32_t length) {
    if (!serial_initialized) {
        board_serial_init();
    }

    // Write the string to the input buffer
    for (uint32_t i = 0; i < length; i++) {
        if (ringbuf_num_empty(&serial_input_buffer) > 0) {
            ringbuf_put(&serial_input_buffer, input[i]);
        } else {
            // Buffer full - this shouldn't happen with reasonable input sizes
            return -1;
        }
    }

    // Let the REPL process from the buffer
    // The supervisor will call board_serial_read() to get the characters
    return 0;
}
