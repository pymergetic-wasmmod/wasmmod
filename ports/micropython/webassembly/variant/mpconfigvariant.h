#define MICROPY_VARIANT_ENABLE_JS_HOOK (1)

#include "../mpconfig_webassembly.h"

/* Direct use so include-cleaner keeps the header (this file exists to pull it). */
#if !defined(MICROPY_WASM_IO_OPS)
#error "mpconfig_webassembly.h must define MICROPY_WASM_IO_OPS"
#endif
