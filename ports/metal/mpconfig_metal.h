/*
 * Metal image defaults when hosting metalpython + wasmmod.
 *
 * Include from the Metal/metalpython port after mpconfigport.h basics:
 *
 *   #include "extmod/wasmmod/ports/metal/mpconfig_metal.h"
 *
 * Or copy the macros into the port. Does not force MICROPY_PY_WASM on.
 * Does not compile wasmmod's POSIX+mbedtls HTTP fill — Metal parks in
 * io_ops on pymergetic.metal.net (Metal's mbedtls).
 */
#ifndef PYMERGETIC_WASMMOD_PORTS_METAL_MPCONFIG_H
#define PYMERGETIC_WASMMOD_PORTS_METAL_MPCONFIG_H

/* One heap: pymergetic.util.mem (TLSF + dual-span). Metal holds an arena
 * pointer; it does not wrap a second malloc ABI.
 *
 *   #define MICROPY_WASM_MALLOC(sz)     pm_util_mem_alloc(metal_arena, (sz))
 *   #define MICROPY_WASM_REALLOC(p, sz) pm_util_mem_realloc(metal_arena, (p), (sz))
 *   #define MICROPY_WASM_FREE(p)        pm_util_mem_free(metal_arena, (p))
 *
 * Unset → wasmmod pack/alloc.h stdlib until Metal creates the arena.
 */
#ifndef MICROPY_WASM_MALLOC
/* leave unset */
#endif

/* Py async = Metal async. Firmware includes mpconfig_firmware.h (GC off). */
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
 * Optional compile-time default I/O table (stub DECLINEs until Metal
 * provides strong pm_metal_wasm_io_*). Must call
 * pm_wasmmod_metal_io_ops_init() first — see io_ops.h.
 *
 *   #include "extmod/wasmmod/ports/metal/io_ops.h"
 *   #define MICROPY_WASM_IO_OPS pm_wasmmod_metal_io_ops
 */

#endif /* PYMERGETIC_WASMMOD_PORTS_METAL_MPCONFIG_H */
