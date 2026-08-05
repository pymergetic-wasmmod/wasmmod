/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Embed helpers — full mp_embed_* only on embed ports; here exec_str uses pyexec.
 */

#include <string.h>

#include "pm_upy/exec/embed.h"
#include "pm_common.h"
#include "shared/runtime/pyexec.h"

#ifndef MICROPY_ENABLE_COMPILER
#define MICROPY_ENABLE_COMPILER 0
#endif
#ifndef MICROPY_PERSISTENT_CODE_LOAD
#define MICROPY_PERSISTENT_CODE_LOAD 0
#endif

int pm_upy_embed_init(void *heap, size_t heap_len, void *stack, size_t stack_len) {
    /* Host µPy already initialized by the port; refuse double-init. */
    (void)heap;
    (void)heap_len;
    (void)stack;
    (void)stack_len;
    return PM_ERR_FEATURE;
}

void pm_upy_embed_deinit(void) {
}

int pm_upy_embed_exec_str(const char *src) {
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

int pm_upy_embed_exec_mpy(const void *mpy, size_t len) {
#if MICROPY_PERSISTENT_CODE_LOAD
    (void)mpy;
    (void)len;
    /* Prefer dedicated rawcode API when filled; keep feature gate for now. */
    return PM_ERR_FEATURE;
#else
    (void)mpy;
    (void)len;
    return PM_ERR_FEATURE;
#endif
}
