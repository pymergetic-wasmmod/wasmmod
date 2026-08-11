/* pymergetic.wasmmod.loader — umbrella: the canonical include for this
 * module, matching path == module. #include "loader/__exports__.h"
 * directly still compiles, but that reaches past this module's real
 * path into its internal face layout — depend on this file, not that
 * one. */
#ifndef PYMERGETIC_WASMMOD_LOADER_H
#define PYMERGETIC_WASMMOD_LOADER_H

#include "src/pymergetic/wasmmod/loader/__exports__.h" // IWYU pragma: export

#endif /* PYMERGETIC_WASMMOD_LOADER_H */
