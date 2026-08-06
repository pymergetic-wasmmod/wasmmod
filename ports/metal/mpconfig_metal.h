/*
 * Metal image defaults when hosting metalpython + wasmmod (upy-compatible).
 *
 * Include from the Metal/metalpython port after mpconfigport.h basics:
 *
 *   #include "extmod/wasmmod/ports/metal/mpconfig_metal.h"
 *
 * Or copy the macros into the port. Does not force MICROPY_PY_WASM on.
 */
#ifndef MICROPY_INCLUDED_WASMMOD_PORTS_METAL_MPCONFIG_H
#define MICROPY_INCLUDED_WASMMOD_PORTS_METAL_MPCONFIG_H

/* One heap: Metal TLSF — override these to pm_metal_mem_* in the real image. */
#ifndef MICROPY_WASM_MALLOC
/* leave unset → wasmmod default allocator until Metal wires TLSF */
#endif

/* GC / upy scheduler DEAD — Metal runners + _async_bridge own concurrency. */
#ifndef MICROPY_ENABLE_GC
#define MICROPY_ENABLE_GC (0)
#endif
#ifndef MICROPY_ENABLE_SCHEDULER
#define MICROPY_ENABLE_SCHEDULER (0)
#endif

/* Prefer event REPL only if the Metal shell uses it; unix stays 0. */
#ifndef MICROPY_REPL_EVENT_DRIVEN
#define MICROPY_REPL_EVENT_DRIVEN (0)
#endif

/*
 * Optional: default I/O ops symbol (stub DECLINEs → VFS/HTTP until Metal
 * replaces fetch/probe with park-to-completion).
 *
 *   #include "extmod/wasmmod/ports/metal/io_ops.h"
 *   #define MICROPY_WASM_IO_OPS mp_wasm_metal_io_ops
 */

#endif /* MICROPY_INCLUDED_WASMMOD_PORTS_METAL_MPCONFIG_H */
