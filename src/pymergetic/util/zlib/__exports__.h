/* pymergetic.util.zlib — what this module provides. Thin one-shot wrapper
 * over lib/uzlib (already vendored by upy; not vendored again here). Raw
 * deflate only (no zlib/gzip header) — matches what pack compression
 * actually needs; add header variants later if something needs them. */
#ifndef PYMERGETIC_UTIL_ZLIB_EXPORT_H
#define PYMERGETIC_UTIL_ZLIB_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#include "src/pymergetic/util/zlib/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-shot raw-deflate decompress: src -> dst. Whole output must fit in
 * dst_cap (no streaming) — that's what lets us skip a ring buffer and
 * back-reference straight into dst itself. Returns bytes written (>= 0) or
 * a PM_UTIL_ZLIB_ERR_* code. */
int32_t pm_util_zlib_inflate(const void *src, size_t src_len,
    void *dst, size_t dst_cap);

/* One-shot raw-deflate compress: src -> dst, one block, no header/trailer.
 * hist_scratch is caller-owned working memory for the LZ77 match window
 * (bigger => better matches on large inputs; hist_scratch_len also caps
 * the match window, it does not need to be src_len). Returns bytes written
 * (>= 0) or a PM_UTIL_ZLIB_ERR_* code. */
int32_t pm_util_zlib_deflate(const void *src, size_t src_len,
    void *dst, size_t dst_cap,
    void *hist_scratch, size_t hist_scratch_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_ZLIB_EXPORT_H */
