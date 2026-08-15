/*
 * Builtin pymergetic.util package.
 * __path__ → MICROPY_WASMMOD_HOST_SRC/pymergetic/util for FS leaves (pysample, …).
 */

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/objmodule.h"

#include "ports/micropython/hostready.h"
#include "ports/micropython/modgen.h"
#include "ports/micropython/modutil.h"
#include "ports/micropython/mpconfig_wasm.h"

#ifndef MICROPY_WASMMOD_HOST_SRC
#define MICROPY_WASMMOD_HOST_SRC ""
#endif

MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_util_override_dict);

#if MICROPY_MODULE_ATTR_DELEGATION
void mp_module_pymergetic_util_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    (void)self_in;
    if (dest[0] == MP_OBJ_NULL) {
        /* load */
        if (attr == MP_QSTR___path__) {
            static const char path[] = MICROPY_WASMMOD_HOST_SRC "/pymergetic/util";
            dest[0] = mp_obj_new_str(path, sizeof(path) - 1);
            return;
        }
        if (MP_STATE_VM(mp_wasm_util_override_dict) != MP_OBJ_NULL) {
            mp_map_elem_t *el = mp_map_lookup(
                &((mp_obj_dict_t *)MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_util_override_dict)))->map,
                MP_OBJ_NEW_QSTR(attr), MP_MAP_LOOKUP);
            if (el != NULL && el->value != MP_OBJ_NULL) {
                dest[0] = el->value;
            }
        }
        return;
    }

    /* store / delete onto override dict (fixed ROM globals cannot grow). */
    if (MP_STATE_VM(mp_wasm_util_override_dict) == MP_OBJ_NULL) {
        MP_STATE_VM(mp_wasm_util_override_dict) = mp_obj_new_dict(4);
    }
    mp_obj_t d = MP_STATE_VM(mp_wasm_util_override_dict);
    if (dest[1] == MP_OBJ_NULL) {
        mp_obj_dict_delete(d, MP_OBJ_NEW_QSTR(attr));
    } else {
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(attr), dest[1]);
        /* First import may install the hook mid-flight; ready on attach too. */
        if (mp_obj_is_type(dest[1], &mp_type_module)) {
            vstr_t fqn;
            vstr_init(&fqn, 24);
            vstr_add_str(&fqn, "pymergetic.util.");
            vstr_add_str(&fqn, qstr_str(attr));
            (void)mp_wasm_host_ready(vstr_null_terminated_str(&fqn), dest[1]);
            vstr_clear(&fqn);
        }
    }
    dest[0] = MP_OBJ_NULL; /* success */
}
#endif

static const mp_rom_map_elem_t mp_module_pymergetic_util_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_util) },
#if MICROPY_PY_WASM_GEN
    { MP_ROM_QSTR(MP_QSTR_gen), MP_ROM_PTR(&mp_module_pymergetic_util_gen) },
#endif
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_util_globals, mp_module_pymergetic_util_globals_table);

const mp_obj_module_t mp_module_pymergetic_util = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_util_globals,
};

#if MICROPY_MODULE_ATTR_DELEGATION
MP_REGISTER_MODULE_DELEGATION(mp_module_pymergetic_util, mp_module_pymergetic_util_attr);
#endif

MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_util, mp_module_pymergetic_util);
