/* Mode A simulator io_ops (see io_ops.c, HOST_ASYNC.md). */
#ifndef MP_WASM_SIM_IO_OPS_H_
#define MP_WASM_SIM_IO_OPS_H_

#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

extern mp_wasm_io_ops_t mp_wasm_sim_io_ops;
void mp_wasm_sim_io_ops_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MP_WASM_SIM_IO_OPS_H_ */
