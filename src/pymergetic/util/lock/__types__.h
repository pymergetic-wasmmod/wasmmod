/* pymergetic.util.lock — handwritten ABI (RS muscle, C callers). */

#ifndef PYMERGETIC_UTIL_LOCK_TYPES_H
#define PYMERGETIC_UTIL_LOCK_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t locked;
} pm_util_lock_t;

void pm_util_lock_init(pm_util_lock_t *);
void pm_util_lock_acquire(pm_util_lock_t *);
int32_t pm_util_lock_try_acquire(pm_util_lock_t *);
void pm_util_lock_release(pm_util_lock_t *);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_LOCK_TYPES_H */
