/* pymergetic.util.zlib — umbrella: the canonical include for this module
 * (recombines types + export), matching path == module. #include
 * "zlib/__exports__.h" directly still compiles, but that reaches past
 * this module's real path into its internal face layout — depend on
 * this file, not that one. */
#ifndef PYMERGETIC_UTIL_ZLIB_H
#define PYMERGETIC_UTIL_ZLIB_H

#include "src/pymergetic/util/zlib/__exports__.h" // IWYU pragma: export

#endif /* PYMERGETIC_UTIL_ZLIB_H */
