/* pymergetic.util.mem — what this module provides. See SOURCETREE.md
 * "Faces": host+guest read this one face, no direction ifdefs.
 *
 * Dual-span arena, same shape as metal's proven
 * pymergetic.metal.mem.{arena,tlsf}/port/mem.c, ported down into wasmmod
 * so metal (and any other product) consumes this instead of growing its
 * own copy again:
 *
 *   low (map_brk ->)                 (<- heap_brk) high
 *   [ map: bump-up, LIFO unmap ][ HOLE ][ TLSF pools: bump-down, lazy add ]
 *
 * One caller-owned [base, base+size) region per arena; "dual arena" as a
 * *usage pattern* (e.g. one arena for host heap, another for wasm linear
 * memory) still composes on top of this — nothing here hardcodes a pair
 * of arenas, just the two spans inside any one of them. */
#ifndef PYMERGETIC_UTIL_MEM_EXPORT_H
#define PYMERGETIC_UTIL_MEM_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#include "src/pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Carves an arena out of a caller-owned [base, base+size) region (the
 * arena header itself lives at the front of that region — no separate
 * allocation for it). No syscalls, no implicit backing store — the
 * caller decides where the bytes come from (static array, mmap, wasm
 * linear memory, ...). Seeds an initial TLSF pool from the high side;
 * returns NULL if size can't even fit the header + one minimal pool. */
pm_util_mem_arena_t *pm_util_mem_arena_create(void *base, size_t size);

/* Frees no memory itself (the arena's bytes are caller-owned); just tears
 * down TLSF's own bookkeeping for every pool this arena ever added. */
void pm_util_mem_arena_destroy(pm_util_mem_arena_t *arena);

/* High side: general allocation, backed by TLSF. On a TLSF miss, first
 * tries carving one more pool out of the shrinking hole (see
 * pm_util_mem_arena_hole) before giving up. */
void *pm_util_mem_alloc(pm_util_mem_arena_t *arena, size_t size);
void *pm_util_mem_realloc(pm_util_mem_arena_t *arena, void *ptr, size_t size);
void *pm_util_mem_memalign(pm_util_mem_arena_t *arena, size_t align, size_t size);
void pm_util_mem_free(pm_util_mem_arena_t *arena, void *ptr);

/* Low side: page-granular bump allocator, independent of TLSF — for
 * page-table-ish/stack-ish grants that want raw carve-and-return, not
 * general alloc. Unmap is LIFO-only (must unmap the most recent map()
 * first), same constraint as metal's arena.unmap. */
void *pm_util_mem_map(pm_util_mem_arena_t *arena, size_t size);
int32_t pm_util_mem_unmap(pm_util_mem_arena_t *arena, void *ptr, size_t size);

/* Introspection — same four numbers as metal's pm_metal_mem_arena_*: total
 * span, bytes claimed by the map side, bytes claimed by the heap/TLSF
 * side (pools added so far, not live allocations within them), and the
 * still-unclaimed hole between them. */
size_t pm_util_mem_arena_bytes(const pm_util_mem_arena_t *arena);
size_t pm_util_mem_arena_map_used(const pm_util_mem_arena_t *arena);
size_t pm_util_mem_arena_heap_used(const pm_util_mem_arena_t *arena);
size_t pm_util_mem_arena_hole(const pm_util_mem_arena_t *arena);

/* Bytes an arena needs to reserve for its own header + one minimal TLSF
 * pool before any real allocation fits — lets a caller size a static
 * buffer correctly. */
size_t pm_util_mem_arena_overhead(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_MEM_EXPORT_H */
