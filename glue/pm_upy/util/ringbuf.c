/*
 * Thin wrap of MicroPython ringbuf_t put/get.
 */

#include "pm_upy/util/ringbuf.h"
#include "pm_common.h"
#include "py/ringbuf.h"

int pm_upy_ringbuf_put(void *rb, uint8_t v) {
    if (!rb) {
        return PM_ERR_ARG;
    }
    return ringbuf_put((ringbuf_t *)rb, v) == 0 ? PM_OK : PM_ERR;
}

int pm_upy_ringbuf_get(void *rb, uint8_t *v) {
    if (!rb || !v) {
        return PM_ERR_ARG;
    }
    int c = ringbuf_get((ringbuf_t *)rb);
    if (c < 0) {
        return PM_ERR;
    }
    *v = (uint8_t)c;
    return PM_OK;
}
