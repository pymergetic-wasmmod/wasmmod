/*
 * Soft-autoload: import a module by name.
 */

#include "pm_upy/util/autoload.h"
#include "pm_common.h"
#include "py/runtime.h"

#include <string.h>

int pm_upy_autoload(const char *name) {
    if (!name || !name[0]) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_import_name(qstr_from_str(name), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
}
