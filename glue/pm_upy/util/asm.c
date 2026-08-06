/*
 * Emit a 16-bit opcode word into an mp_asm_base_t stream.
 */

#include "pm_upy/util/asm.h"
#include "pm_common.h"
#include "py/mpconfig.h"

#ifndef MICROPY_EMIT_NATIVE
#define MICROPY_EMIT_NATIVE 0
#endif

#if MICROPY_EMIT_NATIVE
#include "py/asmbase.h"
#endif

int pm_upy_asm_emit(void *as, int op) {
#if MICROPY_EMIT_NATIVE
    if (!as) {
        return PM_ERR_ARG;
    }
    uint8_t *p = mp_asm_base_get_cur_to_write_bytes(as, 2);
    if (!p) {
        return PM_ERR;
    }
    p[0] = (uint8_t)(op & 0xff);
    p[1] = (uint8_t)((op >> 8) & 0xff);
    return PM_OK;
#else
    (void)as;
    (void)op;
    return PM_ERR_FEATURE;
#endif
}
