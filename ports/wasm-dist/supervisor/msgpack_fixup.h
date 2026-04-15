/*
 * msgpack_fixup.h — Port-local fixup for shared-module/msgpack/__init__.c
 *
 * The upstream msgpack module defines `static void read(...)` and
 * `static void write(...)` which clash with POSIX read(2)/write(2)
 * from the WASI sysroot's <unistd.h>.  Clang treats the conflicting
 * signatures as a hard error.
 *
 * Fix: pre-define the <unistd.h> include guard so the POSIX
 * declarations of read/write are never seen.  The msgpack module
 * doesn't use POSIX I/O — it uses MicroPython's stream protocol —
 * so nothing is lost.
 */
#ifndef _UNISTD_H
#define _UNISTD_H
#endif
