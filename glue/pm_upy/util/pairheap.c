/*
 * Initialise a pairing-heap node.
 */

#include "pm_upy/util/pairheap.h"
#include "pm_common.h"
#include "py/pairheap.h"

int pm_upy_pairheap_init(void *heap) {
    if (!heap) {
        return PM_ERR_ARG;
    }
    mp_pairheap_init_node(NULL, (mp_pairheap_t *)heap);
    return PM_OK;
}
