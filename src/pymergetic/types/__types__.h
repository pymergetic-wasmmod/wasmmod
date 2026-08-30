/* pymergetic.types — universal value + descriptor registry ABI shapes.
 * One memory layout shared by C, Rust, C++, Python: the 16-byte value
 * passes by value everywhere, compound objects carry an 8-byte header
 * whose first word IS the type identity (pointer compare, no hash),
 * and every type publishes a static const descriptor registered into
 * the live registry — the same posture as PM_MOD_EXPORT_C faces.
 * See docs/SOURCETREE.md "Faces" for the card conventions. */
#ifndef PYMERGETIC_TYPES_TYPES_H
#define PYMERGETIC_TYPES_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* pm_util_mem_arena_t — arena backing for all compound allocations.
 * util/mem owns the layout; types only allocs through it. Including the
 * card's own ABI header keeps the single typedef (C11 clean on emcc). */
#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------
 * Tag encoding: low 12 bits kind, high 4 bits flags.
 *--------------------------------------------------------------------*/
#define PM_TYPE_TAG_KIND_SHIFT  0u
#define PM_TYPE_TAG_KIND_MASK   0x0FFFu
#define PM_TYPE_TAG_FLAG_SHIFT  12u
#define PM_TYPE_TAG_FLAG_MASK   0xF000u

/* Kinds — payload packing noted per kind. */
#define PM_TYPE_KIND_NIL    0u   /* all zero */
#define PM_TYPE_KIND_I32    1u   /* payload.i32 */
#define PM_TYPE_KIND_I64    2u   /* payload.i64 */
#define PM_TYPE_KIND_U32    3u   /* payload.u32 */
#define PM_TYPE_KIND_U64    4u   /* payload.u64 */
#define PM_TYPE_KIND_F32    5u   /* payload.f32 */
#define PM_TYPE_KIND_F64    6u   /* payload.f64 */
#define PM_TYPE_KIND_BOOL   7u   /* payload.i32 (0/1) */
#define PM_TYPE_KIND_STR    8u   /* payload.ptr + aux = byte len */
#define PM_TYPE_KIND_BYTES  9u   /* payload.ptr + aux = byte len */
#define PM_TYPE_KIND_OBJ    10u  /* payload.ptr -> heap object (header) */
#define PM_TYPE_KIND_PTR    11u  /* payload.ptr, opaque, not owned */

/* Flag bits OR'd into the tag's high nibble. */
#define PM_TYPE_FLAG_LOCKED   0x1000u /* mutation in progress (writer) */
#define PM_TYPE_FLAG_CONST    0x2000u /* immutable value */

#define PM_TYPE_FIELD_END     0xFFFFu /* struct_new varargs sentinel */

/*----------------------------------------------------------------------
 * The 16-byte universal value. Two x86-64 registers, movdqa-friendly.
 *--------------------------------------------------------------------*/
typedef struct pm_types_value {
    uint16_t tag;       /* kind | flags */
    uint16_t aux;       /* str/bytes byte len; list len; dict used */
    uint16_t aux2;      /* list/dict capacity */
    uint16_t _rsv;
    union {
        int32_t  i32;
        int64_t  i64;
        uint32_t u32;
        uint64_t u64;
        float    f32;
        double   f64;
        void    *ptr;
    } payload;
} pm_type_value_t;

/*----------------------------------------------------------------------
 * Descriptor kinds — the meta-type.
 *--------------------------------------------------------------------*/
#define PM_TYPE_DESC_STRUCT    0u
#define PM_TYPE_DESC_LIST      1u
#define PM_TYPE_DESC_DICT      2u
#define PM_TYPE_DESC_ENUM      3u
#define PM_TYPE_DESC_UNION     4u
#define PM_TYPE_DESC_PRIMITIVE 5u

typedef struct pm_types_descriptor pm_type_descriptor_t;
typedef struct pm_types_field      pm_type_field_t;
typedef struct pm_types_method     pm_type_method_t;

/* Field descriptor. Instances lay fields contiguously; offset is from
 * the start of the instance data (after the 8-byte obj header). */
struct pm_types_field {
    uint16_t                     name_hash; /* djb2 of name (16-bit) */
    uint16_t                     _flags;
    uint32_t                     offset;    /* byte offset in instance data */
    const pm_type_descriptor_t  *type;      /* field's type (NULL = any) */
    const char                  *name;      /* NUL-terminated */
};

/* Method descriptor — a typed fn pointer the descriptor publishes. */
struct pm_types_method {
    uint16_t                     name_hash;
    uint16_t                     is_static;
    const char                  *name;
    const char                  *sig;   /* human-readable ABI sig */
    void                        *fn;
};

/*----------------------------------------------------------------------
 * The descriptor — the "klass". Static const, immutable after
 * registration. Type identity is pointer equality: d1 == d2.
 * Inheritance: single-parent chain, parent's fields come first
 * (offset 0..parent->instance_size), own fields follow.
 *--------------------------------------------------------------------*/
struct pm_types_descriptor {
    uint32_t                     magic;     /* PM_TYPE_DESCRIPTOR_MAGIC */
    uint16_t                     kind;      /* pm_types_desc_kind_t */
    uint16_t                     instance_size; /* bytes of instance data */
    const char                  *name;      /* "Point" */
    const char                  *fqn;       /* "pymergetic.metal.geo.Point" */
    const pm_type_descriptor_t  *parent;    /* inheritance, NULL = root */
    uint16_t                     field_count;
    const pm_type_field_t       *fields;    /* sorted by name_hash */
    uint16_t                     method_count;
    const pm_type_method_t      *methods;   /* sorted by name_hash */
};

#define PM_TYPE_DESCRIPTOR_MAGIC 0x54595045u /* "TYPE" */

/*----------------------------------------------------------------------
 * Heap object header — prepended to every compound allocation.
 * data() = (uint8_t*)(h + 1); fields live at data()+field->offset.
 *--------------------------------------------------------------------*/
typedef struct pm_types_obj_header {
    const pm_type_descriptor_t  *desc;   /* identity via pointer compare */
    int32_t                     refcount;
} pm_type_obj_header_t;

/* List/dict instance data (first thing after the header when the
 * descriptor kind is LIST/DICT). Elements are pm_type_value_t. */
typedef struct pm_types_vec {
    uint32_t    len;
    uint32_t    cap;
    /* pm_type_value_t elems[cap] follow */
} pm_type_value_tEC;

/* Dict entry: open-addressing linear probe over this pair. */
typedef struct pm_types_pair {
    uint16_t    used;      /* 0 empty, 1 live, 0xFFFF tombstone */
    uint16_t    _rsv;
    uint32_t    hash;      /* key hash (i64 or str hash) */
    pm_type_value_t key;
    pm_type_value_t val;
} pm_type_pair_t;

/*----------------------------------------------------------------------
 * C API — primitives (value constructors, no allocation)
 *--------------------------------------------------------------------*/
pm_type_value_t pm_types_nil(void);
pm_type_value_t pm_types_i32(int32_t v);
pm_type_value_t pm_types_i64(int64_t v);
pm_type_value_t pm_types_u32(uint32_t v);
pm_type_value_t pm_types_u64(uint64_t v);
pm_type_value_t pm_types_f32(float v);
pm_type_value_t pm_types_f64(double v);
pm_type_value_t pm_types_bool(int32_t v);

/* Str/bytes: copies into the arena, NUL-terminates for str. */
pm_type_value_t pm_types_str(pm_util_mem_arena_t *arena,
    const char *s, uint32_t len);
pm_type_value_t pm_types_bytes(pm_util_mem_arena_t *arena,
    const uint8_t *data, uint32_t len);

/* Borrowed view: no copy, PM_TYPE_FLAG_CONST set (caller owns memory). */
pm_type_value_t pm_types_str_borrowed(const char *s, uint32_t len);

/*-- Compound constructors (arena-backed) ------------------------------*/

/* Varargs: (uint16_t name_hash, pm_type_value_t) pairs, PM_TYPE_FIELD_END
 * terminates. Field values are stored raw at their offsets; str/bytes
 * payloads are NOT deep-copied (arena owns them; borrow with care). */
pm_type_value_t pm_types_struct_new(pm_util_mem_arena_t *arena,
    const pm_type_descriptor_t *desc, ...);

/* List with inline capacity reservation. */
pm_type_value_t pm_types_list_new(pm_util_mem_arena_t *arena,
    uint32_t capacity);

/* Dict with capacity reservation (power of two >= capacity). */
pm_type_value_t pm_types_dict_new(pm_util_mem_arena_t *arena,
    uint32_t capacity);

/*-- Accessors ----------------------------------------------------------*/

uint16_t pm_types_kind(pm_type_value_t v);
const pm_type_descriptor_t *pm_types_descriptor_of(pm_type_value_t v);
int32_t  pm_types_is_nil(pm_type_value_t v);
int32_t  pm_types_is_struct(pm_type_value_t v);

/* Field access by name_hash — binary search over the descriptor's
 * sorted fields (walks the parent chain). 0 = ok, negative = miss. */
int32_t  pm_types_field_i32(pm_type_value_t v, uint16_t name_hash,
    int32_t *out);
int32_t  pm_types_field_f64(pm_type_value_t v, uint16_t name_hash,
    double *out);
int32_t  pm_types_field_i64(pm_type_value_t v, uint16_t name_hash,
    int64_t *out);
int32_t  pm_types_field_str(pm_type_value_t v, uint16_t name_hash,
    const char **out, uint32_t *len);
int32_t  pm_types_field_value(pm_type_value_t v, uint16_t name_hash,
    pm_type_value_t *out);

/* Field write — raw store at offset; refuses non-struct, miss, and
 * PM_TYPE_FLAG_CONST values. The caller serialises (lock or single
 * writer) — see pm_types_lock. */
int32_t  pm_types_field_set_i32(pm_type_value_t *v, uint16_t name_hash,
    int32_t val);
int32_t  pm_types_field_set_f64(pm_type_value_t *v, uint16_t name_hash,
    double val);
int32_t  pm_types_field_set_value(pm_type_value_t *v, uint16_t name_hash,
    pm_type_value_t val);

/* Inheritance-aware field lookup (walks parent chain). */
const pm_type_field_t *pm_types_find_field(
    const pm_type_descriptor_t *d, uint16_t name_hash);
int32_t  pm_types_is_instance_of(pm_type_value_t v,
    const pm_type_descriptor_t *d);

/* djb2 16-bit name hash — the canonical hash for field/method names. */
uint16_t pm_types_name_hash(const char *name);

/*-- List / dict --------------------------------------------------------*/

int32_t  pm_types_list_len(pm_type_value_t v, uint32_t *out);
int32_t  pm_types_list_get(pm_type_value_t v, uint32_t index,
    pm_type_value_t *out);
int32_t  pm_types_list_set(pm_type_value_t *v, uint32_t index,
    pm_type_value_t item);
int32_t  pm_types_list_push(pm_type_value_t *v, pm_type_value_t item);
int32_t  pm_types_list_pop(pm_type_value_t *v, pm_type_value_t *out);

int32_t  pm_types_dict_len(pm_type_value_t v, uint32_t *out);
int32_t  pm_types_dict_get(pm_type_value_t v, pm_type_value_t key,
    pm_type_value_t *out);
int32_t  pm_types_dict_set(pm_type_value_t *v, pm_type_value_t key,
    pm_type_value_t val);
int32_t  pm_types_dict_del(pm_type_value_t *v, pm_type_value_t key);

/*-- Object header helpers (zero-copy views) --------------------------- */

/* Raw instance-data pointer for a struct value (after the header). */
void    *pm_types_obj_data(pm_type_value_t v);
/* The header itself (identity + refcount). */
pm_type_obj_header_t *pm_types_obj_header(pm_type_value_t v);

/* Refcount — arena owns the block, so unref never frees; the count
 * exists for cross-language ownership bookkeeping (Python wrappers,
 * Rust views) until a real GC story lands. */
int32_t  pm_types_ref(pm_type_value_t v);
int32_t  pm_types_unref(pm_type_value_t v);

/*-- Locking ------------------------------------------------------------*/

/* Tag-bit lock: sets PM_TYPE_FLAG_LOCKED via CAS on the tag word.
 * A locked value refuses further locks (single writer). Unlock by
 * clearing the bit; immutable (CONST) values refuse both. */
int32_t  pm_types_lock(pm_type_value_t *v);
int32_t  pm_types_unlock(pm_type_value_t *v);

/*-- Registry -----------------------------------------------------------*/

/* Register a descriptor (idempotent on same pointer, refuses a
 * different descriptor under a live fqn). Sorted insert by fqn. */
int32_t  pm_types_registry_register(const pm_type_descriptor_t *desc);

/* Find by fqn. Binary search, NULL on miss. */
const pm_type_descriptor_t *pm_types_registry_find(const char *fqn);

/* Count / at — module enumeration (all registered types, index order). */
uint32_t pm_types_registry_count(void);
const pm_type_descriptor_t *pm_types_registry_at(uint32_t index);

/* Primitive descriptor singletons — always registered at boot. */
extern const pm_type_descriptor_t PM_TYPE_NIL_DESC;
extern const pm_type_descriptor_t PM_TYPE_I32_DESC;
extern const pm_type_descriptor_t PM_TYPE_I64_DESC;
extern const pm_type_descriptor_t PM_TYPE_U32_DESC;
extern const pm_type_descriptor_t PM_TYPE_U64_DESC;
extern const pm_type_descriptor_t PM_TYPE_F32_DESC;
extern const pm_type_descriptor_t PM_TYPE_F64_DESC;
extern const pm_type_descriptor_t PM_TYPE_BOOL_DESC;
extern const pm_type_descriptor_t PM_TYPE_STR_DESC;
extern const pm_type_descriptor_t PM_TYPE_BYTES_DESC;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_TYPES_TYPES_H */
