/* pymergetic.util — umbrella for the pep420 `util` namespace (path == module).
 * Aggregates the direct child umbrellas so a C consumer reaches any util.card
 * through one canonical include that matches the module's real path. */
#ifndef PYMERGETIC_UTIL_H
#define PYMERGETIC_UTIL_H

#include "pymergetic/util/gen.h" /* IWYU pragma: export */
#include "pymergetic/util/lock.h" /* IWYU pragma: export */
#include "pymergetic/util/lz4.h" /* IWYU pragma: export */
#include "pymergetic/util/mem.h" /* IWYU pragma: export */
#include "pymergetic/util/mtar.h" /* IWYU pragma: export */
#include "pymergetic/util/pysample.h" /* IWYU pragma: export */
#include "pymergetic/util/version.h" /* IWYU pragma: export */
#include "pymergetic/util/zlib.h" /* IWYU pragma: export */

#endif /* PYMERGETIC_UTIL_H */
