/*
 * Parse an integer from a C string.
 */

#include "pm_upy/util/parsenum.h"
#include "pm_common.h"
#include "py/obj.h"
#include "py/parsenum.h"
#include "py/runtime.h"

#include <string.h>

int pm_upy_parse_num(const char *s, int64_t *out) {
    if (!s || !out) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t o = mp_parse_num_integer(s, strlen(s), 0, NULL);
        *out = (int64_t)mp_obj_get_ll(o);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
}
