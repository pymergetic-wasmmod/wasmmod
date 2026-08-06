/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/module.h"
#include "pm_upy/obj/module.h"
#include <string.h>

static char installed_name[128];
static int installed;

bool pm_wasmmod_module_install(const char *full_name) {
    if (!full_name || !full_name[0]) {
        full_name = "pymergetic.wasmmod";
    }
    if (pm_upy_module_install_face(full_name, (void *)1) != 0) {
        return false;
    }
    strncpy(installed_name, full_name, sizeof(installed_name) - 1);
    installed_name[sizeof(installed_name) - 1] = 0;
    installed = 1;
    return true;
}
bool pm_wasmmod_module_installed(void) { return installed != 0; }
const char *pm_wasmmod_module_name(void) { return installed ? installed_name : NULL; }

