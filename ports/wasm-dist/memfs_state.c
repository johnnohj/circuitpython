/*
 * memfs_state.c — Python execution state serialization to Emscripten MEMFS
 *
 * See memfs_state.h for documentation and result.json schema.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* memmem is POSIX.1-2008 / GNU extension — provide a portable fallback */
static const void *_memmem(const void *haystack, size_t hlen,
                            const void *needle, size_t nlen) {
    if (nlen == 0) return haystack;
    if (nlen > hlen) return NULL;
    const char *h = (const char *)haystack;
    const char *n = (const char *)needle;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (memcmp(h + i, n, nlen) == 0) return h + i;
    }
    return NULL;
}

#include "memfs_state.h"
#include "vdev.h"
#include "library.h"
#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/objlist.h"
#include "py/objint.h"
#include "py/mpprint.h"
#include "py/qstr.h"
#include "py/gc.h"
#include "py/mpstate.h"

/* ---- JSON serialization ---- */

/* Escape a raw string for JSON (writes to vstr with surrounding quotes). */
static void json_escape_str(const char *s, size_t len, vstr_t *out) {
    vstr_add_byte(out, '"');
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            vstr_add_strn(out, "\\\"", 2);
        } else if (c == '\\') {
            vstr_add_strn(out, "\\\\", 2);
        } else if (c == '\n') {
            vstr_add_strn(out, "\\n", 2);
        } else if (c == '\r') {
            vstr_add_strn(out, "\\r", 2);
        } else if (c == '\t') {
            vstr_add_strn(out, "\\t", 2);
        } else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            vstr_add_str(out, esc);
        } else {
            vstr_add_byte(out, c);
        }
    }
    vstr_add_byte(out, '"');
}

void mp_obj_to_json_str(mp_obj_t obj, vstr_t *out, int max_depth) {
    if (obj == mp_const_none) {
        vstr_add_str(out, "null");
    } else if (obj == mp_const_true) {
        vstr_add_str(out, "true");
    } else if (obj == mp_const_false) {
        vstr_add_str(out, "false");
    } else if (mp_obj_is_small_int(obj)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", (int)MP_OBJ_SMALL_INT_VALUE(obj));
        vstr_add_str(out, buf);
    } else if (mp_obj_is_int(obj)) {
        /* Use repr for large ints */
        vstr_t tmp;
        vstr_init(&tmp, 32);
        mp_print_t pr = { &tmp, (mp_print_strn_t)vstr_add_strn };
        mp_obj_print_helper(&pr, obj, PRINT_REPR);
        vstr_add_strn(out, tmp.buf, tmp.len);
        vstr_clear(&tmp);
    } else if (mp_obj_is_float(obj)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.17g", (double)mp_obj_get_float(obj));
        vstr_add_str(out, buf);
    } else if (mp_obj_is_str(obj)) {
        size_t len;
        const char *s = mp_obj_str_get_data(obj, &len);
        json_escape_str(s, len, out);
    } else if (mp_obj_is_type(obj, &mp_type_bytes)) {
        /* bytes → base64-like sentinel */
        size_t len;
        const byte *data = (const byte *)mp_obj_str_get_data(obj, &len);
        vstr_add_str(out, "{\"__type__\":\"bytes\",\"__hex__\":\"");
        for (size_t i = 0; i < len && i < 256; i++) {
            char h[3];
            snprintf(h, sizeof(h), "%02x", data[i]);
            vstr_add_strn(out, h, 2);
        }
        if (len > 256) {
            vstr_add_str(out, "...");
        }
        vstr_add_str(out, "\"}");
    } else if ((mp_obj_is_type(obj, &mp_type_list) ||
                mp_obj_is_type(obj, &mp_type_tuple)) && max_depth > 0) {
        size_t len;
        mp_obj_t *items;
        mp_obj_get_array(obj, &len, &items);
        vstr_add_byte(out, '[');
        for (size_t i = 0; i < len; i++) {
            if (i > 0) {
                vstr_add_byte(out, ',');
            }
            mp_obj_to_json_str(items[i], out, max_depth - 1);
        }
        vstr_add_byte(out, ']');
    } else if (mp_obj_is_type(obj, &mp_type_dict) && max_depth > 0) {
        mp_obj_dict_t *d = MP_OBJ_TO_PTR(obj);
        vstr_add_byte(out, '{');
        bool first = true;
        for (size_t i = 0; i < d->map.alloc; i++) {
            if (mp_map_slot_is_filled(&d->map, i)) {
                mp_obj_t key = d->map.table[i].key;
                mp_obj_t val = d->map.table[i].value;
                /* Only string keys are valid JSON object keys */
                if (!mp_obj_is_str(key)) {
                    continue;
                }
                if (!first) {
                    vstr_add_byte(out, ',');
                }
                first = false;
                size_t klen;
                const char *ks = mp_obj_str_get_data(key, &klen);
                json_escape_str(ks, klen, out);
                vstr_add_byte(out, ':');
                mp_obj_to_json_str(val, out, max_depth - 1);
            }
        }
        vstr_add_byte(out, '}');
    } else {
        /* Unknown type — emit type name + repr as a sentinel object */
        vstr_t repr;
        vstr_init(&repr, 32);
        mp_print_t pr = { &repr, (mp_print_strn_t)vstr_add_strn };
        mp_obj_print_helper(&pr, obj, PRINT_REPR);

        const mp_obj_type_t *t = mp_obj_get_type(obj);
        const char *tname = qstr_str(t->name);

        vstr_add_str(out, "{\"__type__\":");
        json_escape_str(tname, strlen(tname), out);
        vstr_add_str(out, ",\"__repr__\":");
        json_escape_str(repr.buf, repr.len, out);
        vstr_add_byte(out, '}');
        vstr_clear(&repr);
    }
}

/* ---- Globals serialization ---- */

/* Write a globals dict to a MEMFS file as a JSON object.
 * Skips built-in names (starting with '__') and non-JSON-able module refs. */
static void write_globals_to_file(mp_obj_dict_t *globals, const char *path) {
    if (!globals) {
        return;
    }
    vstr_t buf;
    vstr_init(&buf, 512);
    vstr_add_byte(&buf, '{');
    bool first = true;
    for (size_t i = 0; i < globals->map.alloc; i++) {
        if (!mp_map_slot_is_filled(&globals->map, i)) {
            continue;
        }
        mp_obj_t key = globals->map.table[i].key;
        mp_obj_t val = globals->map.table[i].value;
        if (!mp_obj_is_str(key)) {
            continue;
        }
        size_t klen;
        const char *ks = mp_obj_str_get_data(key, &klen);
        /* Skip dunder names and module objects */
        if (klen >= 2 && ks[0] == '_' && ks[1] == '_') {
            continue;
        }
        if (mp_obj_is_type(val, &mp_type_module)) {
            continue;
        }
        if (mp_obj_is_type(val, &mp_type_type)) {
            continue;
        }
        if (!first) {
            vstr_add_byte(&buf, ',');
        }
        first = false;
        json_escape_str(ks, klen, &buf);
        vstr_add_byte(&buf, ':');
        mp_obj_to_json_str(val, &buf, 8);
    }
    vstr_add_byte(&buf, '}');

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, buf.buf, buf.len);
        close(fd);
    }
    vstr_clear(&buf);
}

/* ---- snapshot ---- */

/* Static storage for the snapshot content */
#define SNAP_MAX (128 * 1024)
static char _snap_buf[SNAP_MAX];
static size_t _snap_len = 0;

void mp_memfs_snapshot_globals(void) {
    mp_obj_dict_t *globals = mp_globals_get();
    write_globals_to_file(globals, "/state/snapshot.json");

    /* Also cache a copy in memory for the diff step */
    int fd = open("/state/snapshot.json", O_RDONLY, 0);
    _snap_len = 0;
    if (fd >= 0) {
        ssize_t n = read(fd, _snap_buf, SNAP_MAX - 1);
        close(fd);
        if (n > 0) {
            _snap_len = (size_t)n;
            _snap_buf[n] = '\0';
        }
    }
}

/* ---- result.json writer ---- */

/* Read stderr from the JS-side capture buffer (/dev/py_stderr device). */
static void read_stderr(vstr_t *out) {
    #define STDERR_MAX (16 * 1024)
    static char _stderr_tmp[STDERR_MAX];
    size_t n = mp_js_stderr_read(_stderr_tmp, STDERR_MAX);
    if (n > 0) {
        vstr_add_strn(out, _stderr_tmp, n);
    }
}

void mp_memfs_finish_run(bool aborted, uint32_t start_ms) {
    uint32_t now = (uint32_t)mp_js_ticks_ms();
    uint32_t duration_ms = now - start_ms;

    /* Capture stdout via vdev snapshot */
    const char *stdout_str = vdev_stdout_snapshot();
    size_t stdout_len = vdev_stdout_snapshot_len();

    /* Capture stderr */
    vstr_t stderr_buf;
    vstr_init(&stderr_buf, 64);
    read_stderr(&stderr_buf);

    /* Get current globals and compute delta against snapshot */
    mp_obj_dict_t *globals = mp_globals_get();

    vstr_t delta;
    vstr_init(&delta, 256);
    vstr_add_byte(&delta, '{');
    bool first = true;

    if (globals) {
        for (size_t i = 0; i < globals->map.alloc; i++) {
            if (!mp_map_slot_is_filled(&globals->map, i)) {
                continue;
            }
            mp_obj_t key = globals->map.table[i].key;
            mp_obj_t val = globals->map.table[i].value;
            if (!mp_obj_is_str(key)) {
                continue;
            }
            size_t klen;
            const char *ks = mp_obj_str_get_data(key, &klen);
            if (klen >= 2 && ks[0] == '_' && ks[1] == '_') {
                continue;
            }
            if (mp_obj_is_type(val, &mp_type_module) ||
                mp_obj_is_type(val, &mp_type_type)) {
                continue;
            }

            /* Serialize current value */
            vstr_t cur_val;
            vstr_init(&cur_val, 32);
            mp_obj_to_json_str(val, &cur_val, 8);

            /* Check if this key+value existed in snapshot (simple string search).
             * This is a heuristic — a proper parser would be more robust. */
            bool changed = true;
            if (_snap_len > 2) {
                /* Look for "key":cur_val pattern in snapshot */
                vstr_t pattern;
                vstr_init(&pattern, klen + cur_val.len + 4);
                json_escape_str(ks, klen, &pattern);
                vstr_add_byte(&pattern, ':');
                vstr_add_strn(&pattern, cur_val.buf, cur_val.len);
                if (_memmem(_snap_buf, _snap_len, pattern.buf, pattern.len) != NULL) {
                    changed = false;
                }
                vstr_clear(&pattern);
            }

            if (changed) {
                if (!first) {
                    vstr_add_byte(&delta, ',');
                }
                first = false;
                json_escape_str(ks, klen, &delta);
                vstr_add_byte(&delta, ':');
                vstr_add_strn(&delta, cur_val.buf, cur_val.len);
            }
            vstr_clear(&cur_val);
        }
    }
    vstr_add_byte(&delta, '}');

    /* Build result.json */
    vstr_t result;
    vstr_init(&result, 256 + delta.len + stdout_len + stderr_buf.len);

    vstr_add_str(&result, "{\"delta\":");
    vstr_add_strn(&result, delta.buf, delta.len);
    vstr_add_str(&result, ",\"stdout\":");
    json_escape_str(stdout_str, stdout_len, &result);
    vstr_add_str(&result, ",\"stderr\":");
    json_escape_str(stderr_buf.buf, stderr_buf.len, &result);
    vstr_add_str(&result, ",\"aborted\":");
    vstr_add_str(&result, aborted ? "true" : "false");
    char dur[32];
    snprintf(dur, sizeof(dur), ",\"duration_ms\":%u", (unsigned)duration_ms);
    vstr_add_str(&result, dur);
    vstr_add_str(&result, ",\"frames\":[]}");

    int fd = open("/state/result.json", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, result.buf, result.len);
        close(fd);
    }

    vstr_clear(&delta);
    vstr_clear(&stderr_buf);
    vstr_clear(&result);
}

/* ---- trace append ---- */

/* ---- binary VM checkpoint / restore ---- */

/*
 * Write the GC heap and mp_state_ctx to /mem/heap and /mem/mp_state_ctx.
 * Also writes /mem/state.json with sizes for sanity-checking on restore.
 *
 * Both the heap and mp_state_ctx must be saved together: mp_state_ctx
 * holds internal pointers into the heap (e.g. dict_main.map.table), and
 * the heap ATB tracks allocation state.  Restoring only one side leaves
 * stale pointers.  Because WASM linear memory is fixed-layout, all
 * addresses are stable across checkpoint/restore within the same instance.
 */
void mp_memfs_checkpoint_vm(void) {
    /* Save from gc_alloc_table_start (heap base) to gc_pool_end.
     * This includes both the ATB (allocation table) and the block data.
     * The ATB lives BEFORE gc_pool_start and must be saved: after a dict
     * rehash, the old table block is marked free in the ATB.  If we only
     * save gc_pool_start..gc_pool_end, gc_alloc() will reuse freed blocks
     * (overwriting their data) before the dict lookup reaches them. */
    byte *save_start = MP_STATE_MEM(area.gc_alloc_table_start);
    byte *save_end   = MP_STATE_MEM(area.gc_pool_end);
    size_t save_size = (size_t)(save_end - save_start);

    int fd = open("/mem/heap", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, save_start, save_size);
        close(fd);
    }

    /* mp_state_ctx — holds dict_main, qstr tables, and all other VM roots.
     * Must be saved alongside the heap so pointer relationships stay intact. */
    fd = open("/mem/mp_state_ctx", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, &mp_state_ctx, sizeof(mp_state_ctx));
        close(fd);
    }

    /* Metadata */
    char meta[128];
    snprintf(meta, sizeof(meta),
        "{\"heap_size\":%zu,\"state_ctx_size\":%zu}\n",
        save_size, sizeof(mp_state_ctx));
    fd = open("/mem/state.json", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, meta, strlen(meta));
        close(fd);
    }
}

/*
 * Restore the GC heap (ATB + blocks) and mp_state_ctx from /mem/.
 * The WASM module must not have been restarted — all pointers in the
 * restored mp_state_ctx still refer to valid positions in WASM linear memory.
 */
void mp_memfs_restore_vm(void) {
    /* Restore heap (ATB + block data) */
    byte *save_start = MP_STATE_MEM(area.gc_alloc_table_start);
    byte *save_end   = MP_STATE_MEM(area.gc_pool_end);
    size_t save_size = (size_t)(save_end - save_start);

    int fd = open("/mem/heap", O_RDONLY, 0);
    if (fd >= 0) {
        ssize_t n = read(fd, save_start, save_size);
        close(fd);
        (void)n;
    }

    /* Restore mp_state_ctx — restores dict_main, qstr tables, GC free-list
     * pointers, and everything else the VM needs to resume from checkpoint. */
    fd = open("/mem/mp_state_ctx", O_RDONLY, 0);
    if (fd >= 0) {
        ssize_t n = read(fd, &mp_state_ctx, sizeof(mp_state_ctx));
        close(fd);
        (void)n;
    }
}

void mp_memfs_trace_append(const char *json_line) {
    if (!json_line) {
        return;
    }
    int fd = open("/debug/trace.json", O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd >= 0) {
        write(fd, json_line, strlen(json_line));
        if (json_line[strlen(json_line) - 1] != '\n') {
            write(fd, "\n", 1);
        }
        close(fd);
    }
}
