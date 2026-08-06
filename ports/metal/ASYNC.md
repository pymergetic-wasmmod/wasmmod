# Metal async ↔ µPy await (upy-compatible)

**Lock:** Py async = Metal async. Upy scheduler may be DEAD; do not invent a
second asyncio engine in wasmmod.

## Sync border (keep)

| API | Role |
|-----|------|
| `pm_upy_resume(obj)` | `mp_resume` once; returns `mp_vm_return_kind_t` as int |
| `pm_upy_await(self_h, child_h)` | handle-based resume for guest/host packs |
| `pm_upy_handle_pending` / `loop_step` | run pending callbacks/exceptions |
| `mp_wasm_io_ops_t.yield` | optional runner checkpoint during long I/O |

## Metal registration

```c
#include "extmod/wasmmod/ports/metal/register_upy.h"
mp_wasm_metal_register_upy();  /* sets pm_metal_py_set_upy_resume/await */
```

Metal `_async_bridge` keeps `pm_metal_py_await` → Metal park; µPy pumping uses
`pm_metal_py_upy_resume` after registration.

`pm_upy_sleep_us` on the Metal port is `pm_metal_async_sleep_us` (Metal coro
handle). Generator resume is exercised by Metal `proof_upy_await_park`.

On wake: call `pm_upy_resume` / `pm_upy_await` (sync) then re-evaluate. Never hold
cross-module pointers across Metal await (Metal quiesce rules).

## Port flags

See `mpconfig_metal.h` — GC/scheduler off by default for Metal images.

## Do not

- Change `mp_resume` semantics
- Add Metal-only types to public `pm_upy_*` that C hosts cannot call
- Teach finder/load about Metal tasks — only io ops + resume/pending
