/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include "py/mphal.h"
#include "py/stream.h"
#include "py/mpstate.h"
#include "library.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// For MP_STREAM_POLL_* constants
#ifndef MP_STREAM_POLL_RD
#define MP_STREAM_POLL_RD (1)
#endif


void mp_hal_stdout_tx_char(char c) {
    // Character-by-character output for REPL interaction (echoing, prompts)
#ifndef CIRCUITPY_CORE
    mp_js_write(&c, 1);
#else
    // Core interpreter: use simple printf for output
    printf("%c", c);
#endif
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    // String-based output for Python execution results (efficient for bulk output)
    // This avoids the JavaScript call overhead that was causing output loss
#ifndef CIRCUITPY_CORE
    mp_js_write(str, len);
#else
    // Core interpreter: use simple printf for output
    printf("%.*s", (int)len, str);
#endif
    return len;
}



void mp_hal_move_cursor_back(uint pos) {
    if (pos == 0) {
        return;
    } else if (pos == 1) {
        // move cursor back 1
        printf("\x1b[D");
    } else {
        // move cursor back N
        printf("\x1b[%uD", pos);
    }
}

void mp_hal_erase_line_from_cursor(uint n_chars_to_erase) {
    (void)n_chars_to_erase;
    // clear from cursor to end of line
    printf("\x1b[K");
}

void mp_hal_delay_ms(mp_uint_t ms) {
    uint32_t start = mp_hal_ticks_ms();
    while (mp_hal_ticks_ms() - start < ms) {
    }
}

// mp_hal_delay_us is defined as inline in mphalport.h

mp_uint_t mp_hal_ticks_us(void) {
#ifndef CIRCUITPY_CORE
    return mp_js_ticks_ms() * 1000;
#else
    // Core interpreter: use simple time implementation
    return 0; // Minimal stub, timing not critical for basic interpreter
#endif
}

mp_uint_t mp_hal_ticks_ms(void) {
#ifndef CIRCUITPY_CORE
    return mp_js_ticks_ms();
#else
    // Core interpreter: use simple time implementation
    return 0; // Minimal stub, timing not critical for basic interpreter
#endif
}

uint64_t mp_hal_time_ms(void) {
#ifndef CIRCUITPY_CORE
    double mm = mp_js_time_ms();
    return (uint64_t)mm;
#else
    // Core interpreter: use simple time implementation
    return 0; // Minimal stub, timing not critical for basic interpreter
#endif
}

uint64_t mp_hal_time_ns(void) {
    return mp_hal_time_ms() * 1000000ULL;
}

// Define mp_interrupt_char globally for all variants (needed by library.js)
int mp_interrupt_char = 3; // Ctrl-C

// Interrupt char functions - required by library.js exports
int mp_hal_get_interrupt_char(void) {
    return mp_interrupt_char;
}

void mp_hal_set_interrupt_char(int c) {
    mp_interrupt_char = c;
}

// Check if execution has been interrupted (needed by core CP code)
bool mp_hal_is_interrupted(void) {
    return MP_STATE_THREAD(mp_pending_exception) != MP_OBJ_FROM_PTR(NULL);
}

// JavaScript input buffer management
static char js_input_buffer[256];
static size_t js_input_read_pos = 0;
static size_t js_input_write_pos = 0;
static bool js_stdin_raw_mode = false;

// Check if there's input available in the buffer
static bool js_stdin_has_data(void) {
    return js_input_read_pos != js_input_write_pos;
}

// Get next character from buffer
static int js_stdin_get_char(void) {
    if (!js_stdin_has_data()) {
        return -1; // No data available
    }
    
    int c = js_input_buffer[js_input_read_pos];
    js_input_read_pos = (js_input_read_pos + 1) % sizeof(js_input_buffer);
    return c;
}

// JavaScript API function to add characters to input buffer
void mp_js_stdin_write_char(int c) {
    size_t next_pos = (js_input_write_pos + 1) % sizeof(js_input_buffer);
    if (next_pos != js_input_read_pos) { // Buffer not full
        js_input_buffer[js_input_write_pos] = c;
        js_input_write_pos = next_pos;
    }
    // If buffer is full, drop the character (could log warning)
}

// JavaScript API function to write string to input buffer
void mp_js_stdin_write_str(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        mp_js_stdin_write_char(str[i]);
    }
}

// Proper stdin character reading implementation
int mp_hal_stdin_rx_chr(void) {
    // Try to get character from JavaScript input buffer
    int c = js_stdin_get_char();
    
    if (c == -1) {
        // No input available - in a blocking implementation we'd wait,
        // but in WebAssembly we return 0 to avoid blocking the event loop
        return 0;
    }
    
    // Handle special characters like Unix implementation
    if (c == 0) {
        c = 4; // EOF, ctrl-D
    } else if (c == '\n') {
        c = '\r'; // Convert newline for REPL compatibility
    }
    
    return c;
}

// WebAssembly stub for stdio polling
uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    // Return whether stdin has data available
    if (poll_flags & MP_STREAM_POLL_RD) {
        return js_stdin_has_data() ? MP_STREAM_POLL_RD : 0;
    }
    return 0;
}

// REPL terminal mode control functions
// These communicate with JavaScript to control terminal behavior

void mp_hal_stdio_mode_raw(void) {
    // Signal JavaScript that we're entering raw terminal mode
    // This is crucial for REPL character-by-character input processing
    js_stdin_raw_mode = true;
    
    // In a browser/Node.js environment, this could trigger:
    // - process.stdin.setRawMode(true) in Node.js
    // - Special keyboard event handling in browsers
    // - Disable line buffering and echo
    
    // For now, we'll use an Emscripten JS function call if available
    #ifdef __EMSCRIPTEN__
    // This could call a JavaScript function to set raw mode
    // EM_ASM({ Module._stdin_set_raw_mode(true); });
    #endif
}

void mp_hal_stdio_mode_orig(void) {
    // Restore original terminal mode
    // Called when exiting REPL or on errors
    js_stdin_raw_mode = false;
    
    #ifdef __EMSCRIPTEN__
    // Restore normal terminal behavior in JavaScript
    // EM_ASM({ Module._stdin_set_raw_mode(false); });
    #endif
}

// Function to check if we're in raw mode (useful for JavaScript)
bool mp_hal_is_stdin_raw_mode(void) {
    return js_stdin_raw_mode;
}

// stdout_helpers.c provides these functions


void mp_hal_get_random(size_t n, void *buf) {
    // WebAssembly random implementation
    for (size_t i = 0; i < n; i++) {
        ((uint8_t*)buf)[i] = rand() & 0xff;
    }
}

