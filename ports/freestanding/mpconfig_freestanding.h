/*
 * µPy defaults for a freestanding image that hosts wasmmod — no OS, the host
 * kernel owns the heap and the I/O park. Include after mpconfigport.h basics:
 *
 *   #include "extmod/wasmmod/ports/freestanding/mpconfig_freestanding.h"
 *
 * Or copy the macros into the port. Does not force MICROPY_PY_WASM on, and
 * does not compile wasmmod's POSIX+mbedtls HTTP fill: on these seats the
 * kernel's own net stack answers through io_ops.
 */
#ifndef PYMERGETIC_WASMMOD_PORTS_FREESTANDING_MPCONFIG_H
#define PYMERGETIC_WASMMOD_PORTS_FREESTANDING_MPCONFIG_H

/* No OS under us: wasmmod leaves out its POSIX io fill and modwasmmod.c, and
 * io bytes come from the image heap. See mpconfig_wasm.h. */
#ifndef MICROPY_WASM_FREESTANDING
#define MICROPY_WASM_FREESTANDING (1)
#endif

/* One heap: pymergetic.util.mem (TLSF + dual-span). The kernel holds the arena
 * pointer; it does not wrap a second malloc ABI.
 *
 *   #define MICROPY_WASM_MALLOC(sz)     pm_util_mem_alloc(arena, (sz))
 *   #define MICROPY_WASM_REALLOC(p, sz) pm_util_mem_realloc(arena, (p), (sz))
 *   #define MICROPY_WASM_FREE(p)        pm_util_mem_free(arena, (p))
 *
 * Unset → wasmmod pack/alloc.h stdlib until the kernel creates the arena.
 */
#ifndef MICROPY_WASM_MALLOC
/* leave unset */
#endif

/* Py async is the kernel's async. Firmware layers its own config on top. */
#ifndef MICROPY_ENABLE_GC
#define MICROPY_ENABLE_GC (0)
#endif
#ifndef MICROPY_ENABLE_SCHEDULER
#define MICROPY_ENABLE_SCHEDULER (0)
#endif

#ifndef MICROPY_REPL_EVENT_DRIVEN
#define MICROPY_REPL_EVENT_DRIVEN (0)
#endif

/*
 * Optional compile-time default I/O table. It DECLINEs every URI until the
 * kernel provides strong pm_wasmmod_host_io_* hooks. Must call
 * pm_wasmmod_host_io_ops_init() first — see io_ops.h.
 *
 *   #include "extmod/wasmmod/ports/freestanding/io_ops.h"
 *   #define MICROPY_WASM_IO_OPS pm_wasmmod_host_io_ops
 */

#endif /* PYMERGETIC_WASMMOD_PORTS_FREESTANDING_MPCONFIG_H */
