/*
 * Persistent .mpy load/save and bytecode/mpy execute helpers.
 */

#include "pm_upy/exec/rawcode.h"
#include "pm_upy/exec/run.h"
#include "pm_common.h"
#include "py/bc.h"
#include "py/emitglue.h"
#include "py/persistentcode.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PERSISTENT_CODE_LOAD
#define MICROPY_PERSISTENT_CODE_LOAD 0
#endif
#ifndef MICROPY_PERSISTENT_CODE_SAVE
#define MICROPY_PERSISTENT_CODE_SAVE 0
#endif
#ifndef MICROPY_HAS_FILE_READER
#define MICROPY_HAS_FILE_READER 0
#endif

static int load_and_run_mpy(const uint8_t *data, size_t len) {
#if MICROPY_PERSISTENT_CODE_LOAD
    if (!data || !len) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_module_context_t *ctx = m_new_obj(mp_module_context_t);
        ctx->module.globals = mp_globals_get();
        memset(&ctx->constants, 0, sizeof(ctx->constants));
        mp_compiled_module_t cm;
        cm.context = ctx;
        mp_raw_code_load_mem(data, len, &cm);
        mp_obj_t fun = mp_make_function_from_proto_fun(cm.rc, cm.context, NULL);
        mp_call_function_0(fun);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)data;
    (void)len;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_raw_code_load_mem(const uint8_t *data, size_t len) {
    return load_and_run_mpy(data, len);
}

int pm_upy_execute_bytecode(const uint8_t *bc, size_t len) {
    /* Treat buffer as a .mpy image (persistent code), not raw VM opcodes. */
    return load_and_run_mpy(bc, len);
}

int pm_upy_raw_code_save(void) {
#if MICROPY_PERSISTENT_CODE_SAVE
    return PM_ERR_FEATURE; /* needs an explicit compiled module + printer */
#else
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_raw_code_load_file(const char *path, void **raw_out) {
#if MICROPY_PERSISTENT_CODE_LOAD && MICROPY_HAS_FILE_READER
    if (!path) {
        if (raw_out) {
            *raw_out = NULL;
        }
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_module_context_t *ctx = m_new_obj(mp_module_context_t);
        ctx->module.globals = mp_globals_get();
        memset(&ctx->constants, 0, sizeof(ctx->constants));
        mp_compiled_module_t cm;
        cm.context = ctx;
        mp_raw_code_load_file(qstr_from_str(path), &cm);
        nlr_pop();
        if (raw_out) {
            *raw_out = (void *)cm.rc;
        }
        return PM_OK;
    }
    if (raw_out) {
        *raw_out = NULL;
    }
    return PM_ERR;
#else
    (void)path;
    if (raw_out) {
        *raw_out = NULL;
    }
    return PM_ERR_FEATURE;
#endif
}
