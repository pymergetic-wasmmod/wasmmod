/* pymergetic.wasmmod.io — umbrella (path == module). */
#ifndef PYMERGETIC_WASMMOD_IO_H
#define PYMERGETIC_WASMMOD_IO_H

#include "pymergetic/wasmmod/io/__exports__.h" /* IWYU pragma: export */

/* 1 if io_set already ran. host_boot must not wipe a Metal table. */
int pm_wasmmod_io_is_set(void);

#endif /* PYMERGETIC_WASMMOD_IO_H */
