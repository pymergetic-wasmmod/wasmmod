/* pymergetic.util.mem — impl. Consumer face is generated __exports__.h.
 *
 * Ported from metal's proven pymergetic.metal.mem.{arena,tlsf} +
 * mem/port/mem.c (packages/metalpython/extmod/metal/...) down into
 * wasmmod, so that's the one place this logic lives — metal becomes a
 * consumer of this module instead of carrying its own copy. */
#include "pymergetic/util/mem/__types__.h"
#include "pymergetic/util/lock/__types__.h"

#include "third_party/tlsf/tlsf.h"

#include <stddef.h>
#include <stdint.h>

#include <string.h>

enum {
    PM_UTIL_MEM_PAGE_SIZE = 4096u,
    PM_UTIL_MEM_MIN_SPAN = PM_UTIL_MEM_PAGE_SIZE * 8u,
    /* Initial TLSF seed clamps — mirrors metal's initial_tlsf_bytes: keep
     * the hole the majority share at every scale so map() and later
     * heap_grow_pool() calls both still have room. */
    PM_UTIL_MEM_TLSF_INIT_MIN = 256u * 1024u,
    PM_UTIL_MEM_TLSF_INIT_CAP = 128u * 1024u * 1024u,
};

struct pm_util_mem_arena {
    unsigned char *base;
    unsigned char *end;
    unsigned char *map_brk;  /* low side, bump-up, LIFO unmap */
    unsigned char *heap_brk; /* high side, bump-down; TLSF pools live above this */
    tlsf_t tlsf;             /* NULL until the first pool is seeded */
    pm_util_lock_t lock;
};

static size_t align_up(size_t x, size_t a) {
    return (x + (a - 1u)) & ~(a - 1u);
}

static size_t align_down(size_t x, size_t a) {
    return x & ~(a - 1u);
}

/* First TLSF pool size for a claimed span (page-aligned): ~1/8 of span,
 * clamped to [structural_min, CAP], and never more than 1/4 of a
 * comfortable (>= 4 MiB) span so the hole stays the majority share. */
static size_t initial_tlsf_bytes(size_t span, size_t structural_min) {
    size_t min_seed = structural_min < PM_UTIL_MEM_TLSF_INIT_MIN ? PM_UTIL_MEM_TLSF_INIT_MIN
                                                                   : structural_min;
    min_seed = align_up(min_seed, PM_UTIL_MEM_PAGE_SIZE);
    if (min_seed > span) {
        min_seed = align_down(span, PM_UTIL_MEM_PAGE_SIZE);
    }

    size_t want = span / 8u;
    if (want < min_seed) {
        want = min_seed;
    }
    if (want > (size_t)PM_UTIL_MEM_TLSF_INIT_CAP) {
        want = (size_t)PM_UTIL_MEM_TLSF_INIT_CAP;
    }

    if (span >= (4u * 1024u * 1024u)) {
        size_t max_init = align_down(span / 4u, PM_UTIL_MEM_PAGE_SIZE);
        if (max_init < min_seed) {
            max_init = min_seed;
        }
        if (want > max_init) {
            want = max_init;
        }
    }

    want = align_down(want, PM_UTIL_MEM_PAGE_SIZE);
    if (want < structural_min) {
        want = align_up(structural_min, PM_UTIL_MEM_PAGE_SIZE);
        if (want > span) {
            want = align_down(span, PM_UTIL_MEM_PAGE_SIZE);
        }
    }
    return want;
}

pm_util_mem_arena_t *pm_util_mem_arena_create(void *base, size_t size) {
    if (base == NULL) {
        return NULL;
    }

    /* Header lives at the front of the caller's own region; the rest of
     * [base, base+size) is the dual-span [map_brk, heap_brk) working set. */
    uintptr_t b = (uintptr_t)base;
    uintptr_t al = align_up(b, PM_UTIL_MEM_PAGE_SIZE);
    size_t skip = (size_t)(al - b);
    if (size <= skip) {
        return NULL;
    }
    size_t span = align_down(size - skip, PM_UTIL_MEM_PAGE_SIZE);
    if (span < PM_UTIL_MEM_MIN_SPAN) {
        return NULL;
    }
    unsigned char *region = (unsigned char *)al;

    size_t hdr = align_up(sizeof(pm_util_mem_arena_t), PM_UTIL_MEM_PAGE_SIZE);
    if (hdr >= span) {
        return NULL;
    }
    pm_util_mem_arena_t *arena = (pm_util_mem_arena_t *)region;
    unsigned char *usable = region + hdr;
    size_t usable_span = span - hdr;

    size_t structural_min = tlsf_size() + tlsf_pool_overhead() + 64u;
    structural_min = align_up(structural_min, PM_UTIL_MEM_PAGE_SIZE);
    size_t want = initial_tlsf_bytes(usable_span, structural_min);
    if (want < structural_min || want > usable_span) {
        return NULL;
    }

    arena->base = usable;
    arena->end = usable + usable_span;
    arena->map_brk = usable;
    arena->heap_brk = arena->end - want;
    arena->tlsf = tlsf_create_with_pool(arena->heap_brk, want);
    if (arena->tlsf == NULL) {
        return NULL;
    }
    pm_util_lock_init(&arena->lock);
    return arena;
}

void pm_util_mem_arena_destroy(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return;
    }
    if (arena->tlsf != NULL) {
        tlsf_destroy(arena->tlsf);
    }
}

/* Carve one more pool out of the hole; caller already holds arena->lock.
 * Returns 0 on success. */
static int heap_grow_pool(pm_util_mem_arena_t *arena, size_t need) {
    size_t hole = (size_t)(arena->heap_brk - arena->map_brk);
    /* TLSF is segmented-fit: it rounds a request up to a size class before
     * searching, so a pool sized at exactly need+overhead can still miss
     * for large requests (class granularity grows with size). need/16
     * (~6%) covers that rounding; tiny for small `need`, where PAGE_SIZE
     * rounding below already dwarfs it anyway. */
    size_t grow = align_up(need + need / 16u + tlsf_pool_overhead() + 64u, PM_UTIL_MEM_PAGE_SIZE);
    if (grow < (size_t)PM_UTIL_MEM_TLSF_INIT_MIN) {
        grow = (size_t)PM_UTIL_MEM_TLSF_INIT_MIN;
    }
    grow = align_up(grow, PM_UTIL_MEM_PAGE_SIZE);
    if (grow > hole) {
        grow = align_down(hole, PM_UTIL_MEM_PAGE_SIZE);
    }
    if (grow < tlsf_pool_overhead() + 64u) {
        return -1;
    }
    unsigned char *pool = arena->heap_brk - grow;
    if (tlsf_add_pool(arena->tlsf, pool, grow) == NULL) {
        return -1;
    }
    arena->heap_brk = pool;
    return 0;
}

void *pm_util_mem_alloc(pm_util_mem_arena_t *arena, size_t size) {
    if (arena == NULL || size == 0) {
        return NULL;
    }
    pm_util_lock_acquire(&arena->lock);
    void *p = tlsf_malloc(arena->tlsf, size);
    if (p == NULL && heap_grow_pool(arena, size) == 0) {
        p = tlsf_malloc(arena->tlsf, size);
    }
    pm_util_lock_release(&arena->lock);
    return p;
}

void *pm_util_mem_realloc(pm_util_mem_arena_t *arena, void *ptr, size_t size) {
    if (arena == NULL) {
        return NULL;
    }
    pm_util_lock_acquire(&arena->lock);
    void *p = tlsf_realloc(arena->tlsf, ptr, size);
    if (p == NULL && size > 0 && heap_grow_pool(arena, size) == 0) {
        p = tlsf_realloc(arena->tlsf, ptr, size);
    }
    pm_util_lock_release(&arena->lock);
    return p;
}

void *pm_util_mem_memalign(pm_util_mem_arena_t *arena, size_t align, size_t size) {
    if (arena == NULL || size == 0) {
        return NULL;
    }
    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }
    pm_util_lock_acquire(&arena->lock);
    void *p = tlsf_memalign(arena->tlsf, align, size);
    if (p == NULL && heap_grow_pool(arena, size + align) == 0) {
        p = tlsf_memalign(arena->tlsf, align, size);
    }
    pm_util_lock_release(&arena->lock);
    return p;
}

void pm_util_mem_free(pm_util_mem_arena_t *arena, void *ptr) {
    if (arena == NULL || ptr == NULL) {
        return;
    }
    pm_util_lock_acquire(&arena->lock);
    tlsf_free(arena->tlsf, ptr);
    pm_util_lock_release(&arena->lock);
}

void *pm_util_mem_map(pm_util_mem_arena_t *arena, size_t size) {
    if (arena == NULL || size == 0) {
        return NULL;
    }
    size_t need = align_up(size, PM_UTIL_MEM_PAGE_SIZE);
    pm_util_lock_acquire(&arena->lock);
    if (need > (size_t)(arena->heap_brk - arena->map_brk)) {
        pm_util_lock_release(&arena->lock);
        return NULL;
    }
    unsigned char *p = arena->map_brk;
    arena->map_brk += need;
    pm_util_lock_release(&arena->lock);
    return p;
}

int32_t pm_util_mem_unmap(pm_util_mem_arena_t *arena, void *ptr, size_t size) {
    if (arena == NULL || ptr == NULL || size == 0) {
        return -1;
    }
    size_t need = align_up(size, PM_UTIL_MEM_PAGE_SIZE);
    pm_util_lock_acquire(&arena->lock);
    int ok = (size_t)(arena->map_brk - arena->base) >= need
        && (unsigned char *)ptr == arena->map_brk - need;
    if (ok) {
        arena->map_brk -= need;
    }
    pm_util_lock_release(&arena->lock);
    return ok ? 0 : -1;
}

size_t pm_util_mem_arena_bytes(const pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return 0;
    }
    return (size_t)(arena->end - arena->base);
}

size_t pm_util_mem_arena_map_used(const pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return 0;
    }
    return (size_t)(arena->map_brk - arena->base);
}

size_t pm_util_mem_arena_heap_used(const pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return 0;
    }
    return (size_t)(arena->end - arena->heap_brk);
}

size_t pm_util_mem_arena_hole(const pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return 0;
    }
    return (size_t)(arena->heap_brk - arena->map_brk);
}

size_t pm_util_mem_arena_overhead(void) {
    size_t hdr = align_up(sizeof(pm_util_mem_arena_t), PM_UTIL_MEM_PAGE_SIZE);
    size_t structural_min = tlsf_size() + tlsf_pool_overhead() + 64u;
    return hdr + align_up(structural_min, PM_UTIL_MEM_PAGE_SIZE);
}

/* Same table as PM_MOD_EXPORT_RS! — C language face, next to the muscle. */
#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_arena_create, pm_util_mem_arena_create, pm_util_mem_arena_t *(void *, size_t));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_arena_destroy, pm_util_mem_arena_destroy, void(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_alloc, pm_util_mem_alloc, void *(pm_util_mem_arena_t *, size_t));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_realloc, pm_util_mem_realloc, void *(pm_util_mem_arena_t *, void *, size_t));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_memalign, pm_util_mem_memalign, void *(pm_util_mem_arena_t *, size_t, size_t));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_free, pm_util_mem_free, void(pm_util_mem_arena_t *, void *));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_map, pm_util_mem_map, void *(pm_util_mem_arena_t *, size_t));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_unmap, pm_util_mem_unmap, int32_t(pm_util_mem_arena_t *, void *, size_t));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_arena_bytes, pm_util_mem_arena_bytes, size_t(const pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_arena_map_used, pm_util_mem_arena_map_used, size_t(const pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_arena_heap_used, pm_util_mem_arena_heap_used, size_t(const pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_arena_hole, pm_util_mem_arena_hole, size_t(const pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.util.mem, pm_util_mem_arena_overhead, pm_util_mem_arena_overhead, size_t(void));
