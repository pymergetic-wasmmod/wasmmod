# pymergetic.util.lock — hand-written stand-in for what the facegen tool
# should emit from __exports__.h's pm_wasmmod_pyexport_export_py*-routed
# symbols (see SOURCETREE.md "Python face").
#
# Python IS a real consumer here, same as C/Rust — upy already runs a
# live thread mutex of its own on ports that enable MICROPY_PY_THREAD
# (e.g. unix, on by default: ports/unix/mpconfigport.mk, backed by a real
# pthread_mutex_t in ports/unix/mpthreadport.c), so pretending Python
# "never" needs a lock is simply false on that target today.
#
# This one stays a *separate* primitive from upy's GIL/mp_thread_mutex_t
# on purpose, not by oversight: `pm_util_lock_t` is a pure spin (no OS
# call, works with zero OS underneath — bare metal, wasm32 pre-VM boot),
# while the GIL mutex is deliberately OS-blocking (yields the real
# thread/core instead of burning it) once real OS threads exist. Neither
# can be the other without a regression: spinning as the GIL wastes a
# core; the GIL's pthread mutex doesn't exist on bare metal/wasm32. They
# protect different layers (registry/native-side state that must be
# reachable before/outside the VM, vs. interpreter/GC state) and both
# stay necessary — but that's a reason for Python to be able to reach
# *this* lock when it needs to serialize access to native/registry-level
# state, not a reason to fence Python out of it.
class Lock:
    def __init__(self) -> None: ...
    def acquire(self) -> None: ...
    def release(self) -> None: ...
    def try_acquire(self) -> bool: ...
    def __enter__(self) -> "Lock": ...
    def __exit__(self, *exc_info: object) -> None: ...
