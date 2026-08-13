/* pymergetic.util.gen — shared ABI shapes. See SOURCETREE.md "Faces". */
#ifndef PYMERGETIC_UTIL_GEN_TYPES_H
#define PYMERGETIC_UTIL_GEN_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_util_gen_vfs_ops {
    /* 1=ok, 0=missing, -1=err. buf==NULL → write size into *inout_len. */
    int32_t (*read)(void *ctx, const uint8_t *path, uint32_t path_len,
                    uint8_t *buf, uint32_t *inout_len);
    /* 0=ok, -1=err */
    int32_t (*write)(void *ctx, const uint8_t *path, uint32_t path_len,
                     const uint8_t *data, uint32_t data_len);
} pm_util_gen_vfs_ops_t;

/* Opaque in-memory sink (heap). Free with pm_util_gen_mem_sink_free. */
typedef struct pm_util_gen_mem_sink pm_util_gen_mem_sink_t;

/* Live µPy __init__.pyi provider: 1=filled out, 0=no py surface, -1=err.
 * buf==NULL → write required size into *inout_len. */
typedef int32_t (*pm_util_gen_py_face_fn)(void *ctx, const uint8_t *fqn, uint32_t fqn_len,
                                         uint8_t *buf, uint32_t *inout_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_GEN_TYPES_H */
