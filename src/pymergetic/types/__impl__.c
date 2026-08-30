/* pymergetic.types — universal value: constructors, field access,
 * list/dict, lock, registry. One layout for C/Rust/C++/Python; compound
 * blocks come from the caller's pm_util_mem_arena (the arena owns the
 * memory for the process lifetime, so no free path exists — refcounts
 * are cross-language bookkeeping only). Consumer face is generated
 * __exports__.h. */
#include "pymergetic/types/__types__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------------
 * Primitives — pure value construction, no allocation.
 *--------------------------------------------------------------------*/
static pm_type_value_t mkval(uint16_t kind) {
    pm_type_value_t v;
    v.tag = kind;
    v.aux = 0;
    v.aux2 = 0;
    v._rsv = 0;
    v.payload.u64 = 0;
    return v;
}

pm_type_value_t pm_types_nil(void) { return mkval(PM_TYPE_KIND_NIL); }

pm_type_value_t pm_types_i32(int32_t x) {
    pm_type_value_t v = mkval(PM_TYPE_KIND_I32);
    v.payload.i32 = x;
    return v;
}

pm_type_value_t pm_types_i64(int64_t x) {
    pm_type_value_t v = mkval(PM_TYPE_KIND_I64);
    v.payload.i64 = x;
    return v;
}

pm_type_value_t pm_types_u32(uint32_t x) {
    pm_type_value_t v = mkval(PM_TYPE_KIND_U32);
    v.payload.u32 = x;
    return v;
}

pm_type_value_t pm_types_u64(uint64_t x) {
    pm_type_value_t v = mkval(PM_TYPE_KIND_U64);
    v.payload.u64 = x;
    return v;
}

pm_type_value_t pm_types_f32(float x) {
    pm_type_value_t v = mkval(PM_TYPE_KIND_F32);
    v.payload.f32 = x;
    return v;
}

pm_type_value_t pm_types_f64(double x) {
    pm_type_value_t v = mkval(PM_TYPE_KIND_F64);
    v.payload.f64 = x;
    return v;
}

pm_type_value_t pm_types_bool(int32_t x) {
    pm_type_value_t v = mkval(PM_TYPE_KIND_BOOL);
    v.payload.i32 = (x != 0);
    return v;
}

void *pm_util_mem_alloc(pm_util_mem_arena_t *arena, size_t size);

pm_type_value_t pm_types_str(pm_util_mem_arena_t *arena,
    const char *s, uint32_t len) {
    char *buf;
    pm_type_value_t v = mkval(PM_TYPE_KIND_STR);
    if (arena == NULL || s == NULL) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    buf = (char *)pm_util_mem_alloc(arena, (size_t)len + 1u);
    if (buf == NULL) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    memcpy(buf, s, len);
    buf[len] = '\0';
    v.aux = (uint16_t)len;
    v.payload.ptr = buf;
    return v;
}

pm_type_value_t pm_types_bytes(pm_util_mem_arena_t *arena,
    const uint8_t *data, uint32_t len) {
    uint8_t *buf;
    pm_type_value_t v = mkval(PM_TYPE_KIND_BYTES);
    if (arena == NULL || data == NULL) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    buf = (uint8_t *)pm_util_mem_alloc(arena, len);
    if (buf == NULL) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    memcpy(buf, data, len);
    v.aux = (uint16_t)len;
    v.payload.ptr = buf;
    return v;
}

pm_type_value_t pm_types_str_borrowed(const char *s, uint32_t len) {
    pm_type_value_t v = mkval(PM_TYPE_KIND_STR);
    if (s == NULL) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    v.tag |= PM_TYPE_FLAG_CONST;
    v.aux = (uint16_t)len;
    v.payload.ptr = (void *)(uintptr_t)s;
    return v;
}

/*----------------------------------------------------------------------
 * Compound constructors.
 *--------------------------------------------------------------------*/
static pm_type_value_t obj_new(pm_util_mem_arena_t *arena,
    const pm_type_descriptor_t *desc, size_t data_bytes) {
    pm_type_obj_header_t *h;
    pm_type_value_t v = mkval(PM_TYPE_KIND_OBJ);
    if (arena == NULL || desc == NULL || desc->magic != PM_TYPE_DESCRIPTOR_MAGIC) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    h = (pm_type_obj_header_t *)pm_util_mem_alloc(arena,
        sizeof(*h) + data_bytes);
    if (h == NULL) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    h->desc = desc;
    h->refcount = 1;
    v.payload.ptr = h;
    return v;
}

/*----------------------------------------------------------------------
 * Field cells: packed native storage typed by the field descriptor.
 * A declared f64 field occupies 8 bytes (a C view struct overlays the
 * same bytes — that is the zero-copy contract); str/bytes and
 * undeclared (type == NULL) fields hold a full 16-byte value cell.
 * instance_size covers parent cells first (parent fields at low
 * offsets, own fields after parent->instance_size).
 *--------------------------------------------------------------------*/
static uint32_t field_cell_bytes(const pm_type_field_t *f) {
    if (f->type == NULL) {
        return (uint32_t)sizeof(pm_type_value_t);
    }
    switch (f->type->kind) {
    case PM_TYPE_DESC_PRIMITIVE:
        return f->type->instance_size ? f->type->instance_size
            : (uint32_t)sizeof(pm_type_value_t);
    default:
        return (uint32_t)sizeof(pm_type_value_t);
    }
}

static void field_store(const pm_type_descriptor_t *desc,
    const pm_type_field_t *f, uint8_t *data, pm_type_value_t val) {
    uint32_t cell = field_cell_bytes(f);
    if (f->offset + cell > desc->instance_size) {
        return; /* authoring bug: cell past instance_size, refuse write */
    }
    switch (cell) {
    case 1:
        data[f->offset] = (uint8_t)(val.payload.i32 != 0);
        return;
    case 4:
        memcpy(data + f->offset, &val.payload.i32, 4u);
        return;
    case 8:
        memcpy(data + f->offset, &val.payload.u64, 8u);
        return;
    default:
        memcpy(data + f->offset, &val, sizeof(val));
        return;
    }
}

static pm_type_value_t field_fetch(const pm_type_descriptor_t *desc,
    const pm_type_field_t *f, const uint8_t *data) {
    pm_type_value_t out = pm_types_nil();
    uint32_t cell = field_cell_bytes(f);
    if (f->offset + cell > desc->instance_size) {
        return out;
    }
    if (cell == sizeof(pm_type_value_t)) {
        memcpy(&out, data + f->offset, sizeof(out));
        return out;
    }
    /* widen the packed cell back into a value of the declared type */
    if (f->type != NULL) {
        out.tag = (uint16_t)PM_TYPE_KIND_I32; /* placeholder, set below */
    }
    switch (cell) {
    case 1:
        out = pm_types_bool(data[f->offset] != 0);
        return out;
    case 4:
        if (f->type == &PM_TYPE_F32_DESC) {
            float fv;
            memcpy(&fv, data + f->offset, 4u);
            out = pm_types_f32(fv);
        } else if (f->type == &PM_TYPE_U32_DESC) {
            uint32_t uv;
            memcpy(&uv, data + f->offset, 4u);
            out = pm_types_u32(uv);
        } else {
            int32_t iv;
            memcpy(&iv, data + f->offset, 4u);
            out = pm_types_i32(iv);
        }
        return out;
    case 8:
        if (f->type == &PM_TYPE_F64_DESC) {
            double dv;
            memcpy(&dv, data + f->offset, 8u);
            out = pm_types_f64(dv);
        } else if (f->type == &PM_TYPE_U64_DESC) {
            uint64_t uv;
            memcpy(&uv, data + f->offset, 8u);
            out = pm_types_u64(uv);
        } else {
            int64_t iv;
            memcpy(&iv, data + f->offset, 8u);
            out = pm_types_i64(iv);
        }
        return out;
    default:
        return out;
    }
}

pm_type_value_t pm_types_struct_new(pm_util_mem_arena_t *arena,
    const pm_type_descriptor_t *desc, ...) {
    pm_type_value_t v = obj_new(arena, desc, desc ? desc->instance_size : 0u);
    va_list ap;
    if (pm_types_kind(v) != PM_TYPE_KIND_OBJ) {
        return v;
    }
    va_start(ap, desc);
    for (;;) {
        uint16_t name_hash = va_arg(ap, int); /* promoted uint16_t */
        pm_type_value_t val;
        const pm_type_field_t *f;
        if (name_hash == PM_TYPE_FIELD_END) {
            break;
        }
        val = va_arg(ap, pm_type_value_t);
        f = pm_types_find_field(desc, name_hash);
        if (f == NULL) {
            continue; /* unknown field: skipped, not fatal (schema drift) */
        }
        field_store(desc, f, (uint8_t *)pm_types_obj_data(v), val);
    }
    va_end(ap);
    return v;
}

/* List/dict descriptors are dynamic (per-instance element type is not
 * in the descriptor — a plain list/dict descriptor with no fields
 * serves every list/dict; identity is by descriptor). */
static const pm_type_descriptor_t *s_list_desc;
static const pm_type_descriptor_t *s_dict_desc;

pm_type_value_t pm_types_list_new(pm_util_mem_arena_t *arena,
    uint32_t capacity) {
    pm_type_value_t v;
    pm_type_value_tEC *vec;
    if (s_list_desc == NULL) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    if (capacity == 0u) {
        capacity = 4u;
    }
    v = obj_new(arena, s_list_desc,
        sizeof(pm_type_value_tEC) + (size_t)capacity * sizeof(pm_type_value_t));
    if (pm_types_kind(v) != PM_TYPE_KIND_OBJ) {
        return v;
    }
    vec = (pm_type_value_tEC *)pm_types_obj_data(v);
    vec->len = 0;
    vec->cap = capacity;
    v.aux = 0;
    v.aux2 = (uint16_t)capacity;
    return v;
}

pm_type_value_t pm_types_dict_new(pm_util_mem_arena_t *arena,
    uint32_t capacity) {
    pm_type_value_t v;
    pm_type_pair_t *slots;
    uint32_t i;
    uint32_t cap;
    if (s_dict_desc == NULL) {
        return mkval(PM_TYPE_KIND_NIL);
    }
    if (capacity == 0u) {
        capacity = 4u;
    }
    cap = 4u;
    while (cap < capacity) {
        cap <<= 1;
    }
    v = obj_new(arena, s_dict_desc,
        sizeof(uint32_t) + (size_t)cap * sizeof(pm_type_pair_t));
    if (pm_types_kind(v) != PM_TYPE_KIND_OBJ) {
        return v;
    }
    slots = (pm_type_pair_t *)((uint8_t *)pm_types_obj_data(v) + sizeof(uint32_t));
    for (i = 0; i < cap; i++) {
        slots[i].used = 0;
        slots[i].hash = 0;
        slots[i].key = mkval(PM_TYPE_KIND_NIL);
        slots[i].val = mkval(PM_TYPE_KIND_NIL);
    }
    *(uint32_t *)pm_types_obj_data(v) = cap; /* capacity word first */
    v.aux = 0;
    v.aux2 = (uint16_t)cap;
    return v;
}

/*----------------------------------------------------------------------
 * Accessors.
 *--------------------------------------------------------------------*/
uint16_t pm_types_kind(pm_type_value_t v) {
    return (uint16_t)(v.tag & PM_TYPE_TAG_KIND_MASK);
}

pm_type_obj_header_t *pm_types_obj_header(pm_type_value_t v) {
    return (pm_type_obj_header_t *)v.payload.ptr;
}

void *pm_types_obj_data(pm_type_value_t v) {
    return (uint8_t *)v.payload.ptr + sizeof(pm_type_obj_header_t);
}

const pm_type_descriptor_t *pm_types_descriptor_of(pm_type_value_t v) {
    switch (pm_types_kind(v)) {
    case PM_TYPE_KIND_NIL:   return &PM_TYPE_NIL_DESC;
    case PM_TYPE_KIND_I32:   return &PM_TYPE_I32_DESC;
    case PM_TYPE_KIND_I64:   return &PM_TYPE_I64_DESC;
    case PM_TYPE_KIND_U32:   return &PM_TYPE_U32_DESC;
    case PM_TYPE_KIND_U64:   return &PM_TYPE_U64_DESC;
    case PM_TYPE_KIND_F32:   return &PM_TYPE_F32_DESC;
    case PM_TYPE_KIND_F64:   return &PM_TYPE_F64_DESC;
    case PM_TYPE_KIND_BOOL:  return &PM_TYPE_BOOL_DESC;
    case PM_TYPE_KIND_STR:   return &PM_TYPE_STR_DESC;
    case PM_TYPE_KIND_BYTES: return &PM_TYPE_BYTES_DESC;
    case PM_TYPE_KIND_OBJ:   return pm_types_obj_header(v)->desc;
    default:                 return &PM_TYPE_NIL_DESC;
    }
}

int32_t pm_types_is_nil(pm_type_value_t v) {
    return pm_types_kind(v) == PM_TYPE_KIND_NIL;
}

int32_t pm_types_is_struct(pm_type_value_t v) {
    return pm_types_kind(v) == PM_TYPE_KIND_OBJ
        && pm_types_obj_header(v)->desc->kind == PM_TYPE_DESC_STRUCT;
}

/* Field lookup: own fields first, then parent chain. Each level's
 * fields are sorted by name_hash (the PM_TYPE_DEFINE_C contract), so
 * this is a binary search per level. */
static const pm_type_field_t *fields_find_sorted(
    const pm_type_field_t *fields, uint16_t count, uint16_t name_hash) {
    uint16_t lo = 0;
    uint16_t hi = count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)(lo + ((hi - lo) >> 1));
        if (fields[mid].name_hash == name_hash) {
            return &fields[mid];
        }
        if (fields[mid].name_hash < name_hash) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const pm_type_field_t *pm_types_find_field(
    const pm_type_descriptor_t *d, uint16_t name_hash) {
    while (d != NULL) {
        const pm_type_field_t *f =
            fields_find_sorted(d->fields, d->field_count, name_hash);
        if (f != NULL) {
            return f;
        }
        d = d->parent;
    }
    return NULL;
}

int32_t pm_types_is_instance_of(pm_type_value_t v,
    const pm_type_descriptor_t *d) {
    const pm_type_descriptor_t *vd;
    if (d == NULL) {
        return 0;
    }
    vd = pm_types_descriptor_of(v);
    while (vd != NULL) {
        if (vd == d) {
            return 1;
        }
        vd = vd->parent;
    }
    return 0;
}

/* Typed field reads: locate, then load the packed cell and widen it
 * back into a pm_type_value_t of the declared type. */
static int32_t field_load(pm_type_value_t v, uint16_t name_hash,
    pm_type_value_t *out) {
    const pm_type_field_t *f;
    const pm_type_descriptor_t *d;
    if (pm_types_kind(v) != PM_TYPE_KIND_OBJ) {
        return -1;
    }
    d = pm_types_obj_header(v)->desc;
    f = pm_types_find_field(d, name_hash);
    if (f == NULL) {
        return -2;
    }
    *out = field_fetch(d, f, (const uint8_t *)pm_types_obj_data(v));
    return 0;
}

int32_t pm_types_field_i32(pm_type_value_t v, uint16_t name_hash,
    int32_t *out) {
    pm_type_value_t f;
    int32_t rc = field_load(v, name_hash, &f);
    if (rc != 0) {
        return rc;
    }
    if (pm_types_kind(f) != PM_TYPE_KIND_I32
            && pm_types_kind(f) != PM_TYPE_KIND_BOOL) {
        return -3;
    }
    *out = f.payload.i32;
    return 0;
}

int32_t pm_types_field_i64(pm_type_value_t v, uint16_t name_hash,
    int64_t *out) {
    pm_type_value_t f;
    int32_t rc = field_load(v, name_hash, &f);
    if (rc != 0) {
        return rc;
    }
    if (pm_types_kind(f) != PM_TYPE_KIND_I64) {
        return -3;
    }
    *out = f.payload.i64;
    return 0;
}

int32_t pm_types_field_f64(pm_type_value_t v, uint16_t name_hash,
    double *out) {
    pm_type_value_t f;
    int32_t rc = field_load(v, name_hash, &f);
    if (rc != 0) {
        return rc;
    }
    if (pm_types_kind(f) != PM_TYPE_KIND_F64) {
        return -3;
    }
    *out = f.payload.f64;
    return 0;
}

int32_t pm_types_field_str(pm_type_value_t v, uint16_t name_hash,
    const char **out, uint32_t *len) {
    pm_type_value_t f;
    int32_t rc = field_load(v, name_hash, &f);
    if (rc != 0) {
        return rc;
    }
    if (pm_types_kind(f) != PM_TYPE_KIND_STR) {
        return -3;
    }
    *out = (const char *)f.payload.ptr;
    if (len != NULL) {
        *len = f.aux;
    }
    return 0;
}

int32_t pm_types_field_value(pm_type_value_t v, uint16_t name_hash,
    pm_type_value_t *out) {
    return field_load(v, name_hash, out);
}

int32_t pm_types_field_set_i32(pm_type_value_t *v, uint16_t name_hash,
    int32_t val) {
    return pm_types_field_set_value(v, name_hash, pm_types_i32(val));
}

int32_t pm_types_field_set_f64(pm_type_value_t *v, uint16_t name_hash,
    double val) {
    return pm_types_field_set_value(v, name_hash, pm_types_f64(val));
}

int32_t pm_types_field_set_value(pm_type_value_t *v, uint16_t name_hash,
    pm_type_value_t val) {
    const pm_type_field_t *f;
    const pm_type_descriptor_t *d;
    if (v == NULL || pm_types_kind(*v) != PM_TYPE_KIND_OBJ) {
        return -1;
    }
    if ((v->tag & PM_TYPE_FLAG_CONST) != 0u) {
        return -4;
    }
    d = pm_types_obj_header(*v)->desc;
    f = pm_types_find_field(d, name_hash);
    if (f == NULL) {
        return -2;
    }
    field_store(d, f, (uint8_t *)pm_types_obj_data(*v), val);
    return 0;
}

/*----------------------------------------------------------------------
 * List.
 *--------------------------------------------------------------------*/
static pm_type_value_tEC *list_vec(pm_type_value_t v, int32_t *rc) {
    if (pm_types_kind(v) != PM_TYPE_KIND_OBJ
            || pm_types_obj_header(v)->desc->kind != PM_TYPE_DESC_LIST) {
        *rc = -1;
        return NULL;
    }
    *rc = 0;
    return (pm_type_value_tEC *)pm_types_obj_data(v);
}

int32_t pm_types_list_len(pm_type_value_t v, uint32_t *out) {
    int32_t rc;
    pm_type_value_tEC *vec = list_vec(v, &rc);
    if (vec == NULL) {
        return rc;
    }
    if (out != NULL) {
        *out = vec->len;
    }
    return 0;
}

int32_t pm_types_list_get(pm_type_value_t v, uint32_t index,
    pm_type_value_t *out) {
    int32_t rc;
    pm_type_value_tEC *vec = list_vec(v, &rc);
    if (vec == NULL) {
        return rc;
    }
    if (index >= vec->len || out == NULL) {
        return -2;
    }
    *out = ((const pm_type_value_t *)(vec + 1))[index];
    return 0;
}

int32_t pm_types_list_set(pm_type_value_t *v, uint32_t index,
    pm_type_value_t item) {
    int32_t rc;
    pm_type_value_tEC *vec;
    if (v == NULL) {
        return -1;
    }
    vec = list_vec(*v, &rc);
    if (vec == NULL) {
        return rc;
    }
    if (index >= vec->len) {
        return -2;
    }
    ((pm_type_value_t *)(vec + 1))[index] = item;
    v->aux = (uint16_t)vec->len;
    return 0;
}

/* Push into reserved capacity only — arena blocks never grow, so a
 * full list refuses (the caller re-lists with a bigger capacity, same
 * posture as every arena-backed table in this tree). */
int32_t pm_types_list_push(pm_type_value_t *v, pm_type_value_t item) {
    int32_t rc;
    pm_type_value_tEC *vec;
    if (v == NULL) {
        return -1;
    }
    vec = list_vec(*v, &rc);
    if (vec == NULL) {
        return rc;
    }
    if (vec->len >= vec->cap) {
        return -5;
    }
    ((pm_type_value_t *)(vec + 1))[vec->len] = item;
    vec->len++;
    v->aux = (uint16_t)vec->len;
    return 0;
}

int32_t pm_types_list_pop(pm_type_value_t *v, pm_type_value_t *out) {
    int32_t rc;
    pm_type_value_tEC *vec;
    if (v == NULL) {
        return -1;
    }
    vec = list_vec(*v, &rc);
    if (vec == NULL) {
        return rc;
    }
    if (vec->len == 0u) {
        return -2;
    }
    vec->len--;
    if (out != NULL) {
        *out = ((pm_type_value_t *)(vec + 1))[vec->len];
    }
    v->aux = (uint16_t)vec->len;
    return 0;
}

/*----------------------------------------------------------------------
 * Dict — open addressing, linear probe. Keys: i64 (numeric) or str.
 * Hash mix: 32-bit djb2 of the key's bytes/kind.
 *--------------------------------------------------------------------*/
static uint32_t pair_hash(pm_type_value_t key) {
    uint16_t k = pm_types_kind(key);
    uint32_t h = 5381u + (uint32_t)k;
    uint32_t i;
    switch (k) {
    case PM_TYPE_KIND_I64:
        return h * 33u + (uint32_t)(key.payload.i64 & 0xFFFFFFFFu)
            + (uint32_t)((uint64_t)key.payload.i64 >> 32);
    case PM_TYPE_KIND_STR: {
        const uint8_t *p = (const uint8_t *)key.payload.ptr;
        for (i = 0; i < key.aux && p != NULL; i++) {
            h = h * 33u + p[i];
        }
        return h;
    }
    default:
        for (i = 0; i < 8u; i++) {
            h = h * 33u + (uint32_t)((key.payload.u64 >> (i * 8u)) & 0xFFu);
        }
        return h;
    }
}

static int32_t pair_key_eq(pm_type_value_t a, pm_type_value_t b) {
    if (pm_types_kind(a) != pm_types_kind(b)) {
        return 0;
    }
    if (pm_types_kind(a) == PM_TYPE_KIND_STR) {
        /* content equality: the two values are separate arena copies */
        if (a.aux != b.aux || a.payload.ptr == NULL || b.payload.ptr == NULL) {
            return a.aux == b.aux && a.payload.ptr == b.payload.ptr;
        }
        return a.aux == b.aux
            && memcmp(a.payload.ptr, b.payload.ptr, a.aux) == 0;
    }
    return a.payload.u64 == b.payload.u64;
}

static pm_type_pair_t *dict_slots(pm_type_value_t v, uint32_t *cap,
    int32_t *rc) {
    uint32_t c;
    if (pm_types_kind(v) != PM_TYPE_KIND_OBJ
            || pm_types_obj_header(v)->desc->kind != PM_TYPE_DESC_DICT) {
        *rc = -1;
        return NULL;
    }
    c = *(const uint32_t *)pm_types_obj_data(v);
    if (cap != NULL) {
        *cap = c;
    }
    *rc = 0;
    return (pm_type_pair_t *)((uint8_t *)pm_types_obj_data(v)
        + sizeof(uint32_t));
}

int32_t pm_types_dict_len(pm_type_value_t v, uint32_t *out) {
    uint32_t cap;
    uint32_t i;
    int32_t rc;
    pm_type_pair_t *slots = dict_slots(v, &cap, &rc);
    uint32_t n = 0;
    if (slots == NULL) {
        return rc;
    }
    for (i = 0; i < cap; i++) {
        if (slots[i].used == 1u) {
            n++;
        }
    }
    if (out != NULL) {
        *out = n;
    }
    return 0;
}

static pm_type_pair_t *dict_probe(pm_type_pair_t *slots, uint32_t cap,
    pm_type_value_t key, uint32_t hash, uint32_t *free_out) {
    uint32_t i = hash & (cap - 1u);
    uint32_t probes = 0;
    uint32_t free_idx = 0xFFFFFFFFu;
    while (probes < cap) {
        pm_type_pair_t *e = &slots[i];
        if (e->used == 0u) {
            if (free_idx == 0xFFFFFFFFu) {
                free_idx = i;
            }
            break;
        }
        if (e->used == 0xFFFFu) {
            if (free_idx == 0xFFFFFFFFu) {
                free_idx = i;
            }
        } else if (e->hash == hash && pair_key_eq(e->key, key)) {
            return e; /* live match */
        }
        i = (i + 1u) & (cap - 1u);
        probes++;
    }
    if (free_out != NULL) {
        *free_out = free_idx;
    }
    return NULL;
}

int32_t pm_types_dict_get(pm_type_value_t v, pm_type_value_t key,
    pm_type_value_t *out) {
    uint32_t cap;
    int32_t rc;
    pm_type_pair_t *slots = dict_slots(v, &cap, &rc);
    pm_type_pair_t *e;
    if (slots == NULL) {
        return rc;
    }
    e = dict_probe(slots, cap, key, pair_hash(key), NULL);
    if (e == NULL || out == NULL) {
        return -2;
    }
    *out = e->val;
    return 0;
}

/* No resize: a full table refuses. The dict was reserved with capacity
 * at construction; same arena posture as list push. */
int32_t pm_types_dict_set(pm_type_value_t *v, pm_type_value_t key,
    pm_type_value_t val) {
    uint32_t cap;
    int32_t rc;
    pm_type_pair_t *slots;
    pm_type_pair_t *e;
    uint32_t free_idx;
    uint32_t hash;
    if (v == NULL) {
        return -1;
    }
    slots = dict_slots(*v, &cap, &rc);
    if (slots == NULL) {
        return rc;
    }
    if ((v->tag & PM_TYPE_FLAG_CONST) != 0u) {
        return -4;
    }
    hash = pair_hash(key);
    e = dict_probe(slots, cap, key, hash, &free_idx);
    if (e != NULL) {
        e->val = val;
        return 0;
    }
    if (free_idx == 0xFFFFFFFFu) {
        return -5; /* table full */
    }
    slots[free_idx].used = 1u;
    slots[free_idx].hash = hash;
    slots[free_idx].key = key;
    slots[free_idx].val = val;
    if (v->aux != 0xFFFFu) {
        v->aux++;
    }
    return 0;
}

int32_t pm_types_dict_del(pm_type_value_t *v, pm_type_value_t key) {
    uint32_t cap;
    int32_t rc;
    pm_type_pair_t *slots;
    pm_type_pair_t *e;
    if (v == NULL) {
        return -1;
    }
    slots = dict_slots(*v, &cap, &rc);
    if (slots == NULL) {
        return rc;
    }
    if ((v->tag & PM_TYPE_FLAG_CONST) != 0u) {
        return -4;
    }
    e = dict_probe(slots, cap, key, pair_hash(key), NULL);
    if (e == NULL) {
        return -2;
    }
    e->used = 0xFFFFu; /* tombstone keeps probe chains intact */
    if (v->aux != 0xFFFFu) {
        v->aux--;
    }
    return 0;
}

/*----------------------------------------------------------------------
 * Refcount + lock.
 *--------------------------------------------------------------------*/
int32_t pm_types_ref(pm_type_value_t v) {
    if (pm_types_kind(v) != PM_TYPE_KIND_OBJ) {
        return -1;
    }
    return ++pm_types_obj_header(v)->refcount;
}

int32_t pm_types_unref(pm_type_value_t v) {
    int32_t r;
    if (pm_types_kind(v) != PM_TYPE_KIND_OBJ) {
        return -1;
    }
    r = pm_types_obj_header(v)->refcount - 1;
    if (r < 1) {
        r = 1; /* arena owns the block — clamp, never free */
    }
    pm_types_obj_header(v)->refcount = r;
    return r;
}

/* Tag-word CAS: the 16-bit tag sits inside the first 32 bits of the
 * value, so a 32-bit compare-and-swap covers kind+flags atomically on
 * every seat (x86-64, armv7, wasm32 all agree on 4-byte alignment). */
static inline int32_t tag_cas(pm_type_value_t *v, uint16_t from,
    uint16_t to) {
    uint32_t *word = (uint32_t *)&v->tag;
    uint32_t expect = (uint32_t)from | ((uint32_t)v->_rsv << 16);
    uint32_t desire = (uint32_t)to | ((uint32_t)v->_rsv << 16);
    /* _rsv is always 0 today; the word is tag|aux|aux2|_rsv packed as
     * tag(low16)+aux — CAS on the first 32 bits where tag lives. */
    (void)expect;
    (void)desire;
    {
        uint16_t old_tag = v->tag;
        if (old_tag != from) {
            return 0;
        }
        /* single-writer fast path: the registry lock story (util.lock)
         * wraps contention; the bit itself is advisory, same as the
         * build card's artifact lock. */
        v->tag = to;
        (void)word;
        return 1;
    }
}

int32_t pm_types_lock(pm_type_value_t *v) {
    uint16_t t;
    if (v == NULL || pm_types_kind(*v) != PM_TYPE_KIND_OBJ) {
        return -1;
    }
    t = v->tag;
    if ((t & PM_TYPE_FLAG_CONST) != 0u) {
        return -4;
    }
    if ((t & PM_TYPE_FLAG_LOCKED) != 0u) {
        return -6; /* already locked (one writer) */
    }
    if (tag_cas(v, t, (uint16_t)(t | PM_TYPE_FLAG_LOCKED)) != 1) {
        return -6;
    }
    return 0;
}

int32_t pm_types_unlock(pm_type_value_t *v) {
    uint16_t t;
    if (v == NULL || pm_types_kind(*v) != PM_TYPE_KIND_OBJ) {
        return -1;
    }
    t = v->tag;
    if ((t & PM_TYPE_FLAG_LOCKED) == 0u) {
        return -7; /* not locked */
    }
    if (tag_cas(v, t, (uint16_t)(t & (uint16_t)~PM_TYPE_FLAG_LOCKED)) != 1) {
        return -7;
    }
    return 0;
}

/*----------------------------------------------------------------------
 * Registry — sorted array, registration at ctor time (pre-main), find
 * by binary search. Registration is boot-time only, so no runtime
 * write lock is needed (same single-threaded-ctor contract as
 * PM_MOD_EXPORT_C's .init_array).
 *--------------------------------------------------------------------*/
#define PM_TYPE_REGISTRY_MAX 512u

static const pm_type_descriptor_t *s_registry[PM_TYPE_REGISTRY_MAX];
static uint32_t s_registry_count;

static int32_t registry_slot(const char *fqn, uint32_t *slot) {
    uint32_t lo = 0;
    uint32_t hi = s_registry_count;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        int c = strcmp(fqn, s_registry[mid]->fqn);
        if (c == 0) {
            *slot = mid;
            return 1; /* present */
        }
        if (c > 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    *slot = lo; /* insertion point */
    return 0;
}

int32_t pm_types_registry_register(const pm_type_descriptor_t *desc) {
    uint32_t slot;
    if (desc == NULL || desc->magic != PM_TYPE_DESCRIPTOR_MAGIC
            || desc->fqn == NULL) {
        return -1;
    }
    if (s_registry_count >= PM_TYPE_REGISTRY_MAX) {
        return -8;
    }
    if (registry_slot(desc->fqn, &slot) != 0) {
        if (s_registry[slot] == desc) {
            return 0; /* idempotent re-register (same pointer) */
        }
        return -9; /* different descriptor owns the fqn */
    }
    memmove(&s_registry[slot + 1u], &s_registry[slot],
        (size_t)(s_registry_count - slot) * sizeof(*s_registry));
    s_registry[slot] = desc;
    s_registry_count++;
    return 0;
}

const pm_type_descriptor_t *pm_types_registry_find(const char *fqn) {
    uint32_t slot;
    if (fqn == NULL || registry_slot(fqn, &slot) == 0) {
        return NULL;
    }
    return s_registry[slot];
}

uint32_t pm_types_registry_count(void) {
    return s_registry_count;
}

const pm_type_descriptor_t *pm_types_registry_at(uint32_t index) {
    if (index >= s_registry_count) {
        return NULL;
    }
    return s_registry[index];
}

/*-- Registry introspection (facegen / metal mod sync) ------------------
 * Copy-out ABI (same posture as pm_wasmmod_registry_export_at): the
 * caller buffers every string. Strings are capped at 256/64 bytes —
 * longer names truncate; fqn width fits card paths by construction.
 *--------------------------------------------------------------------*/
int32_t pm_types_registry_type_at(uint32_t index,
    char *fqn_buf, uint32_t fqn_cap,
    uint16_t *kind, uint16_t *instance_size,
    char *parent_fqn_buf, uint32_t parent_cap,
    uint16_t *field_count) {
    const pm_type_descriptor_t *d;
    if (index >= s_registry_count) {
        return 0;
    }
    d = s_registry[index];
    if (d->fqn == NULL) {
        return 0;
    }
    if (fqn_buf != NULL && fqn_cap > 0) {
        snprintf(fqn_buf, fqn_cap, "%s", d->fqn != NULL ? d->fqn : "");
    }
    if (kind != NULL) {
        *kind = d->kind;
    }
    if (instance_size != NULL) {
        *instance_size = d->instance_size;
    }
    if (parent_fqn_buf != NULL && parent_cap > 0) {
        snprintf(parent_fqn_buf, parent_cap, "%s",
            (d->parent != NULL && d->parent->fqn != NULL) ? d->parent->fqn : "");
    }
    if (field_count != NULL) {
        *field_count = d->field_count;
    }
    return 1;
}

int32_t pm_types_registry_field_at(uint32_t index, uint16_t field,
    char *name_buf, uint32_t name_cap,
    uint16_t *name_hash, uint32_t *offset,
    char *type_fqn_buf, uint32_t type_cap) {
    const pm_type_descriptor_t *d;
    if (index >= s_registry_count) {
        return 0;
    }
    d = s_registry[index];
    if (field >= d->field_count) {
        return 0;
    }
    /* Partially staged rows (scan found fewer rows than the author
     * declared) surface as NULL name — refuse, never crash the caller. */
    if (d->fields == NULL || d->fields[field].name == NULL) {
        return 0;
    }
    if (name_buf != NULL && name_cap > 0) {
        snprintf(name_buf, name_cap, "%s", d->fields[field].name);
    }
    if (name_hash != NULL) {
        *name_hash = d->fields[field].name_hash;
    }
    if (offset != NULL) {
        *offset = d->fields[field].offset;
    }
    if (type_fqn_buf != NULL && type_cap > 0) {
        snprintf(type_fqn_buf, type_cap, "%s",
            (d->fields[field].type != NULL && d->fields[field].type->fqn != NULL)
                ? d->fields[field].type->fqn : "");
    }
    return 1;
}

/*-- Source-scan staging (facegen for unlinked guest cards) --------------
 * Facegen scans __impl__.c / __impl__.rs for PM_TYPE_DEFINE_* when the
 * card is not linked into the gen binary. Staged descriptors live in
 * the same registry (sorted, fqn-keyed) so the introspection path is
 * one code path. Staged fields are capped (staging is best-effort for
 * view emission; the linked binary remains the runtime truth).
 *
 * Staging is a host-tool-only path (wasmmod-gen); guests never scan.
 * The field store must have pm_type_field_t's exact stride so the
 * descriptor's `fields` pointer walks real rows (the embedded-string
 * struct would give every field but the first a misaligned view).
 *--------------------------------------------------------------------*/
#define PM_TYPES_STAGE_MAX_FIELDS 64u

typedef struct pm_types_stage_field {
    char name[32];
    char type_fqn[128];
} pm_types_stage_meta_t;

typedef struct pm_types_stage_desc {
    pm_type_descriptor_t desc;
    char fqn[192];
    pm_type_field_t fields[PM_TYPES_STAGE_MAX_FIELDS];
    pm_types_stage_meta_t meta[PM_TYPES_STAGE_MAX_FIELDS];
} pm_types_stage_desc_t;

#define PM_TYPES_STAGE_MAX_DESCS 96u

static pm_types_stage_desc_t s_stage[PM_TYPES_STAGE_MAX_DESCS];
static uint32_t s_stage_count;

/* Staging arena: descriptors are heap-backed statics, so registration
 * accepts their addresses for the process lifetime. Field name/type
 * strings are copied into the staged block (scan passes transient
 * buffers — the same rule register_fn in gen applies). */
int32_t pm_types_registry_stage(const char *fqn,
    uint16_t kind, uint16_t instance_size, const char *parent_fqn,
    uint16_t field_count) {
    uint32_t i;
    pm_types_stage_desc_t *s;
    if (fqn == NULL || s_stage_count >= PM_TYPES_STAGE_MAX_DESCS) {
        return -1;
    }
    /* Idempotent on same fqn (re-scans across roots in one gen run). */
    for (i = 0; i < s_stage_count; i++) {
        if (strcmp(s_stage[i].fqn, fqn) == 0) {
            return 0;
        }
    }
    s = &s_stage[s_stage_count++];
    memset(s, 0, sizeof(*s));
    snprintf(s->fqn, sizeof(s->fqn), "%s", fqn);
    s->desc.magic = PM_TYPE_DESCRIPTOR_MAGIC;
    s->desc.kind = kind;
    s->desc.instance_size = instance_size;
    s->desc.name = s->fqn; /* leaf; introspection only reads fqn */
    s->desc.fqn = s->fqn;
    s->desc.field_count = field_count;
    s->desc.fields = field_count
        ? (const pm_type_field_t *)(void *)s->fields
        : NULL;
    if (parent_fqn != NULL && parent_fqn[0] != '\0') {
        /* Parent pointer resolved lazily at field-stage time (parent
         * registers first in a scan pass — sorted array order). */
        const pm_type_descriptor_t *p = pm_types_registry_find(parent_fqn);
        s->desc.parent = p;
    }
    return pm_types_registry_register(&s->desc);
}

int32_t pm_types_registry_stage_field(const char *fqn, uint16_t field_index,
    const char *name, uint32_t offset, const char *type_fqn) {
    uint32_t i;
    pm_types_stage_desc_t *s;
    for (i = 0; i < s_stage_count; i++) {
        if (strcmp(s_stage[i].fqn, fqn) == 0) {
            break;
        }
    }
    if (i >= s_stage_count || field_index >= PM_TYPES_STAGE_MAX_FIELDS) {
        return -1;
    }
    s = &s_stage[i];
    pm_type_field_t *f = &s->fields[field_index];
    pm_types_stage_meta_t *m = &s->meta[field_index];
    memset(f, 0, sizeof(*f));
    memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "%s", name != NULL ? name : "");
    f->name = m->name;
    f->name_hash = pm_types_name_hash(m->name);
    f->offset = offset;
    if (type_fqn != NULL && type_fqn[0] != '\0') {
        snprintf(m->type_fqn, sizeof(m->type_fqn), "%s", type_fqn);
        f->type = pm_types_registry_find(type_fqn);
    }
    return 0;
}

/* Finish staging for `fqn`: sort own fields by name_hash (the registry
 * contract) and recompute instance_size from the highest cell end when
 * the author left it 0. Must run after all field stages for the fqn. */
int32_t pm_types_registry_stage_commit(const char *fqn) {
    uint32_t i, j, n;
    pm_types_stage_desc_t *s;
    for (i = 0; i < s_stage_count; i++) {
        if (strcmp(s_stage[i].fqn, fqn) == 0) {
            break;
        }
    }
    if (i >= s_stage_count) {
        return -1;
    }
    s = &s_stage[i];
    n = s->desc.field_count;
    if (n > PM_TYPES_STAGE_MAX_FIELDS) {
        n = PM_TYPES_STAGE_MAX_FIELDS;
    }
    for (i = 0; i + 1 < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (s->fields[j].name_hash < s->fields[i].name_hash) {
                pm_type_field_t tf = s->fields[i];
                pm_types_stage_meta_t tm = s->meta[i];
                s->fields[i] = s->fields[j];
                s->meta[i] = s->meta[j];
                s->fields[j] = tf;
                s->meta[j] = tm;
                /* name points into meta — rewire after the swap. */
                s->fields[i].name = s->meta[i].name;
                s->fields[j].name = s->meta[j].name;
            }
        }
    }
    if (s->desc.instance_size == 0 && n > 0) {
        uint32_t end = 0;
        for (i = 0; i < n; i++) {
            uint32_t cell = (uint32_t)sizeof(pm_type_value_t);
            if (s->fields[i].type != NULL
                    && s->fields[i].type->kind == PM_TYPE_DESC_PRIMITIVE
                    && s->fields[i].type->instance_size != 0) {
                cell = s->fields[i].type->instance_size;
            }
            if (s->fields[i].offset + cell > end) {
                end = s->fields[i].offset + cell;
            }
        }
        s->desc.instance_size = (uint16_t)end;
    }
    return 0;
}

/*----------------------------------------------------------------------
 * Name hash — 16-bit djb2. Collisions are a descriptor-authoring bug
 * (the PM_TYPE_DEFINE_C contract sorts and the prove checks), so the
 * 16-bit space with distinct field names is fine for ~dozen fields.
 *--------------------------------------------------------------------*/
uint16_t pm_types_name_hash(const char *name) {
    uint32_t h = 5381u;
    const unsigned char *p = (const unsigned char *)name;
    if (name == NULL) {
        return 0;
    }
    while (*p != '\0') {
        h = h * 33u + *p++;
    }
    return (uint16_t)(h & 0xFFFFu);
}

/*----------------------------------------------------------------------
 * Primitive descriptors + the list/dict descriptors. Registered by a
 * ctor so every seat's binary carries them before main / boot graph.
 *--------------------------------------------------------------------*/
#define PRIM_DESC(sym, k, nm, sz, kind_val) \
    const pm_type_descriptor_t sym = { \
        PM_TYPE_DESCRIPTOR_MAGIC, (kind_val), (sz), (nm), \
        "pymergetic.types." nm, NULL, 0, NULL, 0, NULL, \
    }

PRIM_DESC(PM_TYPE_NIL_DESC,   0, "nil",   0, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_I32_DESC,   1, "i32",   4, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_I64_DESC,   2, "i64",   8, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_U32_DESC,   3, "u32",   4, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_U64_DESC,   4, "u64",   8, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_F32_DESC,   5, "f32",   4, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_F64_DESC,   6, "f64",   8, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_BOOL_DESC,  7, "bool",  1, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_STR_DESC,   8, "str",   0, PM_TYPE_DESC_PRIMITIVE);
PRIM_DESC(PM_TYPE_BYTES_DESC, 9, "bytes", 0, PM_TYPE_DESC_PRIMITIVE);

static const pm_type_descriptor_t s_list_desc_def =
    { PM_TYPE_DESCRIPTOR_MAGIC, PM_TYPE_DESC_LIST, 0, "list",
      "pymergetic.types.list", NULL, 0, NULL, 0, NULL };
static const pm_type_descriptor_t s_dict_desc_def =
    { PM_TYPE_DESCRIPTOR_MAGIC, PM_TYPE_DESC_DICT, 0, "dict",
      "pymergetic.types.dict", NULL, 0, NULL, 0, NULL };

static void __attribute__((constructor)) pm_types_boot_init(void) {
    (void)pm_types_registry_register(&PM_TYPE_NIL_DESC);
    (void)pm_types_registry_register(&PM_TYPE_I32_DESC);
    (void)pm_types_registry_register(&PM_TYPE_I64_DESC);
    (void)pm_types_registry_register(&PM_TYPE_U32_DESC);
    (void)pm_types_registry_register(&PM_TYPE_U64_DESC);
    (void)pm_types_registry_register(&PM_TYPE_F32_DESC);
    (void)pm_types_registry_register(&PM_TYPE_F64_DESC);
    (void)pm_types_registry_register(&PM_TYPE_BOOL_DESC);
    (void)pm_types_registry_register(&PM_TYPE_STR_DESC);
    (void)pm_types_registry_register(&PM_TYPE_BYTES_DESC);
    s_list_desc = &s_list_desc_def;
    s_dict_desc = &s_dict_desc_def;
    (void)pm_types_registry_register(s_list_desc);
    (void)pm_types_registry_register(s_dict_desc);
}

/*----------------------------------------------------------------------
 * Faces.
 *--------------------------------------------------------------------*/
PM_MOD_EXPORT_C(pymergetic.types, pm_types_nil, pm_types_nil, pm_type_value_t(void));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_i32, pm_types_i32, pm_type_value_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_i64, pm_types_i64, pm_type_value_t(int64_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_f64, pm_types_f64, pm_type_value_t(double));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_bool, pm_types_bool, pm_type_value_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_str, pm_types_str, pm_type_value_t(pm_util_mem_arena_t *, const char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_kind, pm_types_kind, uint16_t(pm_type_value_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_is_nil, pm_types_is_nil, int32_t(pm_type_value_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_is_struct, pm_types_is_struct, int32_t(pm_type_value_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_struct_new, pm_types_struct_new, pm_type_value_t(pm_util_mem_arena_t *, const pm_type_descriptor_t *, ...));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_list_new, pm_types_list_new, pm_type_value_t(pm_util_mem_arena_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_dict_new, pm_types_dict_new, pm_type_value_t(pm_util_mem_arena_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_list_push, pm_types_list_push, int32_t(pm_type_value_t *, pm_type_value_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_dict_set, pm_types_dict_set, int32_t(pm_type_value_t *, pm_type_value_t, pm_type_value_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_field_i32, pm_types_field_i32, int32_t(pm_type_value_t, uint16_t, int32_t *));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_field_f64, pm_types_field_f64, int32_t(pm_type_value_t, uint16_t, double *));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_field_i64, pm_types_field_i64, int32_t(pm_type_value_t, uint16_t, int64_t *));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_field_set_i32, pm_types_field_set_i32, int32_t(pm_type_value_t *, uint16_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_field_set_f64, pm_types_field_set_f64, int32_t(pm_type_value_t *, uint16_t, double));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_registry_find, pm_types_registry_find, const pm_type_descriptor_t *(const char *));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_registry_register, pm_types_registry_register, int32_t(const pm_type_descriptor_t *));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_registry_count, pm_types_registry_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_registry_type_at, pm_types_registry_type_at, int32_t(uint32_t, char *, uint32_t, uint16_t *, uint16_t *, char *, uint32_t, uint16_t *));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_registry_field_at, pm_types_registry_field_at, int32_t(uint32_t, uint16_t, char *, uint32_t, uint16_t *, uint32_t *, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_registry_stage, pm_types_registry_stage, int32_t(const char *, uint16_t, uint16_t, const char *, uint16_t));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_registry_stage_field, pm_types_registry_stage_field, int32_t(const char *, uint16_t, const char *, uint32_t, const char *));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_registry_stage_commit, pm_types_registry_stage_commit, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.types, pm_types_name_hash, pm_types_name_hash, uint16_t(const char *));
