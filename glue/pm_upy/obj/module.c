/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_upy/obj/module.h"
#include "pm_common.h"
#include <string.h>

#define PM_UPY_MOD_MAX 32
typedef struct { char name[96]; void *face; } pm_upy_mod_ent_t;
static pm_upy_mod_ent_t mods[PM_UPY_MOD_MAX];
static size_t nmods;

pm_upy_obj_t pm_upy_import_name(const char *name) {
    (void)name;
    return 0; /* full import via mp_import_name in later wave */
}
int pm_upy_bind_reg(const char *full_module, const char *func, void *fn_ptr) {
    (void)full_module; (void)func; (void)fn_ptr;
    return PM_ERR_FEATURE; /* Metal reg bridge later */
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
    if (!full_name) return false;
    for (size_t i = 0; i < nmods; i++) {
        if (strcmp(mods[i].name, full_name) == 0) return true;
    }
    return false;
}

