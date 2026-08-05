/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include <string.h>

#include "pm_upy/exec/run.h"
#include "pm_common.h"
#include "py/compile.h"
#include "py/lexer.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"

#ifndef MICROPY_ENABLE_COMPILER
#define MICROPY_ENABLE_COMPILER 0
#endif

int pm_upy_run_str(const char *src) {
    if (!src) {
        return PM_ERR_ARG;
    }
    vstr_t vstr;
    vstr_init(&vstr, strlen(src) + 1);
    vstr_add_str(&vstr, src);
    int r = pyexec_vstr(&vstr, true);
    vstr_clear(&vstr);
    return (r == PYEXEC_NORMAL_EXIT) ? PM_OK : PM_ERR;
}

int pm_upy_run_script(const char *path) {
    if (!path) {
        return PM_ERR_ARG;
    }
    int r = pyexec_file(path);
    return (r == PYEXEC_NORMAL_EXIT) ? PM_OK : PM_ERR;
}

int pm_upy_parse_compile_execute(const char *src) {
#if MICROPY_ENABLE_COMPILER
    if (!src) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        size_t len = strlen(src);
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, len, 0);
        mp_parse_compile_execute(lex, MP_PARSE_FILE_INPUT, mp_globals_get(), mp_locals_get());
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)src;
    return PM_ERR_FEATURE;
#endif
}
