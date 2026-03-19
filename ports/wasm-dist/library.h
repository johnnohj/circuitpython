/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017, 2018 Rami Ali
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

#include "py/obj.h"

extern void mp_js_write(const char *str, mp_uint_t len);
extern void mp_js_write_stderr(const char *str, mp_uint_t len);
extern int mp_js_ticks_ms(void);
extern void mp_js_hook(void);
extern double mp_js_time_ms(void);
extern uint32_t mp_js_random_u32(void);
extern void mp_js_init_filesystem(void);
/* mp_js_set_deadline / mp_js_get_deadline / mp_js_host_aborted implemented in mphalport.c */
extern void mp_js_set_deadline(uint32_t deadline_ms);
extern int  mp_js_get_deadline(void);
/* Returns 1 if the host (deadline or /dev/interrupt) requested an abort. */
extern int  mp_js_host_aborted(void);

// Hardware synchronization - called during port_background_task()
// Updates virtual hardware state from JavaScript (e.g., input pin values)
extern void mp_js_sync_hardware(void);

// Stdout/stderr capture read-back (implemented in library.js)
extern size_t mp_js_stdout_size(void);
extern size_t mp_js_stdout_read(char *ptr, size_t maxlen);
extern size_t mp_js_stderr_size(void);
extern size_t mp_js_stderr_read(char *ptr, size_t maxlen);

// From mphalport.c - needed by modtime.c
extern uint64_t mp_hal_time_ms(void);
