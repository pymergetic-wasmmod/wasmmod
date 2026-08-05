/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include <string.h>

#include "pm_upy/exec/pyexec.h"
#include "pm_common.h"
#include "shared/runtime/pyexec.h"

int pm_upy_pyexec_file(const char *path) {
    if (!path) {
        return PM_ERR_ARG;
    }
    int r = pyexec_file(path);
    return (r == PYEXEC_NORMAL_EXIT) ? PM_OK : PM_ERR;
}

int pm_upy_pyexec_vstr(const char *src, size_t len) {
    if (!src) {
        return PM_ERR_ARG;
    }
    vstr_t vstr;
    vstr_init(&vstr, len + 1);
    vstr_add_strn(&vstr, src, len);
    int r = pyexec_vstr(&vstr, true);
    vstr_clear(&vstr);
    return (r == PYEXEC_NORMAL_EXIT) ? PM_OK : PM_ERR;
}
