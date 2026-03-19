/*
 * vdev.c — Virtual device driver for WASM-dist
 *
 * Creates and maintains Emscripten MEMFS virtual hardware bus.
 * See vdev.h for the full device layout and purpose.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "vdev.h"
#include "library.h"
#include "py/mphal.h"

/* File descriptors kept open for hot I/O paths */
int vdev_stdout_fd = -1;
int vdev_stderr_fd = -1;
int vdev_stdin_fd  = -1;

/* Static buffer for stdout snapshot */
#define STDOUT_SNAP_MAX (64 * 1024)
static char _stdout_snap[STDOUT_SNAP_MAX];
static size_t _stdout_snap_len = 0;

/* ---- helpers ---- */

static void mkdir_p(const char *path) {
    /* Best-effort mkdir; ignore EEXIST */
    mkdir(path, 0777);
}

static void touch(const char *path, const char *initial_content) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        if (initial_content && initial_content[0]) {
            write(fd, initial_content, strlen(initial_content));
        }
        close(fd);
    }
}

/* ---- public API ---- */

void vdev_init(void) {
    /* Create directory tree */
    mkdir_p("/dev");
    mkdir_p("/mem");
    mkdir_p("/flash");
    mkdir_p("/flash/lib");
    mkdir_p("/state");
    mkdir_p("/proc");
    mkdir_p("/debug");

    /* Virtual device files — empty initially except /dev/time.
     * NOTE: /dev/stdin, /dev/stdout, /dev/stderr are pre-defined Emscripten
     * device nodes that route to the real terminal; we use /state/stdout and
     * /state/stderr as our capture buffers instead. */
    touch("/dev/interrupt", "");
    touch("/dev/bc_out",    "");
    touch("/dev/bc_in",     "");
    touch("/state/stdout",  "");
    touch("/state/stderr",  "");

    /* /dev/time: write initial timestamp (JS will refresh via mp_js_hook) */
    vdev_update_time();

    /* /proc/self.json — workerId populated by JS before mp_js_init returns */
    touch("/proc/self.json",    "{\"workerId\":\"main\",\"role\":\"executor\"}");
    touch("/proc/workers.json", "[]");

    /* /state/ seed files so result.json always exists after first run */
    touch("/state/snapshot.json", "{}");
    touch("/state/result.json",
        "{\"delta\":{},\"stdout\":\"\",\"stderr\":\"\","
        "\"aborted\":false,\"duration_ms\":0,\"frames\":[]}");

    /* /debug/ */
    touch("/debug/trace.json",       "");
    touch("/debug/semihosting.log",  "");

    /* Open hot-path FDs for mphalport.
     * /dev/py_stdout is our custom capture device registered in library.js;
     * writes trigger the JS device callback which accumulates in Module._pyStdout. */
    vdev_stdout_fd = open("/dev/py_stdout", O_WRONLY, 0);
    vdev_stderr_fd = open("/dev/py_stderr", O_WRONLY, 0);
    vdev_stdin_fd  = open("/dev/stdin",     O_RDONLY, 0);
}

void vdev_update_time(void) {
    /* mp_js_ticks_ms() is declared in mpconfigport.h and resolved from library.js */
    int ms = mp_js_ticks_ms();
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d", ms);
    int fd = open("/dev/time", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, buf, len);
        close(fd);
    }
}

int vdev_bc_out_drain(char *buf, size_t buf_len) {
    if (buf_len == 0) {
        return 0;
    }
    int fd = open("/dev/bc_out", O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    ssize_t n = read(fd, buf, buf_len - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';

    /* Clear bc_out */
    int wfd = open("/dev/bc_out", O_WRONLY | O_TRUNC, 0);
    if (wfd >= 0) {
        close(wfd);
    }

    /* Count newlines = line count */
    int lines = 0;
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

void vdev_bc_in_write(const char *json_line) {
    if (!json_line) {
        return;
    }
    int fd = open("/dev/bc_in", O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd >= 0) {
        write(fd, json_line, strlen(json_line));
        /* Ensure newline separator */
        if (json_line[strlen(json_line) - 1] != '\n') {
            write(fd, "\n", 1);
        }
        close(fd);
    }
}

const char *vdev_stdout_snapshot(void) {
    /* Drain the JS-side _pyStdout buffer accumulated by the /dev/py_stdout device. */
    _stdout_snap_len = mp_js_stdout_read(_stdout_snap, STDOUT_SNAP_MAX);
    _stdout_snap[_stdout_snap_len] = '\0';
    return _stdout_snap;
}

size_t vdev_stdout_snapshot_len(void) {
    return _stdout_snap_len;
}
