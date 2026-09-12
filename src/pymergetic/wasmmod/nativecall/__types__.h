/* pymergetic.wasmmod.nativecall — the rooms µPy's native-call bridge works in.
 *
 * A Python call into a card's C or Rust muscle sometimes needs memory of its
 * own to work in: a compile wants a heap for its tables, a link wants the whole
 * card in one span, an extract wants somewhere to inflate. The bridge that
 * makes those calls lives in ports/micropython — glue, not a card — so the
 * sizes it works to used to be numbers in glue, and a seat had nowhere to ask
 * about them.
 *
 * They live here instead. This card is what a room is: how big it may be (a
 * knob per kind of work), where the memory comes from (the seat's own span,
 * through metal.boot), and how it is given back. The bridge holds the rooms and
 * calls in; nothing is reserved until a call opens one.
 */
#ifndef PYMERGETIC_WASMMOD_NATIVECALL_TYPES_H
#define PYMERGETIC_WASMMOD_NATIVECALL_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/util/limits/__types__.h"
#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One room. The caller keeps it — the bridge has one per kind of work, because
 * those flows are sequential — and this card fills it in and empties it. */
typedef struct pm_wasmmod_nativecall_room {
    pm_util_mem_arena_t *arena;
    void *span;
    size_t len; /* the span's length: giving the pages back needs it */
} pm_wasmmod_nativecall_room_t;

/* How big each kind of work may ask for. Every one of them is a default a seat
 * moves: a build host with a big tree wants more, a small seat would rather
 * keep the memory. Read where the room is opened, so the next call gets the new
 * number with nothing to reshape.
 *
 * The compile and cpp rooms are the largest because an in-process compile keeps
 * its tables in the arena, and a link relocates a whole card at once. A seat
 * that will never compile in-process (a board) lowers them at boot rather than
 * carrying a number it cannot honour — nothing is reserved either way. */
extern pm_util_limit_t pm_wasmmod_nativecall_limit_compile;
extern pm_util_limit_t pm_wasmmod_nativecall_limit_cpp;
extern pm_util_limit_t pm_wasmmod_nativecall_limit_link;
extern pm_util_limit_t pm_wasmmod_nativecall_limit_dump;
extern pm_util_limit_t pm_wasmmod_nativecall_limit_workspace;
extern pm_util_limit_t pm_wasmmod_nativecall_limit_extract;

/* `want` bytes of the seat's own memory, page-aligned the way an arena wants
 * its base, and an arena over it. Opening gives the room back first: these
 * arms raise from the middle of a Python flow and a raise never returns, so the
 * next caller is where a room left standing is taken down. NULL when there is
 * no seat to ask or it cannot spare the room. */
pm_util_mem_arena_t *pm_wasmmod_nativecall_room_open(pm_wasmmod_nativecall_room_t *room,
    uint32_t want);
void pm_wasmmod_nativecall_room_close(pm_wasmmod_nativecall_room_t *room);

/* The same memory without an arena over it, for work that wants plain bytes
 * (a dump buffer). Given back with the length it was taken with. */
void *pm_wasmmod_nativecall_span_take(uint32_t want);
void pm_wasmmod_nativecall_span_give(void *span, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_NATIVECALL_TYPES_H */
