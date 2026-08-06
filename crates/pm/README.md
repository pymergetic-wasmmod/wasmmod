Raw declarations: `pm::upy::ffi::*` / `pm::wasmmod::ffi::*` (bindgen dump). Thin wraps:
`pm::upy::{…}` and `pm::wasmmod::{…}` modules covering every bindgen `pm_*` function
(regen: parse `OUT_DIR/bindings.rs` — see agent notes / METAL_INTEGRATION Phase A).

Status helpers: `pm::check_status` / `pm::FeatureError`.

**NLR:** `pm_upy_nlr_push/pop/jump*` are C macros (setjmp); use from C with `py/nlr.h`,
not from Rust wraps.

**Linking:** this crate does not compile or link MicroPython/wasmmod glue. Host binaries
must link the µPy port + `glue/` objects. Guest import attrs are a later `cfg` path.
