/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/module.h"
#include "pm_common.h"
#include "py/builtin.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"

#include <string.h>

#define PM_UPY_MOD_MAX 32
typedef struct {
    char name[96];
    void *face;
} pm_upy_mod_ent_t;
static pm_upy_mod_ent_t mods[PM_UPY_MOD_MAX];
static size_t nmods;

/* Dotted names need a non-empty fromlist so µPy returns the leaf module. */
static mp_obj_t pm_upy_import_leaf(const char *name) {
    mp_obj_t fromlist = strchr(name, '.') != NULL ? mp_const_true : mp_const_none;
    return mp_import_name(qstr_from_str(name), fromlist, MP_OBJ_NEW_SMALL_INT(0));
}

pm_upy_obj_t pm_upy_import_name(const char *name) {
    if (!name || !name[0]) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = pm_upy_import_leaf(name);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)mod;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
}

int pm_upy_bind_reg(const char *full_module, const char *func, void *fn_ptr) {
    if (!full_module || !func || !fn_ptr) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = pm_upy_import_leaf(full_module);
        /* Store the C pointer as an int attribute; call via host/ffi as needed. */
        mp_store_attr(mod, qstr_from_str(func), mp_obj_new_int_from_uint((mp_uint_t)(uintptr_t)fn_ptr));
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
}

int pm_upy_module_install_face(const char *full_name, void *face) {
    if (!full_name || !full_name[0] || nmods >= PM_UPY_MOD_MAX) {
        return PM_ERR_ARG;
    }
    for (size_t i = 0; i < nmods; i++) {
        if (strcmp(mods[i].name, full_name) == 0) {
            mods[i].face = face;
            return PM_OK;
        }
    }
    strncpy(mods[nmods].name, full_name, sizeof(mods[nmods].name) - 1);
    mods[nmods].face = face;
    nmods++;
    return PM_OK;
}

bool pm_upy_module_has(const char *full_name) {
    if (!full_name) {
        return false;
    }
    for (size_t i = 0; i < nmods; i++) {
        if (strcmp(mods[i].name, full_name) == 0) {
            return true;
        }
    }
    return false;
}

pm_upy_obj_t pm_upy_module_get_builtin(const char *name) {
    if (!name) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    mp_obj_t mod = mp_module_get_builtin(qstr_from_str(name), false);
    return (pm_upy_obj_t)(uintptr_t)(mod == MP_OBJ_NULL ? mp_const_none : mod);
}

pm_upy_obj_t pm_upy_obj_new_module(const char *name) {
    if (!name) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_module(qstr_from_str(name));
}

int pm_upy_bind(const char *mod, const char *name, void *fn) {
    return pm_upy_bind_reg(mod, name, fn);
}

pm_upy_obj_t pm_upy_bind_resolve_module(const char *name) {
    return pm_upy_import_name(name);
}

pm_upy_obj_t pm_upy_import_from(const char *mod, const char *name) {
    if (!mod || !name) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t m = pm_upy_import_leaf(mod);
        mp_obj_t attr = mp_import_from(m, qstr_from_str(name));
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)attr;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
}

int pm_upy_import_all(const char *mod) {
    if (!mod) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t m = pm_upy_import_leaf(mod);
        mp_import_all(m);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
}
