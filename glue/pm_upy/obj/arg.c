/*
 * mp_arg_parse_all wrapper.
 *
 * `spec` points at:
 *   struct { size_t n_allowed; const mp_arg_t *allowed; mp_arg_val_t *out_vals; }
 */

#include "pm_upy/obj/arg.h"
#include "pm_common.h"
#include "py/runtime.h"

typedef struct {
    size_t n_allowed;
    const mp_arg_t *allowed;
    mp_arg_val_t *out_vals;
} pm_upy_arg_spec_t;

int pm_upy_arg_parse(size_t n_args, const pm_upy_obj_t *args, void *spec) {
    if (!spec || (n_args && !args)) {
        return PM_ERR_ARG;
    }
    pm_upy_arg_spec_t *s = (pm_upy_arg_spec_t *)spec;
    if (!s->allowed || !s->out_vals) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_arg_parse_all(n_args, (const mp_obj_t *)args, NULL, s->n_allowed, s->allowed, s->out_vals);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
}
