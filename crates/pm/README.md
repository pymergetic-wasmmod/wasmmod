# `pm` — Rust FFI for `include/pm_*.h`

Bindgen over the public umbrellas:

- `include/pm_upy.h`
- `include/pm_wasmmod.h`
- `include/pm_guest.h` (host: `PM_WASMMOD_GUEST=0`)

```text
cargo check -p pm
```

Raw declarations: `pm::sys::*`. Status helpers: `pm::check_status` / `pm::FeatureError`.

**Linking:** this crate does not compile or link MicroPython/wasmmod glue. Host binaries
must link the µPy port + `glue/` objects. Guest import attrs are a later `cfg` path.
