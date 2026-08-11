/* pymergetic.util.lock — hand-written stand-in for what `cbindgen` should
 * emit by reading __impl__.rs. Delete once the real cbindgen pipeline
 * exists; this is training-scaffold, not the plan.
 *
 * This is the raw, non-generic primitive only — no payload, just the
 * exclusion bit. C consumers manage their own protected data next to a
 * bare pm_util_lock_t, same as any real-world lock; Rust consumers normally
 * want the ergonomic generic SpinLock<T>/Mutex<T> wrapper in __impl__.rs
 * instead (not exported here — C has no generics to mirror it into). */
#ifndef PYMERGETIC_UTIL_LOCK_EXPORT_H
#define PYMERGETIC_UTIL_LOCK_EXPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Layout-compatible with Rust's `AtomicU32` — one plain 32-bit word, 0 =
 * unlocked / 1 = locked. Must be zero-initialized or pm_util_lock_init'd
 * before first use. */
typedef struct {
    uint32_t locked;
} pm_util_lock_t;

void pm_util_lock_init(pm_util_lock_t *lock);
void pm_util_lock_acquire(pm_util_lock_t *lock);
void pm_util_lock_release(pm_util_lock_t *lock);
/* Returns 1 if acquired, 0 if already locked. */
int32_t pm_util_lock_try_acquire(pm_util_lock_t *lock);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_LOCK_EXPORT_H */
