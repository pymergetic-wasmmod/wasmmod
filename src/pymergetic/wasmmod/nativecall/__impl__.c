/* pymergetic.wasmmod.nativecall — see __types__.h. */
#include "pymergetic/wasmmod/nativecall/__types__.h"

#include <stddef.h>

/* The faces this card calls, spelled here rather than taken from the generated
 * umbrellas: this TU is compiled by the crate's build script, which runs before
 * the faces exist. Same posture as util.limits reaching util.mem. */
pm_util_mem_arena_t *pm_util_mem_arena_create(void *base, size_t size);
void pm_util_mem_arena_destroy(pm_util_mem_arena_t *arena);
void *pm_wasmmod_registry_resolve_native(const uint8_t *fqn, uint32_t fqn_len,
    const uint8_t *name, uint32_t name_len);

/* Sized for the in-arena TCC compile (the tccpp token and symbol pools are
 * 2 x 256KB and every table rides the arena) and for the jit.cpp transpile
 * chain, which stays up across lex -> parse -> lower. */
PM_UTIL_LIMIT_C(pm_wasmmod_nativecall_limit_compile, pymergetic.wasmmod.nativecall, compile,
    32u * 1024u * 1024u, 0u, NULL);
PM_UTIL_LIMIT_C(pm_wasmmod_nativecall_limit_cpp, pymergetic.wasmmod.nativecall, cpp,
    64u * 1024u * 1024u, 0u, NULL);
PM_UTIL_LIMIT_C(pm_wasmmod_nativecall_limit_link, pymergetic.wasmmod.nativecall, link,
    64u * 1024u * 1024u, 0u, NULL);
PM_UTIL_LIMIT_C(pm_wasmmod_nativecall_limit_dump, pymergetic.wasmmod.nativecall, dump,
    1024u * 1024u, 0u, NULL);
/* metal.workspace: the paths a materialize builds, and the inflated tar an
 * extract streams through (sized for the prove's fixtures — a real 575 MB
 * tarball goes through the C build card's host-loaded path). */
PM_UTIL_LIMIT_C(pm_wasmmod_nativecall_limit_workspace, pymergetic.wasmmod.nativecall, workspace,
    64u * 1024u, 0u, NULL);
PM_UTIL_LIMIT_C(pm_wasmmod_nativecall_limit_extract, pymergetic.wasmmod.nativecall, extract,
    512u * 1024u, 0u, NULL);

/* metal.boot knows where a seat's memory comes from — pages on a hosted seat,
 * the memmap on a board — and this card asks for it the way the bridge asks for
 * every metal face, through the registry: a seat without metal has no rooms to
 * give, and the call refuses instead of failing to link. */
void *pm_wasmmod_nativecall_span_take(uint32_t want) {
    void *(*fn)(size_t);
    if (want == 0u) {
        return NULL;
    }
    fn = (void *(*)(size_t))pm_wasmmod_registry_resolve_native(
        (const uint8_t *)"pymergetic.metal.boot", 21u,
        (const uint8_t *)"pm_metal_boot_span_take", 23u);
    if (fn == NULL) {
        return NULL;
    }
    return fn((size_t)want);
}

void pm_wasmmod_nativecall_span_give(void *span, uint32_t len) {
    void (*fn)(void *, size_t);
    if (span == NULL) {
        return;
    }
    fn = (void (*)(void *, size_t))pm_wasmmod_registry_resolve_native(
        (const uint8_t *)"pymergetic.metal.boot", 21u,
        (const uint8_t *)"pm_metal_boot_span_give", 23u);
    if (fn != NULL) {
        fn(span, (size_t)len);
    }
}

void pm_wasmmod_nativecall_room_close(pm_wasmmod_nativecall_room_t *room) {
    if (room == NULL) {
        return;
    }
    if (room->arena != NULL) {
        pm_util_mem_arena_destroy(room->arena);
        room->arena = NULL;
    }
    pm_wasmmod_nativecall_span_give(room->span, (uint32_t)room->len);
    room->span = NULL;
    room->len = 0;
}

pm_util_mem_arena_t *pm_wasmmod_nativecall_room_open(pm_wasmmod_nativecall_room_t *room,
    uint32_t want) {
    if (room == NULL) {
        return NULL;
    }
    pm_wasmmod_nativecall_room_close(room);
    room->span = pm_wasmmod_nativecall_span_take(want);
    if (room->span == NULL) {
        return NULL;
    }
    room->len = (size_t)want;
    room->arena = pm_util_mem_arena_create(room->span, (size_t)want);
    if (room->arena == NULL) {
        pm_wasmmod_nativecall_room_close(room);
    }
    return room->arena;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.wasmmod.nativecall, pm_wasmmod_nativecall_span_take, pm_wasmmod_nativecall_span_take, void *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.nativecall, pm_wasmmod_nativecall_span_give, pm_wasmmod_nativecall_span_give, void(void *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.nativecall, pm_wasmmod_nativecall_room_open, pm_wasmmod_nativecall_room_open, pm_util_mem_arena_t *(pm_wasmmod_nativecall_room_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.nativecall, pm_wasmmod_nativecall_room_close, pm_wasmmod_nativecall_room_close, void(pm_wasmmod_nativecall_room_t *));
