/* pymergetic.wasmmod.nativecall — the rooms are knobs a seat can find, and a
 * room nobody can give memory for refuses rather than pretends. */
#include "pymergetic/wasmmod/nativecall.h"
#include "pymergetic/util/limits.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "wasmmod.nativecall test: %s\n", why);
    return 1;
}

/* Every room this bridge opens is one knob under this card, by leaf name. */
static int32_t case_knobs(void) {
    static const char *const leaves[] = { "compile", "cpp", "dump", "extract",
        "link", "workspace" };
    const char *mod = "pymergetic.wasmmod.nativecall";
    uint32_t i;

    if (pm_util_limits_count_of(mod) != 6u) {
        return fail("the bridge's rooms are not six knobs under this card");
    }
    for (i = 0; i < 6u; i++) {
        int32_t k = pm_util_limits_find_of(mod, leaves[i]);
        if (k < 0) {
            return fail("a room is missing its knob");
        }
        if (pm_util_limits_soft(k) == 0u || pm_util_limits_soft(k) != pm_util_limits_default(k)) {
            return fail("a room's knob does not read back what it shipped with");
        }
        /* Listing order is by name, so the nth of this card is the nth leaf. */
        if (pm_util_limits_nth_of(mod, (int32_t)i) != k) {
            return fail("a card's knobs do not list in name order");
        }
        if (strcmp(pm_util_limits_module(k), mod) != 0
            || strcmp(pm_util_limits_leaf(k), leaves[i]) != 0) {
            return fail("a room's knob does not say where it belongs");
        }
    }
    /* The seat moves them: a smaller link room is a seat's decision. */
    {
        int32_t k = pm_util_limits_find_of(mod, "link");
        uint32_t was = pm_util_limits_soft(k);
        if (pm_util_limits_set_at(k, 1024u * 1024u) != 0
            || pm_wasmmod_nativecall_limit_link.soft != 1024u * 1024u) {
            return fail("the card did not see its knob move");
        }
        if (pm_util_limits_reset_at(k) != 0 || pm_util_limits_soft(k) != was) {
            return fail("reset");
        }
    }
    return 0;
}

/* No metal on this seat means no span to hand out, and a room that cannot be
 * given memory is NULL, not an arena over nothing. */
static int32_t case_room_without_seat(void) {
    pm_wasmmod_nativecall_room_t room = { NULL, NULL, 0 };
    if (pm_wasmmod_nativecall_span_take(0u) != NULL) {
        return fail("a room of nothing was handed out");
    }
    if (pm_wasmmod_nativecall_room_open(NULL, 4096u) != NULL) {
        return fail("a room with nowhere to live opened");
    }
    /* Whatever this seat answers, closing an unopened room is quiet and leaves
     * nothing behind. */
    pm_wasmmod_nativecall_room_close(&room);
    if (room.arena != NULL || room.span != NULL || room.len != 0) {
        return fail("closing an empty room left something in it");
    }
    return 0;
}

static int32_t pm_wasmmod_nativecall_tests(void) {
    int32_t st = case_knobs();
    if (st == 0) {
        st = case_room_without_seat();
    }
    return st;
}

PM_MOD_TEST_C(pymergetic.wasmmod.nativecall, tests, pm_wasmmod_nativecall_tests);
