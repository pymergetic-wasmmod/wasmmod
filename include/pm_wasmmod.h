/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Umbrella: wasmmod product host API (re-exports domain headers).
 */

#ifndef PM_PM_WASMMOD_H_
#define PM_PM_WASMMOD_H_

#include "pm_common.h" // IWYU pragma: export
#include "pm_wasmmod/version.h" // IWYU pragma: export
#include "pm_wasmmod/runtime.h" // IWYU pragma: export
#include "pm_wasmmod/pack/load.h" // IWYU pragma: export
#include "pm_wasmmod/pack/call.h" // IWYU pragma: export
#include "pm_wasmmod/pack/mem.h" // IWYU pragma: export
#include "pm_wasmmod/host/cookie.h" // IWYU pragma: export
#include "pm_wasmmod/host/handle.h" // IWYU pragma: export
#include "pm_wasmmod/host/call.h" // IWYU pragma: export
#include "pm_wasmmod/host/self.h" // IWYU pragma: export

#include "pm_wasmmod/path/io.h" // IWYU pragma: export
#include "pm_wasmmod/path/cdn.h" // IWYU pragma: export
#include "pm_wasmmod/path/verify.h" // IWYU pragma: export
#include "pm_wasmmod/path/fetch.h" // IWYU pragma: export
#include "pm_wasmmod/path/zlib.h" // IWYU pragma: export
#include "pm_wasmmod/inspect/inspect.h" // IWYU pragma: export
#include "pm_wasmmod/inspect/source.h" // IWYU pragma: export
#include "pm_wasmmod/module.h" // IWYU pragma: export
#include "pm_mod.h" // IWYU pragma: export — µPy module SoT (publish/connect)

#endif /* PM_PM_WASMMOD_H_ */
