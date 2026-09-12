/* pymergetic.util.mem — shared ABI shapes. See SOURCETREE.md "Faces". */
#ifndef PYMERGETIC_UTIL_MEM_TYPES_H
#define PYMERGETIC_UTIL_MEM_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* Opaque: internal layout (dual-span brks, TLSF handle, lock) lives behind
 * this, callers never peek inside. */
typedef struct pm_util_mem_arena pm_util_mem_arena_t;

/* Walk callback: called once per block in every pool. ptr+size delimit the
 * block; used is 1 when the block is in-flight (allocated, not free). user
 * is the caller's opaque context pointer. */
typedef void (*pm_util_mem_walk_fn)(void *ptr, size_t size, int used, void *user);

#ifdef __cplusplus
extern "C" {
#endif

/* Walk every pool in the arena, calling fn once per block. The arena's heap
 * lock is held during the walk; fn must not allocate or free from the same
 * arena. */
void pm_util_mem_arena_walk(const pm_util_mem_arena_t *arena, pm_util_mem_walk_fn fn, void *user);

/* Check every pool's internal integrity (block coalescing, free-list links,
 * sentinel guards). Returns 0 when clean, non-zero when a pool fails.
 * Diagnostic only; never called on the hot path. */
int32_t pm_util_mem_arena_check(const pm_util_mem_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_MEM_TYPES_H */
