/* pymergetic.wasmmod.api — umbrella: the canonical include for this module
 * (recombines types + export), matching path == module. #include
 * "api/__exports__.h" directly still compiles, but that reaches past
 * this module's real path into its internal face layout — depend on
 * this file, not that one. */
#ifndef PYMERGETIC_WASMMOD_API_H
#define PYMERGETIC_WASMMOD_API_H

#include "pymergetic/wasmmod/api/__exports__.h" /* IWYU pragma: export */

#endif /* PYMERGETIC_WASMMOD_API_H */
