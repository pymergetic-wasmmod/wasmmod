/* pymergetic.util.mem — shared ABI shapes. See SOURCETREE.md "Faces". */
#ifndef PYMERGETIC_UTIL_MEM_TYPES_H
#define PYMERGETIC_UTIL_MEM_TYPES_H

/* Opaque: internal layout (dual-span brks, TLSF handle, lock) lives behind
 * this, callers never peek inside. */
typedef struct pm_util_mem_arena pm_util_mem_arena_t;

#endif /* PYMERGETIC_UTIL_MEM_TYPES_H */
