/*
 * `pymergetic` — the root package every card tree hangs off.
 *
 * One owner on every seat. The dict carries no children: a child is reached by
 * its own MP_REGISTER_MODULE (import) or by the attr delegation (attribute
 * access), which asks the builtin table and then the card registry. So this TU
 * never learns the name of a downstream tree, and a seat that leaves some
 * module TU out loses that module, not the package.
 */
#include "py/obj.h"
#include "ports/micropython/importhook.h"

static mp_obj_t pymergetic___init__(void) {
    mp_wasm_ensure_inited();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pymergetic___init___obj, pymergetic___init__);

static const mp_rom_map_elem_t mp_module_pymergetic_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&pymergetic___init___obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_globals, mp_module_pymergetic_globals_table);

const mp_obj_module_t mp_module_pymergetic = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_globals,
};

#if MICROPY_MODULE_ATTR_DELEGATION
MP_REGISTER_MODULE_DELEGATION(mp_module_pymergetic, mp_wasm_pymergetic_attr);
#endif
MP_REGISTER_MODULE(MP_QSTR_pymergetic, mp_module_pymergetic);
