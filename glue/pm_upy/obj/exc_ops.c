/*
 * Exception construct / print helpers.
 */

#include <string.h>

#include "pm_upy/obj/exc.h"
#include "pm_upy/obj/print.h"
#include "py/mpprint.h"
#include "py/obj.h"
#include "py/runtime.h"

void pm_upy_raise_OSError(int errno_val) {
    mp_raise_OSError(errno_val);
}

pm_upy_obj_t pm_upy_obj_new_exception(const char *type_name, const char *msg) {
    (void)type_name;
    const char *m = msg ? msg : "error";
    mp_obj_t arg = mp_obj_new_str(m, strlen(m));
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_exception_arg1(&mp_type_RuntimeError, arg);
}

void pm_upy_obj_print_exception(pm_upy_obj_t exc) {
    mp_obj_print_exception(&mp_plat_print, (mp_obj_t)(uintptr_t)exc);
}

void pm_upy_obj_print(pm_upy_obj_t o) {
    mp_obj_print_helper(&mp_plat_print, (mp_obj_t)(uintptr_t)o, PRINT_REPR);
}
