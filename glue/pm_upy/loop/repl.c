/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/loop/repl.h"
#include "pm_common.h"
#include "py/mpprint.h"
#include "shared/runtime/pyexec.h"

#include <string.h>

#ifndef MICROPY_HELPER_REPL
#define MICROPY_HELPER_REPL 0
#endif

#if MICROPY_HELPER_REPL
/* Avoid pulling py/repl.h (sys mutable PS1/PS2) into this TU. */
bool mp_repl_continue_with_input(const char *input);
size_t mp_repl_autocomplete(const char *str, size_t len, const mp_print_t *print, const char **compl_str);
#endif

int pm_upy_repl_start(void) {
#if MICROPY_HELPER_REPL
    pyexec_event_repl_init();
    return PM_OK;
#else
    return PM_ERR_FEATURE;
#endif
}

void pm_upy_repl_stop(void) {
    /* Event REPL has no explicit stop; idle when inactive. */
}

int pm_upy_repl_active(void) {
#if MICROPY_HELPER_REPL
    return pyexec_repl_active ? 1 : 0;
#else
    return 0;
#endif
}

int pm_upy_repl_feed_line(const char *line, size_t len) {
#if MICROPY_HELPER_REPL
    if (!line) {
        return PM_ERR_ARG;
    }
    for (size_t i = 0; i < len; i++) {
        int r = pyexec_event_repl_process_char((int)(unsigned char)line[i]);
        if (r & PYEXEC_FORCED_EXIT) {
            return r;
        }
    }
    if (len == 0 || line[len - 1] != '\n') {
        int r = pyexec_event_repl_process_char('\n');
        if (r & PYEXEC_FORCED_EXIT) {
            return r;
        }
    }
    return PM_OK;
#else
    (void)line;
    (void)len;
    return PM_ERR_FEATURE;
#endif
}

const char *pm_upy_repl_prompt(void) {
    return pyexec_mode_kind == PYEXEC_MODE_RAW_REPL ? "" : ">>> ";
}

int pm_upy_repl_continue(const char *src) {
#if MICROPY_HELPER_REPL
    if (!src) {
        return PM_ERR_ARG;
    }
    return mp_repl_continue_with_input(src) ? 1 : 0;
#else
    (void)src;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_repl_autocomplete(const char *src, char *out, size_t out_len) {
#if MICROPY_HELPER_REPL
    if (!src) {
        return PM_ERR_ARG;
    }
    const char *compl = NULL;
    size_t n = mp_repl_autocomplete(src, strlen(src), &mp_plat_print, &compl);
    if (out && out_len) {
        out[0] = '\0';
        if (compl && n > 0) {
            size_t copy = n < out_len - 1 ? n : out_len - 1;
            memcpy(out, compl, copy);
            out[copy] = '\0';
        }
    }
    return (int)n;
#else
    (void)src;
    if (out && out_len) {
        out[0] = '\0';
    }
    return PM_ERR_FEATURE;
#endif
}

const char *pm_upy_repl_banner(void) {
    return "MicroPython";
}
