/*
 * dev_repl.c — /dev/repl ring buffer device
 *
 * Backed by a file at a known path (e.g., /dev/repl or /tmp/dev/repl).
 * Uses POSIX read/write with seek for random access to the ring buffer.
 *
 * The ring buffer is cached in memory. Flush syncs the cache to the file.
 * The file is the shared interface between the executor worker and the
 * JS HAL on the main thread.
 */

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "dev_repl.h"

// In-memory cache of the ring buffer
static uint8_t _repl_buf[REPL_FILE_SIZE];
static dev_repl_header_t *_hdr = (dev_repl_header_t *)_repl_buf;
static uint8_t *_stdin_ring = _repl_buf + REPL_HEADER_SIZE;
static uint8_t *_stdout_ring = _repl_buf + REPL_HEADER_SIZE + REPL_STDIN_SIZE;
static int _repl_fd = -1;
static int _dirty = 0;

int dev_repl_init(const char *path) {
    // Try to open existing file first (preserves JS-written stdin data)
    _repl_fd = open(path, O_RDWR);
    if (_repl_fd < 0) {
        // Create new
        _repl_fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (_repl_fd < 0) {
            return -1;
        }
        // Initialize with zeros
        memset(_repl_buf, 0, REPL_FILE_SIZE);
        write(_repl_fd, _repl_buf, REPL_FILE_SIZE);
    } else {
        // Read existing content (may have stdin data from JS)
        lseek(_repl_fd, 0, SEEK_SET);
        ssize_t n = read(_repl_fd, _repl_buf, REPL_FILE_SIZE);
        if (n < REPL_FILE_SIZE) {
            // File too small — pad with zeros
            memset(_repl_buf + (n > 0 ? n : 0), 0, REPL_FILE_SIZE - (n > 0 ? n : 0));
            lseek(_repl_fd, 0, SEEK_SET);
            write(_repl_fd, _repl_buf, REPL_FILE_SIZE);
        }
    }
    return 0;
}

// ---- stdout (Python writes, JS reads) ----

size_t dev_repl_stdout_write(const char *data, size_t len) {
    size_t written = 0;
    uint32_t w = _hdr->stdout_write;
    uint32_t r = _hdr->stdout_read;

    while (written < len) {
        uint32_t next_w = (w + 1) % REPL_STDOUT_SIZE;
        if (next_w == r) {
            break;  // ring full
        }
        _stdout_ring[w] = (uint8_t)data[written];
        w = next_w;
        written++;
    }

    _hdr->stdout_write = w;
    _dirty = 1;
    return written;
}

// ---- stdin (JS writes, Python reads) ----

size_t dev_repl_stdin_read(char *buf, size_t len) {
    // Re-read the header from the file to pick up JS-written stdin data
    if (_repl_fd >= 0) {
        lseek(_repl_fd, 0, SEEK_SET);
        read(_repl_fd, _repl_buf, REPL_HEADER_SIZE + REPL_STDIN_SIZE);
    }

    size_t count = 0;
    uint32_t w = _hdr->stdin_write;
    uint32_t r = _hdr->stdin_read;

    while (count < len && r != w) {
        buf[count] = (char)_stdin_ring[r];
        r = (r + 1) % REPL_STDIN_SIZE;
        count++;
    }

    if (count > 0) {
        _hdr->stdin_read = r;
        _dirty = 1;
    }
    return count;
}

// ---- flush ----

void dev_repl_flush(void) {
    if (_dirty && _repl_fd >= 0) {
        lseek(_repl_fd, 0, SEEK_SET);
        write(_repl_fd, _repl_buf, REPL_FILE_SIZE);
        _dirty = 0;
    }
}

// ---- interrupt check ----

int dev_repl_check_interrupt(void) {
    // Re-read header to check flags
    if (_repl_fd >= 0) {
        dev_repl_header_t hdr;
        lseek(_repl_fd, 0, SEEK_SET);
        read(_repl_fd, &hdr, sizeof(hdr));
        if (hdr.flags & REPL_FLAG_CTRLC) {
            // Clear the flag
            hdr.flags &= ~REPL_FLAG_CTRLC;
            lseek(_repl_fd, 0, SEEK_SET);
            write(_repl_fd, &hdr, sizeof(hdr));
            _hdr->flags = hdr.flags;
            return 1;
        }
        // Also update stdin pointers from file
        _hdr->stdin_write = hdr.stdin_write;
    }
    return 0;
}
