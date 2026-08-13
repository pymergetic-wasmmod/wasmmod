/* pymergetic.util.zlib — impl. See __exports__.h for the module contract. */
#include "pymergetic/util/zlib/__exports__.h"

#include <string.h>

/* Path relative to the MicroPython TOP, same convention upstream's own
 * extmod/moddeflate.c uses for this exact library — resolves via the
 * existing "-I../.." in .clangd, no new include path needed. */
#include "lib/uzlib/uzlib.h"

int32_t pm_util_zlib_inflate(const void *src, size_t src_len, void *dst, size_t dst_cap) {
    uzlib_uncomp_t d;
    memset(&d, 0, sizeof(d));
    uzlib_uncompress_init(&d, NULL, 0);

    d.source = (const unsigned char *)src;
    d.source_limit = (const unsigned char *)src + src_len;
    d.dest_start = (unsigned char *)dst;
    d.dest = (unsigned char *)dst;
    d.dest_limit = (unsigned char *)dst + dst_cap;

    int st;
    do {
        st = uzlib_uncompress(&d);
    } while (st == UZLIB_OK);

    if (st != UZLIB_DONE) {
        return (d.dest >= d.dest_limit) ? PM_UTIL_ZLIB_ERR_NOSPACE : PM_UTIL_ZLIB_ERR_DATA;
    }
    return (int32_t)(d.dest - d.dest_start);
}

typedef struct {
    unsigned char *buf;
    size_t cap;
    size_t pos;
    int overflow;
} pm_util_zlib_out_ctx_t;

static void pm_util_zlib_out_byte_cb(void *data, uint8_t byte) {
    pm_util_zlib_out_ctx_t *ctx = (pm_util_zlib_out_ctx_t *)data;
    if (ctx->pos < ctx->cap) {
        ctx->buf[ctx->pos] = byte;
    } else {
        ctx->overflow = 1;
    }
    ctx->pos++;
}

int32_t pm_util_zlib_deflate(const void *src, size_t src_len, void *dst, size_t dst_cap,
    void *hist_scratch, size_t hist_scratch_len) {
    pm_util_zlib_out_ctx_t ctx = { (unsigned char *)dst, dst_cap, 0, 0 };

    uzlib_lz77_state_t lz;
    uzlib_lz77_init(&lz, (uint8_t *)hist_scratch, hist_scratch_len);
    lz.dest_write_data = &ctx;
    lz.dest_write_cb = pm_util_zlib_out_byte_cb;

    uzlib_start_block(&lz);
    uzlib_lz77_compress(&lz, (const uint8_t *)src, (unsigned int)src_len);
    uzlib_finish_block(&lz);

    if (ctx.overflow) {
        return PM_UTIL_ZLIB_ERR_NOSPACE;
    }
    return (int32_t)ctx.pos;
}

/* Same table as PM_MOD_EXPORT_RS! — C language face, next to the muscle. */
#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.util.zlib, pm_util_zlib_inflate, pm_util_zlib_inflate, int32_t(const void *, size_t, void *, size_t));
PM_MOD_EXPORT_C(pymergetic.util.zlib, pm_util_zlib_deflate, pm_util_zlib_deflate, int32_t(const void *, size_t, void *, size_t, void *, size_t));
