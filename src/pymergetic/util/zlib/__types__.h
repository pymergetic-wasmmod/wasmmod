/* pymergetic.util.zlib — shared ABI shapes. See SOURCETREE.md "Faces". */
#ifndef PYMERGETIC_UTIL_ZLIB_TYPES_H
#define PYMERGETIC_UTIL_ZLIB_TYPES_H

#define PM_UTIL_ZLIB_OK 0
/* dst_cap was too small to hold the fully decompressed output. */
#define PM_UTIL_ZLIB_ERR_NOSPACE (-1)
/* src wasn't a valid raw-deflate stream (or was truncated). */
#define PM_UTIL_ZLIB_ERR_DATA (-2)

#endif /* PYMERGETIC_UTIL_ZLIB_TYPES_H */
