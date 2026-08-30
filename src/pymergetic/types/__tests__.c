/* pymergetic.types — module tests (`__tests__.c`). The prove covers the
 * whole universal-value story: primitives round-trip, structs built
 * through PM_TYPE_DEFINE_C descriptors (fields sorted by name_hash),
 * inheritance (Entity -> Person, parent fields at low offsets), list
 * and dict behaviour inside arena limits, the lock bit, and the
 * registry's find/refuse contract. No .pmdef, no DSL — the types are
 * defined right here in the impl language, same posture as
 * PM_MOD_EXPORT_C. */
#include "pymergetic/types/__exports__.h"
#include "pymergetic/types/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "types: %s\n", why);
    return 1;
}

/*----------------------------------------------------------------------
 * Types under test — defined with the public PM_TYPE_DEFINE_C from
 * pymergetic/types/__types__.h (moved there so any card can author
 * types). Fields sorted by name_hash ascending; the prove asserts the
 * order at runtime so an author who gets it wrong fails the gate,
 * not the search.
 *--------------------------------------------------------------------*/

/* djb2-16 name hashes (compile-time today, verified at runtime):
 *   id 0x7832  created 0x0A3D  name 0x0C46  age 0x5D32
 *   x   0xB61D  y       0xB61E
 * Sorted arrays below: created < id; name < age; x < y. */

/* Entity — the parent. Packed instance data: created:i64 @0, id:i64 @8.
 * Field cells are typed (f->type) so the bytes match a C view struct:
 *   typedef struct { int64_t created; int64_t id; } EntityView; */
static const pm_type_field_t s_entity_fields[] = {
    { 0x0A3D, 0, 0, &PM_TYPE_I64_DESC, "created" },
    { 0x7832, 0, 8, &PM_TYPE_I64_DESC, "id" },
};
PM_TYPE_DEFINE_C(s_entity_desc, "pymergetic.types.Entity",
    PM_TYPE_DESC_STRUCT, 16, NULL, s_entity_fields, 2);

/* Person — child of Entity. Parent cells 0..15, then own packed cells:
 *   typedef struct { int64_t created; int64_t id; int32_t age; uint32_t pad;
 *                    pm_type_value_t name; } PersonView;  // name @24 */
static const pm_type_field_t s_person_fields[] = {
    { 0x0C46, 0, 24, &PM_TYPE_STR_DESC, "name" },
    { 0x5D32, 0, 16, &PM_TYPE_I32_DESC, "age" },
};
PM_TYPE_DEFINE_C(s_person_desc, "pymergetic.types.Person",
    PM_TYPE_DESC_STRUCT, 40, &s_entity_desc, s_person_fields, 2);

/* Point — plain struct, no parent. Packed: x:f64 @0, y:f64 @8.
 *   typedef struct { double x; double y; } PointView; */
static const pm_type_field_t s_point_fields[] = {
    { 0xB61D, 0, 0, &PM_TYPE_F64_DESC, "x" },
    { 0xB61E, 0, 8, &PM_TYPE_F64_DESC, "y" },
};
PM_TYPE_DEFINE_C(s_point_desc, "pymergetic.types.Point",
    PM_TYPE_DESC_STRUCT, 16, NULL, s_point_fields, 2);

/* helpers */
static pm_util_mem_arena_t *s_arena;
static void *s_backing;

static int32_t case_primitives(void) {
    if (pm_types_kind(pm_types_nil()) != PM_TYPE_KIND_NIL) {
        return fail("nil kind");
    }
    if (pm_types_i32(-7).payload.i32 != -7) {
        return fail("i32");
    }
    if (pm_types_i64(0x1234567890LL).payload.i64 != 0x1234567890LL) {
        return fail("i64");
    }
    if (pm_types_u32(4000000000u).payload.u32 != 4000000000u) {
        return fail("u32");
    }
    if (pm_types_u64(0xFFFFFFFFFFFFFFFFull).payload.u64 != 0xFFFFFFFFFFFFFFFFull) {
        return fail("u64");
    }
    if (pm_types_f32(1.5f).payload.f32 != 1.5f) {
        return fail("f32");
    }
    if (pm_types_f64(3.14159).payload.f64 != 3.14159) {
        return fail("f64");
    }
    if (pm_types_bool(42).payload.i32 != 1 || pm_types_bool(0).payload.i32 != 0) {
        return fail("bool");
    }
    if (pm_types_descriptor_of(pm_types_i32(1)) != &PM_TYPE_I32_DESC) {
        return fail("descriptor_of i32");
    }
    if (pm_types_descriptor_of(pm_types_f64(1.0)) != &PM_TYPE_F64_DESC) {
        return fail("descriptor_of f64");
    }
    return 0;
}

static int32_t case_str_bytes(void) {
    pm_type_value_t s = pm_types_str(s_arena, "hello", 5);
    const char *p = NULL;
    uint32_t len = 0;
    if (pm_types_kind(s) != PM_TYPE_KIND_STR || s.aux != 5) {
        return fail("str shape");
    }
    if (s.payload.ptr == NULL || memcmp(s.payload.ptr, "hello", 5) != 0) {
        return fail("str bytes");
    }
    if (((const char *)s.payload.ptr)[5] != '\0') {
        return fail("str NUL");
    }
    {
        pm_type_value_t b = pm_types_bytes(s_arena, (const uint8_t *)"\x01\x02", 2);
        if (pm_types_kind(b) != PM_TYPE_KIND_BYTES || b.aux != 2
                || memcmp(b.payload.ptr, "\x01\x02", 2) != 0) {
            return fail("bytes");
        }
    }
    {
        /* borrowed: no copy, CONST set */
        pm_type_value_t v = pm_types_str_borrowed("static", 6);
        if ((v.tag & PM_TYPE_FLAG_CONST) == 0u
                || v.payload.ptr != (void *)"static") {
            return fail("borrowed");
        }
    }
    /* the field-str read path exercises the same accessors */
    if (pm_types_str(s_arena, NULL, 3).tag != PM_TYPE_KIND_NIL) {
        return fail("str NULL refused");
    }
    p = NULL;
    (void)len;
    return 0;
}

static int32_t case_registry(void) {
    const pm_type_descriptor_t *d;
    uint32_t n;
    uint32_t i;
    if (pm_types_registry_find("pymergetic.types.Point") != &s_point_desc) {
        return fail("find Point");
    }
    d = pm_types_registry_find("pymergetic.types.Person");
    if (d != &s_person_desc || d->parent != &s_entity_desc) {
        return fail("Person parent");
    }
    if (pm_types_registry_find("pymergetic.types.Nope") != NULL) {
        return fail("find miss must be NULL");
    }
    if (pm_types_registry_find(NULL) != NULL) {
        return fail("find NULL refused");
    }
    /* idempotent re-register: same pointer is a no-op, a different
     * descriptor under a live fqn is refused */
    if (pm_types_registry_register(&s_point_desc) != 0) {
        return fail("re-register same");
    }
    if (pm_types_registry_register(&PM_TYPE_I32_DESC) == 0
            && pm_types_registry_find("pymergetic.types.i32") != &PM_TYPE_I32_DESC) {
        return fail("conflict handling");
    }
    /* every descriptor is reachable; fields sorted ascending */
    n = pm_types_registry_count();
    if (n < 12u) { /* 10 primitives + list + dict + our 3 */
        return fail("registry count");
    }
    for (i = 0; i < n; i++) {
        const pm_type_descriptor_t *at = pm_types_registry_at(i);
        uint16_t j;
        if (at == NULL || at->magic != PM_TYPE_DESCRIPTOR_MAGIC) {
            return fail("registry at");
        }
        for (j = 1; j < at->field_count; j++) {
            if (at->fields[j - 1u].name_hash >= at->fields[j].name_hash) {
                return fail("fields not sorted");
            }
        }
        if (i + 1u < n
                && strcmp(at->fqn, pm_types_registry_at(i + 1u)->fqn) >= 0) {
            return fail("registry not sorted");
        }
    }
    if (pm_types_name_hash("id") != 0x7832u
            || pm_types_name_hash("created") != 0x0A3Du) {
        return fail("name hash values");
    }
    return 0;
}

static int32_t case_struct(void) {
    pm_type_value_t p = pm_types_struct_new(s_arena, &s_point_desc,
        0xB61D, pm_types_f64(1.5),
        0xB61E, pm_types_f64(2.5),
        PM_TYPE_FIELD_END);
    double x = 0.0;
    double y = 0.0;
    if (pm_types_kind(p) != PM_TYPE_KIND_OBJ) {
        return fail("point obj");
    }
    if (!pm_types_is_struct(p)) {
        return fail("is_struct");
    }
    if (pm_types_descriptor_of(p) != &s_point_desc) {
        return fail("point identity");
    }
    if (pm_types_field_f64(p, 0xB61D, &x) != 0 || x != 1.5) {
        return fail("point x");
    }
    if (pm_types_field_f64(p, 0xB61E, &y) != 0 || y != 2.5) {
        return fail("point y");
    }
    if (pm_types_field_f64(p, 0x0001, &x) == 0) {
        return fail("unknown field must miss");
    }
    /* typed C view over the same bytes — the zero-copy story */
    {
        const struct { double x; double y; } *view =
            (const struct { double x; double y; } *)pm_types_obj_data(p);
        if (view->x != 1.5 || view->y != 2.5) {
            return fail("typed view");
        }
    }
    /* unknown field in varargs: skipped, not fatal */
    {
        pm_type_value_t q = pm_types_struct_new(s_arena, &s_point_desc,
            0x0001, pm_types_i32(9), /* unknown hash */
            PM_TYPE_FIELD_END);
        if (pm_types_kind(q) != PM_TYPE_KIND_OBJ) {
            return fail("unknown field skipped");
        }
    }
    return 0;
}

static int32_t case_inheritance(void) {
    pm_type_value_t person = pm_types_struct_new(s_arena, &s_person_desc,
        0x7832, pm_types_i64(7),          /* parent: id @8 */
        0x0A3D, pm_types_i64(99),         /* parent: created @0 */
        0x0C46, pm_types_str(s_arena, "ada", 3), /* own: name @24 */
        0x5D32, pm_types_i32(36),         /* own: age @16 */
        PM_TYPE_FIELD_END);
    int64_t id = 0;
    int64_t created = 0;
    int32_t age = 0;
    const char *name = NULL;
    uint32_t name_len = 0;
    if (pm_types_kind(person) != PM_TYPE_KIND_OBJ) {
        return fail("person obj");
    }
    if (!pm_types_is_instance_of(person, &s_entity_desc)) {
        return fail("person is-a Entity");
    }
    if (pm_types_is_instance_of(person, &s_point_desc)) {
        return fail("person is-not-a Point");
    }
    if (pm_types_field_i64(person, 0x7832, &id) != 0 || id != 7) {
        return fail("entity id (parent chain)");
    }
    if (pm_types_field_i64(person, 0x0A3D, &created) != 0 || created != 99) {
        return fail("entity created (parent chain)");
    }
    if (pm_types_field_i32(person, 0x5D32, &age) != 0 || age != 36) {
        return fail("person age");
    }
    if (pm_types_field_str(person, 0x0C46, &name, &name_len) != 0
            || name_len != 3 || strncmp(name, "ada", 3) != 0) {
        return fail("person name");
    }
    /* the packed cells: a typed view overlays the same bytes */
    {
        const struct {
            int64_t created;
            int64_t id;
            int32_t age;
            uint32_t _pad;
            pm_type_value_t name;
        } *view = (const void *)pm_types_obj_data(person);
        if (view->created != 99 || view->id != 7 || view->age != 36) {
            return fail("person typed view");
        }
    }
    return 0;
}

static int32_t case_list(void) {
    pm_type_value_t l = pm_types_list_new(s_arena, 8);
    uint32_t len = 0;
    pm_type_value_t got;
    uint32_t i;
    if (pm_types_kind(l) != PM_TYPE_KIND_OBJ
            || pm_types_obj_header(l)->desc->kind != PM_TYPE_DESC_LIST) {
        return fail("list obj");
    }
    if (pm_types_list_len(l, &len) != 0 || len != 0) {
        return fail("list empty");
    }
    for (i = 0; i < 5u; i++) {
        if (pm_types_list_push(&l, pm_types_i32((int32_t)i * 11)) != 0) {
            return fail("list push");
        }
    }
    if (pm_types_list_len(l, &len) != 0 || len != 5) {
        return fail("list len 5");
    }
    if (pm_types_list_get(l, 3u, &got) != 0 || got.payload.i32 != 33) {
        return fail("list get 3");
    }
    if (pm_types_list_set(&l, 0u, pm_types_i32(5)) != 0
            || pm_types_list_get(l, 0u, &got) != 0 || got.payload.i32 != 5) {
        return fail("list set");
    }
    if (pm_types_list_get(l, 5u, &got) == 0) {
        return fail("list get past end refused");
    }
    if (pm_types_list_pop(&l, &got) != 0 || got.payload.i32 != 44) {
        return fail("list pop");
    }
    if (pm_types_list_len(l, &len) != 0 || len != 4) {
        return fail("list len after pop");
    }
    /* capacity 8, 4 live: 5 more pushes -> one must refuse at 8 */
    for (i = 0; i < 4u; i++) {
        if (pm_types_list_push(&l, pm_types_nil()) != 0) {
            return fail("list refill push");
        }
    }
    if (pm_types_list_push(&l, pm_types_nil()) == 0) {
        return fail("list full must refuse");
    }
    /* wrong kind refuses */
    if (pm_types_list_len(pm_types_i32(1), &len) == 0) {
        return fail("list len on non-list refused");
    }
    return 0;
}

static int32_t case_dict(void) {
    pm_type_value_t d = pm_types_dict_new(s_arena, 8);
    pm_type_value_t got;
    uint32_t len = 0;
    if (pm_types_kind(d) != PM_TYPE_KIND_OBJ
            || pm_types_obj_header(d)->desc->kind != PM_TYPE_DESC_DICT) {
        return fail("dict obj");
    }
    if (pm_types_dict_len(d, &len) != 0 || len != 0) {
        return fail("dict empty");
    }
    if (pm_types_dict_set(&d, pm_types_i64(10), pm_types_str(s_arena, "ten", 3)) != 0) {
        return fail("dict set 10");
    }
    if (pm_types_dict_set(&d, pm_types_i64(20), pm_types_i32(20)) != 0) {
        return fail("dict set 20");
    }
    if (pm_types_dict_set(&d, pm_types_i64(10), pm_types_i32(999)) != 0) {
        return fail("dict overwrite");
    }
    if (pm_types_dict_len(d, &len) != 0 || len != 2) {
        return fail("dict len 2");
    }
    if (pm_types_dict_get(d, pm_types_i64(10), &got) != 0
            || got.payload.i32 != 999) {
        return fail("dict get 10 (overwritten)");
    }
    if (pm_types_dict_get(d, pm_types_i64(20), &got) != 0
            || got.payload.i32 != 20) {
        return fail("dict get 20");
    }
    if (pm_types_dict_get(d, pm_types_i64(30), &got) == 0) {
        return fail("dict get miss");
    }
    if (pm_types_dict_del(&d, pm_types_i64(10)) != 0) {
        return fail("dict del");
    }
    if (pm_types_dict_get(d, pm_types_i64(10), &got) == 0) {
        return fail("dict get after del");
    }
    if (pm_types_dict_len(d, &len) != 0 || len != 1) {
        return fail("dict len after del");
    }
    /* tombstone keeps probe chains intact: 20 still reachable past it */
    if (pm_types_dict_get(d, pm_types_i64(20), &got) != 0) {
        return fail("dict 20 after tombstone");
    }
    /* str keys hash by bytes */
    {
        pm_type_value_t sd = pm_types_dict_new(s_arena, 8);
        if (pm_types_dict_set(&sd, pm_types_str(s_arena, "k1", 2), pm_types_i32(1)) != 0
            || pm_types_dict_set(&sd, pm_types_str(s_arena, "k2", 2), pm_types_i32(2)) != 0) {
            return fail("str key set");
        }
        if (pm_types_dict_get(sd, pm_types_str(s_arena, "k2", 2), &got) != 0
                || got.payload.i32 != 2) {
            return fail("str key get");
        }
        if (pm_types_dict_get(sd, pm_types_str(s_arena, "k3", 2), &got) == 0) {
            return fail("str key miss");
        }
    }
    return 0;
}

static int32_t case_lock(void) {
    pm_type_value_t p = pm_types_struct_new(s_arena, &s_point_desc,
        0xB61D, pm_types_f64(0.0),
        0xB61E, pm_types_f64(0.0),
        PM_TYPE_FIELD_END);
    if (pm_types_lock(&p) != 0) {
        return fail("lock");
    }
    if (pm_types_lock(&p) == 0) {
        return fail("double lock refused");
    }
    /* writes are allowed while locked (the caller holds the write lease) */
    if (pm_types_field_set_f64(&p, 0xB61D, 6.5) != 0) {
        return fail("set while locked");
    }
    if (pm_types_unlock(&p) != 0) {
        return fail("unlock");
    }
    if (pm_types_unlock(&p) == 0) {
        return fail("double unlock refused");
    }
    {
        double x = 0.0;
        if (pm_types_field_f64(p, 0xB61D, &x) != 0 || x != 6.5) {
            return fail("value after lock cycle");
        }
    }
    /* CONST refuses both */
    {
        pm_type_value_t c = pm_types_str_borrowed("x", 1);
        if (pm_types_lock(&c) == 0) {
            return fail("const lock refused");
        }
    }
    /* primitives (no header) refuse */
    {
        pm_type_value_t ip = pm_types_i32(3);
        if (pm_types_lock(&ip) == 0) {
            return fail("primitive lock refused");
        }
    }
    return 0;
}

static int32_t case_refcount(void) {
    pm_type_value_t p = pm_types_struct_new(s_arena, &s_point_desc,
        0xB61D, pm_types_f64(1.0),
        0xB61E, pm_types_f64(1.0),
        PM_TYPE_FIELD_END);
    int32_t r;
    if (pm_types_obj_header(p)->refcount != 1) {
        return fail("initial refcount");
    }
    r = pm_types_ref(p);
    if (r != 2) {
        return fail("ref -> 2");
    }
    r = pm_types_unref(p);
    if (r != 1) {
        return fail("unref -> 1 (clamped, arena owns)");
    }
    if (pm_types_ref(pm_types_i32(1)) != -1) {
        return fail("ref on primitive refused");
    }
    return 0;
}

static int32_t case_null_guards(void) {
    if (pm_types_kind(pm_types_nil()) != PM_TYPE_KIND_NIL) {
        return fail("nil");
    }
    if (pm_types_struct_new(NULL, &s_point_desc, PM_TYPE_FIELD_END).tag
            != PM_TYPE_KIND_NIL) {
        return fail("struct_new NULL arena -> NIL");
    }
    if (pm_types_list_new(NULL, 4).tag != PM_TYPE_KIND_NIL) {
        return fail("list_new NULL arena -> NIL");
    }
    if (pm_types_dict_new(NULL, 4).tag != PM_TYPE_KIND_NIL) {
        return fail("dict_new NULL arena -> NIL");
    }
    if (pm_types_registry_register(NULL) != -1) {
        return fail("register NULL");
    }
    if (pm_types_registry_at(0xFFFFFFu) != NULL) {
        return fail("at past end");
    }
    return 0;
}

/* the arena every case shares: created before any case runs. */
static int32_t pm_types_tests(void) {
    int32_t rc;
    s_backing = malloc(1u << 20);
    if (s_backing == NULL) {
        return fail("backing");
    }
    s_arena = pm_util_mem_arena_create(s_backing, 1u << 20);
    if (s_arena == NULL) {
        free(s_backing);
        return fail("arena");
    }
    rc = case_primitives();
    if (rc == 0) rc = case_str_bytes();
    if (rc == 0) rc = case_registry();
    if (rc == 0) rc = case_struct();
    if (rc == 0) rc = case_inheritance();
    if (rc == 0) rc = case_list();
    if (rc == 0) rc = case_dict();
    if (rc == 0) rc = case_lock();
    if (rc == 0) rc = case_refcount();
    if (rc == 0) rc = case_null_guards();
    pm_util_mem_arena_destroy(s_arena);
    free(s_backing);
    return rc;
}

PM_MOD_TEST_C(pymergetic.types, types, pm_types_tests);
