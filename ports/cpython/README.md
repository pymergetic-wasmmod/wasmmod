# CPython host (planned)

Extension-module glue for CPython lands here. Shared core stays at the repo
root: pack parse, WAMR runtime/forwarders, `tools/`, `third_party/wamr`.

**Split:** the port loader chooses which Python payload to exec
(`.cpy.cpXYZ.pyc` matching the running interpreter, else `.py`). The
MicroPython port does the symmetric job for `.upy.*.mpy`. Same fat `.wasm`
can carry both; each host ignores the other lane — no shared scorer exports.

Until this port exists, `wasm_pack` may still emit `.pyc` targets; the
MicroPython loader skips them.
