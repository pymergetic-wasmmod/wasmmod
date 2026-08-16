/* pymergetic.wasmmod.guest — µPy face of PM_MOD_BOOT_* (same names as guest.h). */
#include "py/obj.h"
#include "py/runtime.h"
#include "py/nlr.h"

#include "pymergetic/wasmmod/boot/__types__.h"

#include <string.h>

#ifndef PM_MOD_BOOT_PY_MAX
#define PM_MOD_BOOT_PY_MAX 32
#endif

MP_REGISTER_ROOT_POINTER(mp_obj_t pm_mod_boot_py_init[PM_MOD_BOOT_PY_MAX]);
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_mod_boot_py_deinit[PM_MOD_BOOT_PY_MAX]);
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_mod_boot_py_ready[PM_MOD_BOOT_PY_MAX]);

static pm_mod_boot_t s_rec[PM_MOD_BOOT_PY_MAX];
static pm_mod_bootdep_t s_dep[PM_MOD_BOOT_PY_MAX];
static char s_fqn[PM_MOD_BOOT_PY_MAX][80];
static char s_dep_fqn[PM_MOD_BOOT_PY_MAX][80];
static char s_dep_dep[PM_MOD_BOOT_PY_MAX][80];
static uint32_t s_n;
static uint32_t s_ndep;

static int32_t slot_of(const pm_mod_boot_t *rec) {
    uint32_t i;
    if (rec == NULL) {
        return -1;
    }
    for (i = 0; i < s_n; i++) {
        if (&s_rec[i] == rec) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t call_py(mp_obj_t cb, int32_t none_ok) {
    nlr_buf_t nlr;
    if (cb == MP_OBJ_NULL) {
        return none_ok;
    }
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_0(cb);
        int32_t out = (int32_t)mp_obj_get_int(res);
        nlr_pop();
        return out;
    }
    return -1;
}

static int32_t py_init(pm_util_mem_arena_t *arena) {
    int32_t i;
    (void)arena;
    i = slot_of(pm_mod_boot_current());
    if (i < 0) {
        return -1;
    }
    return call_py(MP_STATE_VM(pm_mod_boot_py_init)[i], -1);
}

static void py_deinit(void) {
    int32_t i = slot_of(pm_mod_boot_current());
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (i < 0) {
        return;
    }
    cb = MP_STATE_VM(pm_mod_boot_py_deinit)[i];
    if (cb == MP_OBJ_NULL) {
        return;
    }
    if (nlr_push(&nlr) == 0) {
        (void)mp_call_function_0(cb);
        nlr_pop();
    }
}

static int32_t py_ready(void) {
    int32_t i = slot_of(pm_mod_boot_current());
    if (i < 0) {
        return -1;
    }
    return call_py(MP_STATE_VM(pm_mod_boot_py_ready)[i], 0);
}

static void copy_fqn(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (src == NULL || cap == 0) {
        return;
    }
    while (src[n] != 0 && n + 1u < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static mp_obj_t guest_PM_MOD_BOOT(size_t n_args, const mp_obj_t *args) {
    uint32_t i;
    const char *fqn;
    fqn = mp_obj_str_get_str(args[0]);
    if (!mp_obj_is_callable(args[1]) || !mp_obj_is_callable(args[2])) {
        mp_raise_TypeError(MP_ERROR_TEXT("PM_MOD_BOOT init/deinit"));
    }
    if (s_n >= PM_MOD_BOOT_PY_MAX) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("PM_MOD_BOOT slots full"));
    }
    i = s_n++;
    copy_fqn(s_fqn[i], sizeof(s_fqn[i]), fqn);
    MP_STATE_VM(pm_mod_boot_py_init)[i] = args[1];
    MP_STATE_VM(pm_mod_boot_py_deinit)[i] = args[2];
    MP_STATE_VM(pm_mod_boot_py_ready)[i] = (n_args > 3 && args[3] != mp_const_none) ? args[3]
                                                                                    : MP_OBJ_NULL;
    memset(&s_rec[i], 0, sizeof(s_rec[i]));
    s_rec[i].fqn = s_fqn[i];
    s_rec[i].init = py_init;
    s_rec[i].deinit = py_deinit;
    s_rec[i].ready = (MP_STATE_VM(pm_mod_boot_py_ready)[i] == MP_OBJ_NULL) ? NULL : py_ready;
    if (pm_mod_boot_add(&s_rec[i]) != 0) {
        MP_STATE_VM(pm_mod_boot_py_init)[i] = MP_OBJ_NULL;
        s_n--;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("pm_mod_boot_add"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(guest_PM_MOD_BOOT_obj, 3, 4, guest_PM_MOD_BOOT);

static mp_obj_t guest_PM_MOD_BOOTDEP(mp_obj_t mod, mp_obj_t dep) {
    uint32_t i;
    if (s_ndep >= PM_MOD_BOOT_PY_MAX) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("PM_MOD_BOOTDEP slots full"));
    }
    i = s_ndep++;
    copy_fqn(s_dep_fqn[i], sizeof(s_dep_fqn[i]), mp_obj_str_get_str(mod));
    copy_fqn(s_dep_dep[i], sizeof(s_dep_dep[i]), mp_obj_str_get_str(dep));
    s_dep[i].fqn = s_dep_fqn[i];
    s_dep[i].dep = s_dep_dep[i];
    s_dep[i].flags = PM_MOD_BOOTDEP_HARD;
    if (pm_mod_bootdep_add(&s_dep[i]) != 0) {
        s_ndep--;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("pm_mod_bootdep_add"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(guest_PM_MOD_BOOTDEP_obj, guest_PM_MOD_BOOTDEP);

static mp_obj_t guest_PM_MOD_BOOT_CHILD(mp_obj_t mod, mp_obj_t child) {
    uint32_t i;
    if (s_ndep >= PM_MOD_BOOT_PY_MAX) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("PM_MOD_BOOT_CHILD slots full"));
    }
    i = s_ndep++;
    copy_fqn(s_dep_fqn[i], sizeof(s_dep_fqn[i]), mp_obj_str_get_str(child));
    copy_fqn(s_dep_dep[i], sizeof(s_dep_dep[i]), mp_obj_str_get_str(mod));
    s_dep[i].fqn = s_dep_fqn[i];
    s_dep[i].dep = s_dep_dep[i];
    s_dep[i].flags = PM_MOD_BOOTDEP_CHILD;
    if (pm_mod_bootdep_add(&s_dep[i]) != 0) {
        s_ndep--;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("pm_mod_bootdep_add"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(guest_PM_MOD_BOOT_CHILD_obj, guest_PM_MOD_BOOT_CHILD);

static const mp_rom_map_elem_t guest_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_wasmmod_dot_guest) },
    { MP_ROM_QSTR(MP_QSTR_PM_MOD_BOOT), MP_ROM_PTR(&guest_PM_MOD_BOOT_obj) },
    { MP_ROM_QSTR(MP_QSTR_PM_MOD_BOOTDEP), MP_ROM_PTR(&guest_PM_MOD_BOOTDEP_obj) },
    { MP_ROM_QSTR(MP_QSTR_PM_MOD_BOOT_CHILD), MP_ROM_PTR(&guest_PM_MOD_BOOT_CHILD_obj) },
};
static MP_DEFINE_CONST_DICT(guest_globals, guest_globals_table);

const mp_obj_module_t mp_module_pymergetic_wasmmod_guest = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&guest_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_wasmmod_dot_guest, mp_module_pymergetic_wasmmod_guest);
