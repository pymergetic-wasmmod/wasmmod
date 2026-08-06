/*
 * Compile source to a code object when the compiler is enabled.
 */

#include "pm_upy/exec/compile.h"
#include "pm_common.h"
#include "py/bc.h"
#include "py/compile.h"
#include "py/lexer.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_ENABLE_COMPILER
#define MICROPY_ENABLE_COMPILER 0
#endif

pm_upy_obj_t pm_upy_compile(const char *src, const char *filename, int kind) {
#if MICROPY_ENABLE_COMPILER
    if (!src) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    qstr fn = qstr_from_str(filename ? filename : "<pm_upy_compile>");
    mp_parse_input_kind_t parse_kind = MP_PARSE_FILE_INPUT;
    if (kind == 1) {
        parse_kind = MP_PARSE_SINGLE_INPUT;
    } else if (kind == 2) {
        parse_kind = MP_PARSE_EVAL_INPUT;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(fn, src, strlen(src), 0);
        mp_parse_tree_t pt = mp_parse(lex, parse_kind);
        mp_obj_t code = mp_compile(&pt, lex->source_name, false);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)code;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)src;
    (void)filename;
    (void)kind;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

int pm_upy_compile_to_raw_code(const char *src, void **raw_out) {
#if MICROPY_ENABLE_COMPILER && MICROPY_EXPOSE_MP_COMPILE_TO_RAW_CODE
    if (!src) {
        if (raw_out) {
            *raw_out = NULL;
        }
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_compiled_module_t cm;
        cm.context = m_new_obj(mp_module_context_t);
        cm.context->module.globals = mp_globals_get();
        mp_compile_to_raw_code(&pt, lex->source_name, false, &cm);
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
    (void)src;
    if (raw_out) {
        *raw_out = NULL;
    }
    return PM_ERR_FEATURE;
#endif
}
