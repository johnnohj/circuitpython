/*
 * dev_repl.h — /dev/repl ring buffer device
 *
 * Bidirectional ring buffer for REPL I/O, backed by an OPFS file.
 * Analogous to USB serial on a real CircuitPython board:
 *   - fd 0/1 (WASI stdin/stdout) = UART serial (debug/programming)
 *   - /dev/repl = USB serial (user-facing REPL, xterm.js in browser)
 *
 * Layout (8KB file):
 *   Offset 0:    Header (32 bytes)
 *   Offset 32:   stdin ring  (REPL_STDIN_SIZE bytes) — JS writes, Python reads
 *   Offset 32+S: stdout ring (REPL_STDOUT_SIZE bytes) — Python writes, JS reads
 *
 * The executor flushes stdout to the ring in port_background_task().
 * The JS HAL reads stdout from the ring and writes stdin to it.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define REPL_STDIN_SIZE  2048
#define REPL_STDOUT_SIZE 4096
#define REPL_HEADER_SIZE 32
#define REPL_FILE_SIZE   (REPL_HEADER_SIZE + REPL_STDIN_SIZE + REPL_STDOUT_SIZE)

typedef struct {
    uint32_t stdin_write;     // JS writes here (head)
    uint32_t stdin_read;      // Python reads from here (tail)
    uint32_t stdout_write;    // Python writes here (head)
    uint32_t stdout_read;     // JS reads from here (tail)
    uint16_t flags;           // bit 0: ctrl-C pending
    uint16_t _pad;
    uint32_t _reserved[2];
} dev_repl_header_t;

#define REPL_FLAG_CTRLC (1 << 0)

// Initialize the /dev/repl file (create if needed, zero header)
int dev_repl_init(const char *path);

// Write bytes to the stdout ring (called by mp_hal_stdout_tx_strn)
// Returns number of bytes written (may be less than len if ring full)
size_t dev_repl_stdout_write(const char *data, size_t len);

// Read bytes from the stdin ring (called by mp_hal_stdin_rx_chr)
// Returns number of bytes read (0 if ring empty)
size_t dev_repl_stdin_read(char *buf, size_t len);

// Flush: sync the in-memory ring buffer state to the file
void dev_repl_flush(void);

// Check for Ctrl-C in the stdin ring
int dev_repl_check_interrupt(void);
