/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Umbrella: µPy gut-control API (re-exports domain headers).
 */

#ifndef PM_PM_UPY_H_
#define PM_PM_UPY_H_

#include "pm_common.h" // IWYU pragma: export
#include "pm_upy/features.h" // IWYU pragma: export
#include "pm_upy/init.h" // IWYU pragma: export
#include "pm_upy/mem/heap.h" // IWYU pragma: export
#include "pm_upy/mem/gc.h" // IWYU pragma: export
#include "pm_upy/mem/stack.h" // IWYU pragma: export
#include "pm_upy/loop/step.h" // IWYU pragma: export
#include "pm_upy/loop/sched.h" // IWYU pragma: export
#include "pm_upy/loop/repl.h" // IWYU pragma: export
#include "pm_upy/exec/run.h" // IWYU pragma: export
#include "pm_upy/exec/embed.h" // IWYU pragma: export
#include "pm_upy/exec/pyexec.h" // IWYU pragma: export
#include "pm_upy/exec/compile.h" // IWYU pragma: export
#include "pm_upy/exec/rawcode.h" // IWYU pragma: export
#include "pm_upy/exec/reader.h" // IWYU pragma: export
#include "pm_upy/exec/await.h" // IWYU pragma: export
#include "pm_upy/exec/profile.h" // IWYU pragma: export
#include "pm_upy/exec/native.h" // IWYU pragma: export
#include "pm_upy/nlr/nlr.h" // IWYU pragma: export
#include "pm_upy/hal/time.h" // IWYU pragma: export
#include "pm_upy/hal/stdio.h" // IWYU pragma: export
#include "pm_upy/obj/core.h" // IWYU pragma: export
#include "pm_upy/obj/call.h" // IWYU pragma: export
#include "pm_upy/obj/attr.h" // IWYU pragma: export
#include "pm_upy/obj/module.h" // IWYU pragma: export
#include "pm_upy/obj/exc.h" // IWYU pragma: export
#include "pm_upy/obj/qstr.h" // IWYU pragma: export
#include "pm_upy/obj/print.h" // IWYU pragma: export
#include "pm_upy/obj/buf.h" // IWYU pragma: export
#include "pm_upy/obj/list.h" // IWYU pragma: export
#include "pm_upy/obj/dict.h" // IWYU pragma: export
#include "pm_upy/obj/tuple.h" // IWYU pragma: export
#include "pm_upy/obj/type.h" // IWYU pragma: export
#include "pm_upy/obj/ops.h" // IWYU pragma: export
#include "pm_upy/obj/stream.h" // IWYU pragma: export
#include "pm_upy/obj/arg.h" // IWYU pragma: export
#include "pm_upy/obj/binary.h" // IWYU pragma: export
#include "pm_upy/obj/gen.h" // IWYU pragma: export
#include "pm_upy/vfs/vfs.h" // IWYU pragma: export
#include "pm_upy/vfs/blockdev.h" // IWYU pragma: export
#include "pm_upy/lib/uctypes.h" // IWYU pragma: export
#include "pm_upy/lib/re.h" // IWYU pragma: export
#include "pm_upy/lib/json.h" // IWYU pragma: export
#include "pm_upy/lib/deflate.h" // IWYU pragma: export
#include "pm_upy/lib/select.h" // IWYU pragma: export
#include "pm_upy/lib/socket.h" // IWYU pragma: export
#include "pm_upy/lib/asyncio.h" // IWYU pragma: export
#include "pm_upy/lib/ssl.h" // IWYU pragma: export
#include "pm_upy/lib/hw.h" // IWYU pragma: export
#include "pm_upy/util/warning.h" // IWYU pragma: export
#include "pm_upy/util/mperrno.h" // IWYU pragma: export
#include "pm_upy/util/libc_policy.h" // IWYU pragma: export

#endif /* PM_PM_UPY_H_ */
