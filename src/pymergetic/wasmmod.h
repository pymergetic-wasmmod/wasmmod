/* pymergetic.wasmmod — umbrella for the `wasmmod` package (path == module).
 * Aggregates the direct child umbrellas so a C consumer reaches any
 * wasmmod.card through one canonical include that matches the module's real
 * path. (The generated face for this node is intentionally empty — see
 * SOURCETREE.md "Empty umbrella `pymergetic.wasmmod` still skips" — but the
 * hand-written umbrella still routes to the real cards below it.) */
#ifndef PYMERGETIC_WASMMOD_H
#define PYMERGETIC_WASMMOD_H

#include "pymergetic/wasmmod/api.h" /* IWYU pragma: export */
#include "pymergetic/wasmmod/boot.h" /* IWYU pragma: export */
#include "pymergetic/wasmmod/io.h" /* IWYU pragma: export */
#include "pymergetic/wasmmod/loader.h" /* IWYU pragma: export */
#include "pymergetic/wasmmod/net.h" /* IWYU pragma: export */
#include "pymergetic/wasmmod/pyexport.h" /* IWYU pragma: export */
#include "pymergetic/wasmmod/registry.h" /* IWYU pragma: export */
#include "pymergetic/wasmmod/verify.h" /* IWYU pragma: export */

#endif /* PYMERGETIC_WASMMOD_H */
