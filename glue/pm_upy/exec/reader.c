/*
 * Lexer reader constructors (heap-allocated mp_reader_t).
 */

#include "pm_upy/exec/reader.h"
#include "py/reader.h"
#include "py/runtime.h"

#include <stdlib.h>
#include <string.h>

void *pm_upy_reader_new_mem(const uint8_t *data, size_t len) {
    mp_reader_t *reader = (mp_reader_t *)malloc(sizeof(mp_reader_t));
    if (!reader) {
        return NULL;
    }
    mp_reader_new_mem(reader, data, len, 0);
    return reader;
}

void *pm_upy_reader_new_file(const char *path) {
    if (!path) {
        return NULL;
    }
    mp_reader_t *reader = (mp_reader_t *)malloc(sizeof(mp_reader_t));
    if (!reader) {
        return NULL;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_reader_new_file(reader, qstr_from_str(path));
        nlr_pop();
        return reader;
    }
    free(reader);
    return NULL;
}
