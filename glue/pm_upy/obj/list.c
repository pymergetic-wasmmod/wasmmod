/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/list.h"
#include "py/objlist.h"

pm_upy_obj_t pm_upy_obj_new_list(size_t n) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_list(n, NULL);
}

int pm_upy_list_append(pm_upy_obj_t list, pm_upy_obj_t item) {
    mp_obj_list_append((mp_obj_t)(uintptr_t)list, (mp_obj_t)(uintptr_t)item);
    return 0;
}
