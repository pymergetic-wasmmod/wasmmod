# Symbol names: C ↔ Rust ↔ Python

Grouped by `include/` module path. **Alpha.**

## Status flags

| Flag | Meaning |
|------|---------|
| `ok` | Declared + real glue (config-gated where noted) |
| `stub` | Declared + linked, but `PM_ERR_FEATURE` / noop today |
| `probe` | Declared as `*_available()` only — honest config bit, no ops API |
| `missing` | Known needed / planned — **not in headers yet** (backlog for later runs) |

Columns: **C** · **Rust** (`pm::upy::ffi` / `pm::wasmmod::ffi` once bound) · **Python** · **Guest** · **Status**

Errors: `PM_ERR_FEATURE` → Rust `FeatureError` → Python raise.

### Module SoT (`pm_mod_*`)

| C | Status |
|---|--------|
| `pm_mod_publish` | `ok` |
| `pm_mod_export_set` | `ok` |
| `pm_mod_resolve_native` | `ok` |
| `pm_mod_connect_import` | `ok` |
| `pm_mod_import_get` | `ok` |
| `pm_mod_border_malloc` / `free` / `realloc` | `ok` |
| `pm_mod_connect_guest` | `ok` |

### Guest imports

| Module | Field | Host C | Status |
|--------|-------|--------|--------|
| `micropython.runtime` | `features` | `pm_upy_features` | `ok` |
| `micropython.runtime` | `has` | `pm_upy_has` | `ok` |
| `micropython.runtime` | `ticks_ms` | `pm_upy_ticks_ms` | `ok` |
| `wasmmod.host` | `mem_alloc` | `pm_wasmmod_mem_alloc` | `ok` |
| `wasmmod.host` | `mem_free` | `pm_wasmmod_mem_free` | `ok` |
| `wasmmod.host` | `mem_len` | `pm_wasmmod_mem_len` | `ok` |
| `micropython.runtime` | `ticks_us` | `pm_upy_ticks_us` | `ok` |
| `micropython.runtime` | `time_ns` | `pm_upy_time_ns` | `ok` |
| `micropython.runtime` | `delay_ms` | `pm_upy_delay_ms` | `ok` |
| `micropython.runtime` | `gc_collect` | `pm_upy_gc_collect` | `ok` |
| `micropython.runtime` | `run_str` | `pm_upy_run_str` | `ok` |
| `micropython.runtime` | `handle_pending` | `pm_upy_handle_pending` | `ok` |
| `micropython.runtime` | `sched_schedule` | `pm_upy_sched_schedule` | `ok` |
| `wasmmod.host` | `mem_set` | `pm_wasmmod_mem_set` | `ok` |
| `wasmmod.host` | `mem_valid` | `pm_wasmmod_mem_valid` | `ok` |

### Backlog counts

| ok | stub | probe | missing (API) | missing (guest import) |
|----|------|-------|---------------|------------------------|
| 285 | 0 | 33 | 0 | 0 ||------------------------|
| 266 | 19 | 33 | 0 | 0 |
| 249 | 36 | 33 | 0 | 0 ||------------------------|
| 227 | 58 | 33 | 0 | 0 ||------------------------|
| 216 | 69 | 33 | 0 | 0 |

Later runs: `rg '`missing`' include/SYMBOLS.md` (or filter Status column).

## `pm_wasmmod/version`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_version` | `pm::wasmmod::version::version` | `wasm.version` | — | `ok` | |

## `pm_wasmmod/runtime`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_runtime_deinit` | `pm::wasmmod::runtime::runtime_deinit` | — | — | `ok` | |
| `pm_wasmmod_runtime_init` | `pm::wasmmod::runtime::runtime_init` | `import pymergetic.wasmmod` | — | `ok` | |

## `pm_wasmmod/module`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_module_install` | `pm::wasmmod::module::module_install` | — | — | `ok` | |
| `pm_wasmmod_module_installed` | `pm::wasmmod::module::module_installed` | — | — | `ok` | |
| `pm_wasmmod_module_name` | `pm::wasmmod::module::module_name` | `wasm.__name__` | — | `ok` | |

## `pm_wasmmod/pack/load`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_pack_arch` | `pm::wasmmod::pack::pack_arch` | `pack.arch` | — | `ok` | |
| `pm_wasmmod_pack_close` | `pm::wasmmod::pack::pack_close` | `pack.close` / `wasm.unload` | — | `ok` | |
| `pm_wasmmod_pack_kind_str` | `pm::wasmmod::pack::pack_kind_str` | `pack.kind` | — | `ok` | |
| `pm_wasmmod_pack_load` | `pm::wasmmod::pack::pack_load` | `wasm.load` / `wasm.load_pack` | — | `ok` | |
| `pm_wasmmod_pack_load_ex` | `pm::wasmmod::pack::pack_load_ex` | `wasm.load_pack` | — | `ok` | |
| `pm_wasmmod_pack_origin` | `pm::wasmmod::pack::pack_origin` | `pack.origin` | — | `ok` | |

## `pm_wasmmod/pack/call`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_pack_call_0` | `pm::wasmmod::pack::pack_call_0` | `pack.call` | — | `ok` | |
| `pm_wasmmod_pack_call_i32` | `pm::wasmmod::pack::pack_call_i32` | `pack.call` | — | `ok` | |
| `pm_wasmmod_pack_lookup_fn` | `pm::wasmmod::pack::pack_lookup_fn` | `pack.<export>` | — | `ok` | |

## `pm_wasmmod/pack/mem`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_pack_linear` | `pm::wasmmod::pack::pack_linear` | — | — | `ok` | |
| `pm_wasmmod_pack_mem_alloc` | `pm::wasmmod::pack::pack_mem_alloc` | `pack.memory_alloc` | — | `ok` | |
| `pm_wasmmod_pack_mem_free` | `pm::wasmmod::pack::pack_mem_free` | `pack.memory_free` | — | `ok` | |
| `pm_wasmmod_pack_mem_read` | `pm::wasmmod::pack::pack_mem_read` | `pack.memory_read` | — | `ok` | |
| `pm_wasmmod_pack_mem_write` | `pm::wasmmod::pack::pack_mem_write` | `pack.memory_write` | — | `ok` | |

## `pm_wasmmod/host/cookie`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_mem_alloc` | `pm::wasmmod::cookie::mem_alloc` | `wasm.mem_alloc` | `wasmmod.host.mem_alloc` | `ok` | |
| `pm_wasmmod_mem_alloc_copy` | `pm::wasmmod::cookie::mem_alloc_copy` | — | — | `ok` | |
| `pm_wasmmod_mem_clear_all` | `pm::wasmmod::cookie::mem_clear_all` | `wasm.mem_clear` | — | `ok` | |
| `pm_wasmmod_mem_data` | `pm::wasmmod::cookie::mem_data` | `wasm.mem_get` | — | `ok` | |
| `pm_wasmmod_mem_free` | `pm::wasmmod::cookie::mem_free` | `wasm.mem_free` | `wasmmod.host.mem_free` | `ok` | |
| `pm_wasmmod_mem_len` | `pm::wasmmod::cookie::mem_len` | — | `wasmmod.host.mem_len` | `ok` | |
| `pm_wasmmod_mem_set` | `pm::wasmmod::cookie::mem_set` | `wasm.mem_set` | — | `ok` | |
| `pm_wasmmod_mem_valid` | `pm::wasmmod::cookie::mem_valid` | — | — | `ok` | |

## `pm_wasmmod/host/handle`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_handle_clear_all` | `pm::wasmmod::handle::handle_clear_all` | `wasm.handle_clear` | — | `ok` | |
| `pm_wasmmod_handle_free` | `pm::wasmmod::handle::handle_free` | `wasm.handle_free` | — | `ok` | |
| `pm_wasmmod_handle_register_ptr` | `pm::wasmmod::handle::handle_register_ptr` | `wasm.handle_register` | — | `ok` | |
| `pm_wasmmod_handle_resolve_ptr` | `pm::wasmmod::handle::handle_resolve_ptr` | `wasm.handle_resolve` | — | `ok` | |

## `pm_wasmmod/host/call`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_host_call_export_i32` | `pm::wasmmod::host::host_call_export_i32` | `wasm.c_call` | `wasmmod.host.call_i32` | `ok` | |

## `pm_wasmmod/path/io`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_io_get` | `pm::wasmmod::io::io_get` | — | — | `ok` | |
| `pm_wasmmod_io_set` | `pm::wasmmod::io::io_set` | — | — | `ok` | |
| `pm_wasmmod_io_yield` | `pm::wasmmod::io::io_yield` | — | — | `ok` | |

## `pm_wasmmod/path/cdn`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_cdn_configure` | `pm::wasmmod::cdn::cdn_configure` | `wasm.cdn` | — | `ok` | |
| `pm_wasmmod_cdn_fetch_index` | `pm::wasmmod::cdn::cdn_fetch_index` | — | — | `ok` | |
| `pm_wasmmod_cdn_fetch_pack` | `pm::wasmmod::cdn::cdn_fetch_pack` | — | — | `ok` | |
| `pm_wasmmod_cdn_reset` | `pm::wasmmod::cdn::cdn_reset` | — | — | `ok` | |

## `pm_wasmmod/path/fetch`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_fetch` | `pm::wasmmod::fetch::fetch` | — | — | `ok` | |

## `pm_wasmmod/path/verify`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_get_verify_enabled` | `pm::wasmmod::verify::get_verify_enabled` | `wasm.verify` | — | `ok` | |
| `pm_wasmmod_set_verify_enabled` | `pm::wasmmod::verify::set_verify_enabled` | `wasm.verify` | — | `ok` | |
| `pm_wasmmod_trust_add` | `pm::wasmmod::verify::trust_add` | `wasm.add_trust` | — | `ok` | |
| `pm_wasmmod_trust_clear` | `pm::wasmmod::verify::trust_clear` | `wasm.trust_clear` | — | `ok` | |
| `pm_wasmmod_trust_count` | `pm::wasmmod::verify::trust_count` | `wasm.trust_count` | — | `ok` | |
| `pm_wasmmod_verify_bytes` | `pm::wasmmod::verify::verify_bytes` | `wasm.verify_sig` | — | `ok` | |

## `pm_wasmmod/path/zlib`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_zlib_inflate` | `pm::wasmmod::zlib::zlib_inflate` | — | — | `ok` | |


## `pm_wasmmod/inspect/inspect`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_inspect_has_dwarf` | `pm::wasmmod::inspect::inspect_has_dwarf` | `wasm.has_dwarf` | — | `ok` | .wasm/.aot/.elf |
| `pm_wasmmod_inspect_symbols` | `pm::wasmmod::inspect::inspect_symbols` | `wasm.symbols` | — | `ok` | |
| `pm_wasmmod_inspect_addr2line` | `pm::wasmmod::inspect::inspect_addr2line` | `wasm.addr2line` | — | `ok` | |
| `pm_wasmmod_inspect_locations` | `pm::wasmmod::inspect::inspect_locations` | `wasm.locations` | — | `ok` | |
| `pm_wasmmod_inspect_disasm` | `pm::wasmmod::inspect::inspect_disasm` | `wasm.disasm` | — | `ok` | |
| `pm_wasmmod_inspect_mpy_disasm` | `pm::wasmmod::inspect::inspect_mpy_disasm` | `wasm.mpy_disasm` | — | `ok` | |

Host engine package name (self-desc embed): **`pymergetic.wasmmod`** (`role=host`). See `docs/PACK.md`.

## `pm_wasmmod/host/self`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_host_package_name` | `pm::wasmmod::host::host_package_name` | `pymergetic.wasmmod.host.package_name` | — | `ok` | `"pymergetic.wasmmod"` |
| `pm_wasmmod_host_self_path` | `pm::wasmmod::host::host_self_path` | — | — | `ok` | `/proc/self/exe` on Linux |
| `pm_wasmmod_host_set_self_image` | `pm::wasmmod::host::host_set_self_image` | `pymergetic.wasmmod.host.set_self_image` | — | `ok` | browser / no path |
| `pm_wasmmod_host_self_open` | `pm::wasmmod::host::host_self_open` | `pymergetic.wasmmod.host.source` | — | `ok` | running host `wasmmod.source` |
| `pm_wasmmod_host_pack_root` | `pm::wasmmod::host::host_pack_root` | `pymergetic.wasmmod.host.pack_root` | — | `ok` | `/mods/pymergetic.wasmmod` |
| `pm_wasmmod_host_self_pack_open` | `pm::wasmmod::host::host_self_pack_open` | `host.pack_files` / `host.pack_read` | — | `ok` | host `wasmmod.pack` as VFS paths |

## `pm_wasmmod/inspect/source`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_wasmmod_source_open_buffer` | `pm::wasmmod::source::source_open_buffer` | `wasm.source_from_bytes` | — | `ok` | |
| `pm_wasmmod_source_open_owned` | `pm::wasmmod::source::source_open_owned` | — | — | `ok` | takes ownership |
| `pm_wasmmod_source_open_file` | `pm::wasmmod::source::source_open_file` | `wasm.source_from_file` | — | `ok` | |
| `pm_wasmmod_source_open_name` | `pm::wasmmod::source::source_open_name` | `wasm.source` | — | `ok` | loaded pack name |
| `pm_wasmmod_source_close` | `pm::wasmmod::source::source_close` | `src.close` | — | `ok` | |
| `pm_wasmmod_source_info` | `pm::wasmmod::source::source_info` | `src.meta` | — | `ok` | |
| `pm_wasmmod_source_read` | `pm::wasmmod::source::source_read` | `src.read` | — | `ok` | |
| `pm_wasmmod_source_mount_prefix` | `pm::wasmmod::source::source_mount_prefix` | — | — | `ok` | |
| `pm_wasmmod_source_list_files` | `pm::wasmmod::source::source_list_files` | `src.files` | — | `ok` | |
| `pm_wasmmod_source_list_modules` | `pm::wasmmod::source::source_list_modules` | `src.modules` | — | `ok` | |
| `pm_wasmmod_source_list_submodules` | `pm::wasmmod::source::source_list_submodules` | `src.submodules` | — | `ok` | |

## `pm_upy/features`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_features` | `pm::upy::features::features` | — | `micropython.runtime.features` | `ok` | |
| `pm_upy_has` | `pm::upy::features::has` | — | `micropython.runtime.has` | `ok` | |
| `pm_upy_version` | `pm::upy::features::version` | — | — | `ok` | |

## `pm_upy/init`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_deinit` | `pm::upy::init::deinit` | `upy.init.deinit` | — | `ok` | |
| `pm_upy_init` | `pm::upy::init::init` | — | — | `ok` | |
| `pm_upy_ready` | `pm::upy::init::ready` | `upy.init.ready` | — | `ok` | |

## `pm_upy/mem/heap`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_alloc` | `pm::upy::mem::alloc` | — | — | `ok` | |
| `pm_upy_free` | `pm::upy::mem::free` | — | — | `ok` | |
| `pm_upy_realloc` | `pm::upy::mem::realloc` | — | — | `ok` | |

## `pm_upy/mem/gc`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_gc_collect` | `pm::upy::mem::gc_collect` | `gc.collect` | — | `ok` | |
| `pm_upy_gc_enabled` | `pm::upy::mem::gc_enabled` | — | — | `ok` | |

## `pm_upy/mem/stack`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_stack_check` | `pm::upy::mem::stack_check` | `upy.mem.stack_check` | — | `ok` | |
| `pm_upy_stack_ctrl_init` | `pm::upy::mem::stack_ctrl_init` | — | — | `ok` | |

## `pm_upy/loop/step`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_handle_pending` | `pm::upy::sched::handle_pending` | `upy.sched.handle_pending` | `micropython.runtime.handle_pending` | `ok` | |
| `pm_upy_loop_feed` | `pm::upy::step::loop_feed` | `upy.step.loop_feed` | — | `ok` | |
| `pm_upy_loop_reset` | `pm::upy::step::loop_reset` | `upy.step.loop_reset` | — | `ok` | |
| `pm_upy_loop_step` | `pm::upy::step::loop_step` | `upy.step.loop_step` | — | `ok` | |

## `pm_upy/loop/sched`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_sched_keyboard_interrupt` | `pm::upy::sched::sched_keyboard_interrupt` | `upy.sched.keyboard_interrupt` | — | `ok` | |
| `pm_upy_sched_lock` | `pm::upy::sched::sched_lock` | `upy.sched.lock` | — | `ok` | |
| `pm_upy_sched_num_pending` | `pm::upy::sched::sched_num_pending` | `upy.sched.num_pending` | — | `ok` | |
| `pm_upy_sched_schedule` | `pm::upy::sched::sched_schedule` | `micropython.schedule` | `micropython.runtime.sched_schedule` | `ok` | raw ptrs; prefer stdlib from Python |
| `pm_upy_sched_unlock` | `pm::upy::sched::sched_unlock` | `upy.sched.unlock` | — | `ok` | |
| `pm_upy_sched_exception` | `pm::upy::sched::sched_exception` | — | — | `ok` |  |
| `pm_upy_event_wait_ms` | `pm::upy::sched::event_wait_ms` | `upy.sched.event_wait_ms` | — | `ok` |  |

## `pm_upy/loop/repl`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_repl_active` | `pm::upy::repl::repl_active` | `upy.repl.active` | — | `ok` | |
| `pm_upy_repl_feed_line` | `pm::upy::repl::repl_feed_line` | `upy.repl.feed_line` | — | `ok` | |
| `pm_upy_repl_prompt` | `pm::upy::repl::repl_prompt` | `upy.repl.prompt` | — | `ok` | |
| `pm_upy_repl_start` | `pm::upy::repl::repl_start` | `upy.repl.start` | — | `ok` | |
| `pm_upy_repl_stop` | `pm::upy::repl::repl_stop` | `upy.repl.stop` | — | `ok` | |
| `pm_upy_repl_continue` | `pm::upy::repl::repl_continue` | `upy.repl.continue_src` | — | `ok` |  |
| `pm_upy_repl_autocomplete` | `pm::upy::repl::repl_autocomplete` | — | — | `ok` |  |
| `pm_upy_repl_banner` | `pm::upy::repl::repl_banner` | `upy.repl.banner` | — | `ok` |  |

## `pm_upy/exec/run`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_parse_compile_execute` | `pm::upy::run::parse_compile_execute` | `upy.run.parse_compile_execute` | — | `ok` | |
| `pm_upy_run_script` | `pm::upy::run::run_script` | `upy.run.run_script` | — | `ok` | |
| `pm_upy_run_str` | `pm::upy::run::run_str` | `upy.run.run_str` | `micropython.runtime.run_str` | `ok` | |
| `pm_upy_execute_bytecode` | `pm::upy::run::execute_bytecode` | — | — | `ok` |  |
| `pm_upy_make_function` | `pm::upy::run::make_function` | — | — | `ok` |  |
| `pm_upy_make_closure` | `pm::upy::run::make_closure` | — | — | `ok` |  |

## `pm_upy/exec/embed`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_embed_deinit` | `pm::upy::embed::embed_deinit` | `upy.embed.deinit` | — | `ok` | |
| `pm_upy_embed_exec_mpy` | `pm::upy::embed::embed_exec_mpy` | — | — | `ok` | |
| `pm_upy_embed_exec_str` | `pm::upy::embed::embed_exec_str` | `upy.embed.exec_str` | — | `ok` | |
| `pm_upy_embed_init` | `pm::upy::embed::embed_init` | — | — | `ok` | |

## `pm_upy/exec/pyexec`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_pyexec_file` | `pm::upy::pyexec::pyexec_file` | — | — | `ok` | |
| `pm_upy_pyexec_vstr` | `pm::upy::pyexec::pyexec_vstr` | — | — | `ok` | |

## `pm_upy/exec/compile`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_compile_available` | `pm::upy::compile::compile_available` | — / module `compile` if built | — | `probe` | |
| `pm_upy_compile` | `pm::upy::compile::compile` | compile() | — | `ok` | to raw_code / code obj |
| `pm_upy_compile_to_raw_code` | `pm::upy::compile::compile_to_raw_code` | — | — | `ok` |  |

## `pm_upy/exec/rawcode`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_raw_code_load_mem` | `pm::upy::rawcode::raw_code_load_mem` | — | — | `ok` |  |
| `pm_upy_raw_code_save` | `pm::upy::rawcode::raw_code_save` | — | — | `ok` |  |
| `pm_upy_raw_code_load_file` | `pm::upy::rawcode::raw_code_load_file` | — | — | `ok` |  |
| `pm_upy_find_frozen` | `pm::upy::rawcode::find_frozen` | — | — | `ok` |  |

## `pm_upy/exec/reader`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_reader_available` | `pm::upy::reader::reader_available` | — / module `reader` if built | — | `probe` | |
| `pm_upy_reader_new_mem` | `pm::upy::reader::reader_new_mem` | — | — | `ok` |  |
| `pm_upy_reader_new_file` | `pm::upy::reader::reader_new_file` | — | — | `ok` |  |

## `pm_upy/exec/await`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_await` | `pm::upy::await_::await_` | — | — | `ok` |  |
| `pm_upy_new_awaitable` | `pm::upy::await_::new_awaitable` | — | — | `ok` |  |
| `pm_upy_resume` | `pm::upy::await_::resume` | — | — | `ok` |  |
| `pm_upy_sleep_us` | `pm::upy::time::sleep_us` | — | — | `ok` |  |
| `pm_upy_gen_resume` | `pm::upy::await_::gen_resume` | — | — | `ok` | generator resume |

## `pm_upy/exec/profile`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_profile_settrace` | `pm::upy::profile::profile_settrace` | — | — | `ok` |  |
| `pm_upy_prof_instr_tick` | `pm::upy::profile::prof_instr_tick` | — | — | `ok` |  |

## `pm_upy/exec/native`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_dynruntime_available` | `pm::upy::native::dynruntime_available` | — / module `dynruntime` if built | — | `probe` | |
| `pm_upy_dynruntime_load` | `pm::upy::native::dynruntime_load` | — | — | `ok` |  |

## `pm_upy/nlr/nlr`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_nlr_available` | `pm::upy::nlr::nlr_available` | — / module `nlr` if built | — | `probe` | |
| `pm_upy_nlr_push` | — (C macro → `nlr_push`) | — | — | `ok` | setjmp macro; not bindgen/Rust-wrappable |
| `pm_upy_nlr_pop` | — (C macro → `nlr_pop`) | — | — | `ok` | setjmp macro; not bindgen/Rust-wrappable |
| `pm_upy_nlr_jump` | — (C macro → `nlr_jump`) | — | — | `ok` | setjmp macro; not bindgen/Rust-wrappable |
| `pm_upy_nlr_jump_fail` | — (C macro → `nlr_jump_fail`) | — | — | `ok` | setjmp macro; not bindgen/Rust-wrappable |

## `pm_upy/hal/time`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_delay_ms` | `pm::upy::time::delay_ms` | `time.sleep_ms` | `micropython.runtime.delay_ms` | `ok` | |
| `pm_upy_delay_us` | `pm::upy::time::delay_us` | `upy.time.delay_us` | — | `ok` | |
| `pm_upy_ticks_ms` | `pm::upy::time::ticks_ms` | `time.ticks_ms` | `micropython.runtime.ticks_ms` | `ok` | |
| `pm_upy_ticks_us` | `pm::upy::time::ticks_us` | `time.ticks_us` | `micropython.runtime.ticks_us` | `ok` | |
| `pm_upy_time_ns` | `pm::upy::time::time_ns` | `time.time_ns` | `micropython.runtime.time_ns` | `ok` | |
| `pm_upy_ticks_cpu` | `pm::upy::time::ticks_cpu` | — | — | `ok` |  |
| `pm_upy_time_localtime` | `pm::upy::time::time_localtime` | time.localtime | — | `ok` |  |
| `pm_upy_time_mktime` | `pm::upy::time::time_mktime` | time.mktime | — | `ok` |  |

## `pm_upy/hal/stdio`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_stdin_rx` | `pm::upy::stdio::stdin_rx` | — | — | `ok` | |
| `pm_upy_stdio_poll` | `pm::upy::stdio::stdio_poll` | — | — | `ok` | |
| `pm_upy_stdout_tx` | `pm::upy::stdio::stdout_tx` | — | — | `ok` | |

## `pm_upy/obj/core`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_obj_get_ll` | `pm::upy::obj::obj_get_ll` | — | — | `ok` | |
| `pm_upy_obj_new_bytes` | `pm::upy::obj::obj_new_bytes` | — | — | `ok` | |
| `pm_upy_obj_new_int_from_ll` | `pm::upy::obj::obj_new_int_from_ll` | — | — | `ok` | |
| `pm_upy_obj_new_str` | `pm::upy::obj::obj_new_str` | — | — | `ok` | |
| `pm_upy_obj_none` | `pm::upy::obj::obj_none` | — | — | `ok` | |
| `pm_upy_obj_new_int` | `pm::upy::obj::obj_new_int` | int() | — | `ok` | small int |
| `pm_upy_obj_get_int` | `pm::upy::obj::obj_get_int` | int(x) | — | `ok` |  |
| `pm_upy_obj_str_get` | `pm::upy::obj::obj_str_get` | str | — | `ok` | buffer out |
| `pm_upy_obj_new_bool` | `pm::upy::obj::obj_new_bool` | bool() | — | `ok` |  |
| `pm_upy_obj_new_float` | `pm::upy::obj::obj_new_float` | float() | — | `ok` |  |
| `pm_upy_obj_get_float` | `pm::upy::obj::obj_get_float` | float(x) | — | `ok` |  |
| `pm_upy_obj_new_bytearray` | `pm::upy::obj::obj_new_bytearray` | bytearray() | — | `ok` |  |
| `pm_upy_obj_new_memoryview` | `pm::upy::obj::obj_new_memoryview` | memoryview() | — | `ok` |  |
| `pm_upy_obj_new_slice` | `pm::upy::obj::obj_new_slice` | slice() | — | `ok` |  |
| `pm_upy_obj_new_complex` | `pm::upy::obj::obj_new_complex` | complex() | — | `ok` |  |
| `pm_upy_obj_new_set` | `pm::upy::obj::obj_new_set` | set() | — | `ok` |  |
| `pm_upy_obj_to_str` | `pm::upy::obj::obj_to_str` | str() | — | `ok` |  |

## `pm_upy/obj/call`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_call_function_0` | `pm::upy::call::call_function_0` | — | — | `ok` | |
| `pm_upy_call_function_1` | `pm::upy::call::call_function_1` | — | — | `ok` | |
| `pm_upy_fn_call_i32` | `pm::upy::call::fn_call_i32` | `upy.call.fn_call_i32` | — | `ok` | int path via handle |
| `pm_upy_fn_resolve` | `pm::upy::call::fn_resolve` | `upy.call.fn_resolve` | — | `ok` | leaf import + attr → handle |
| `pm_upy_fn_call` | `pm::upy::call::fn_call` | — | — | `ok` | n-arg/str path via handle |
| `pm_upy_call_function_n` | `pm::upy::call::call_function_n` | — | — | `ok` | n-arg call |
| `pm_upy_call_method` | `pm::upy::call::call_method` | — | — | `ok` |  |
| `pm_upy_fn_call_async` | `pm::upy::call::fn_call_async` | — | — | `ok` |  |

## `pm_upy/obj/attr`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_load_attr` | `pm::upy::ops::load_attr` | — | — | `ok` | |
| `pm_upy_store_attr` | `pm::upy::ops::store_attr` | — | — | `ok` | |

## `pm_upy/obj/module`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_bind_reg` | `pm::upy::module::bind_reg` | — | — | `ok` | |
| `pm_upy_import_name` | `pm::upy::module::import_name` | `__import__` | — | `ok` | dotted → leaf (non-empty fromlist) |
| `pm_upy_module_has` | `pm::upy::module::module_has` | — | — | `ok` | |
| `pm_upy_module_install_face` | `pm::upy::module::module_install_face` | — | — | `ok` | |
| `pm_upy_module_get_builtin` | `pm::upy::module::module_get_builtin` | — | — | `ok` |  |
| `pm_upy_obj_new_module` | `pm::upy::obj::obj_new_module` | — | — | `ok` |  |
| `pm_upy_bind` | `pm::upy::module::bind` | — | — | `ok` |  |
| `pm_upy_bind_resolve_module` | `pm::upy::module::bind_resolve_module` | — | — | `ok` |  |
| `pm_upy_import_from` | `pm::upy::module::import_from` | from x import y | — | `ok` |  |
| `pm_upy_import_all` | `pm::upy::module::import_all` | from x import * | — | `ok` |  |

## `pm_upy/obj/exc`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_raise_feature` | `pm::upy::exc::raise_feature` | — | — | `ok` | |
| `pm_upy_raise_msg` | `pm::upy::exc::raise_msg` | — | — | `ok` | |
| `pm_upy_raise_OSError` | `pm::upy::exc::raise_OSError` | raise OSError | — | `ok` |  |
| `pm_upy_obj_new_exception` | `pm::upy::obj::obj_new_exception` | — | — | `ok` |  |
| `pm_upy_obj_print_exception` | `pm::upy::obj::obj_print_exception` | — | — | `ok` |  |

## `pm_upy/obj/qstr`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_qstr_from_str` | `pm::upy::qstr::qstr_from_str` | — | — | `ok` | |
| `pm_upy_qstr_len` | `pm::upy::qstr::qstr_len` | — | — | `ok` | |
| `pm_upy_qstr_str` | `pm::upy::qstr::qstr_str` | — | — | `ok` | |

## `pm_upy/obj/print`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_printf` | `pm::upy::print::printf` | `print` | — | `ok` | |
| `pm_upy_obj_print` | `pm::upy::obj::obj_print` | print | — | `ok` |  |

## `pm_upy/obj/buf`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_buf_get` | `pm::upy::buf::buf_get` | — | — | `ok` | |

## `pm_upy/obj/list`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_list_append` | `pm::upy::list::list_append` | — | — | `ok` | |
| `pm_upy_obj_new_list` | `pm::upy::obj::obj_new_list` | — | — | `ok` | |
| `pm_upy_list_remove` | `pm::upy::list::list_remove` | list.remove | — | `ok` |  |
| `pm_upy_list_sort` | `pm::upy::list::list_sort` | list.sort | — | `ok` |  |

## `pm_upy/obj/dict`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_dict_store` | `pm::upy::dict::dict_store` | — | — | `ok` | |
| `pm_upy_obj_new_dict` | `pm::upy::obj::obj_new_dict` | — | — | `ok` | |
| `pm_upy_dict_get` | `pm::upy::dict::dict_get` | d[k] | — | `ok` |  |
| `pm_upy_dict_delete` | `pm::upy::dict::dict_delete` | del d[k] | — | `ok` |  |
| `pm_upy_dict_copy` | `pm::upy::dict::dict_copy` | d.copy() | — | `ok` |  |

## `pm_upy/obj/tuple`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_obj_new_tuple` | `pm::upy::obj::obj_new_tuple` | — | — | `ok` | |

## `pm_upy/obj/type`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_obj_is_callable` | `pm::upy::obj::obj_is_callable` | — | — | `ok` | |
| `pm_upy_obj_is_true` | `pm::upy::obj::obj_is_true` | — | — | `ok` | |
| `pm_upy_obj_new_bound_meth` | `pm::upy::obj::obj_new_bound_meth` | — | — | `ok` |  |
| `pm_upy_obj_new_closure` | `pm::upy::obj::obj_new_closure` | — | — | `ok` |  |
| `pm_upy_obj_new_gen_wrap` | `pm::upy::obj::obj_new_gen_wrap` | — | — | `ok` |  |
| `pm_upy_obj_new_cell` | `pm::upy::obj::obj_new_cell` | — | — | `ok` |  |

## `pm_upy/obj/ops`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_ops_available` | `pm::upy::misc::ops_available` | — / module `ops` if built | — | `probe` | |
| `pm_upy_unary_op` | `pm::upy::ops::unary_op` | — | — | `ok` |  |
| `pm_upy_binary_op` | `pm::upy::ops::binary_op` | — | — | `ok` |  |
| `pm_upy_getiter` | `pm::upy::ops::getiter` | iter() | — | `ok` |  |
| `pm_upy_iternext` | `pm::upy::ops::iternext` | next() | — | `ok` |  |
| `pm_upy_subscr` | `pm::upy::ops::subscr` | x[i] | — | `ok` |  |
| `pm_upy_len` | `pm::upy::ops::len` | len() | — | `ok` |  |
| `pm_upy_equal` | `pm::upy::ops::equal` | == | — | `ok` |  |
| `pm_upy_get_type` | `pm::upy::ops::get_type` | type() | — | `ok` |  |
| `pm_upy_is_subclass` | `pm::upy::ops::is_subclass` | issubclass() | — | `ok` |  |
| `pm_upy_load_global` | `pm::upy::ops::load_global` | — | — | `ok` |  |
| `pm_upy_store_global` | `pm::upy::ops::store_global` | — | — | `ok` |  |
| `pm_upy_load_name` | `pm::upy::ops::load_name` | — | — | `ok` |  |
| `pm_upy_store_name` | `pm::upy::ops::store_name` | — | — | `ok` |  |

## `pm_upy/obj/stream`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_stream_available` | `pm::upy::stream::stream_available` | — / module `stream` if built | — | `probe` | |
| `pm_upy_stream_rw` | `pm::upy::stream::stream_rw` | read/write | — | `ok` |  |
| `pm_upy_stream_seek` | `pm::upy::stream::stream_seek` | seek | — | `ok` |  |
| `pm_upy_stream_close` | `pm::upy::stream::stream_close` | close | — | `ok` |  |

## `pm_upy/obj/arg`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_arg_available` | `pm::upy::arg::arg_available` | — / module `arg` if built | — | `probe` | |
| `pm_upy_arg_parse` | `pm::upy::arg::arg_parse` | — | — | `ok` |  |

## `pm_upy/obj/binary`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_binary_available` | `pm::upy::binary::binary_available` | — / module `binary` if built | — | `probe` | |
| `pm_upy_binary_get` | `pm::upy::binary::binary_get` | struct | — | `ok` |  |
| `pm_upy_binary_set` | `pm::upy::binary::binary_set` | struct | — | `ok` |  |

## `pm_upy/obj/gen`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_gen_available` | `pm::upy::gen::gen_available` | — / module `gen` if built | — | `probe` | |
| `pm_upy_gen_resume` | `pm::upy::await_::gen_resume` | g.send | — | `ok` | dup of await group ok |

## `pm_upy/vfs/vfs`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_vfs_open` | `pm::upy::vfs::vfs_open` | `open` | — | `ok` | |
| `pm_upy_vfs_stat` | `pm::upy::vfs::vfs_stat` | `os.stat` | — | `ok` | |
| `pm_upy_vfs_mount` | `pm::upy::vfs::vfs_mount` | os.mount | — | `ok` |  |
| `pm_upy_vfs_listdir` | `pm::upy::vfs::vfs_listdir` | os.listdir | — | `ok` |  |
| `pm_upy_vfs_import_stat` | `pm::upy::vfs::vfs_import_stat` | — | — | `ok` |  |
| `pm_upy_builtin_open` | `pm::upy::vfs::builtin_open` | open | — | `ok` |  |

## `pm_upy/vfs/blockdev`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_vfs_blockdev_available` | `pm::upy::vfs::vfs_blockdev_available` | — / module `vfs_blockdev` if built | — | `probe` | |
| `pm_upy_vfs_blockdev_read` | `pm::upy::vfs::vfs_blockdev_read` | — | — | `ok` |  |
| `pm_upy_vfs_blockdev_write` | `pm::upy::vfs::vfs_blockdev_write` | — | — | `ok` |  |
| `pm_upy_vfs_blockdev_ioctl` | `pm::upy::vfs::vfs_blockdev_ioctl` | — | — | `ok` |  |

## `pm_upy/lib/uctypes`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_uctypes_available` | `pm::upy::uctypes::uctypes_available` | — / module `uctypes` if built | — | `probe` | |
| `pm_upy_uctypes_struct` | `pm::upy::uctypes::uctypes_struct` | uctypes.struct | — | `ok` |  |

## `pm_upy/lib/re`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_re_available` | `pm::upy::re::re_available` | — / module `re` if built | — | `probe` | |
| `pm_upy_re_compile` | `pm::upy::re::re_compile` | re.compile | — | `ok` |  |
| `pm_upy_re_match` | `pm::upy::re::re_match` | re.match | — | `ok` |  |

## `pm_upy/lib/json`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_json_available` | `pm::upy::json::json_available` | — / module `json` if built | — | `probe` | |
| `pm_upy_json_loads` | `pm::upy::json::json_loads` | json.loads | — | `ok` |  |
| `pm_upy_json_dumps` | `pm::upy::json::json_dumps` | json.dumps | — | `ok` |  |

## `pm_upy/lib/deflate`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_deflate_available` | `pm::upy::deflate::deflate_available` | — / module `deflate` if built | — | `probe` | |
| `pm_upy_deflate_decompress` | `pm::upy::deflate::deflate_decompress` | deflate | — | `ok` |  |

## `pm_upy/lib/select`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_select_available` | `pm::upy::select::select_available` | — / module `select` if built | — | `probe` | |
| `pm_upy_select_poll` | `pm::upy::select::select_poll` | select.poll | — | `ok` |  |

## `pm_upy/lib/socket`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_socket_available` | `pm::upy::socket::socket_available` | — / module `socket` if built | — | `probe` | |
| `pm_upy_socket_create` | `pm::upy::socket::socket_create` | socket.socket | — | `ok` | engine |
| `pm_upy_socket_connect` | `pm::upy::socket::socket_connect` | sock.connect | — | `ok` |  |
| `pm_upy_socket_bind` | `pm::upy::socket::socket_bind` | sock.bind | — | `ok` |  |
| `pm_upy_socket_listen` | `pm::upy::socket::socket_listen` | sock.listen | — | `ok` |  |
| `pm_upy_socket_accept` | `pm::upy::socket::socket_accept` | sock.accept | — | `ok` |  |
| `pm_upy_socket_send` | `pm::upy::socket::socket_send` | sock.send | — | `ok` |  |
| `pm_upy_socket_recv` | `pm::upy::socket::socket_recv` | sock.recv | — | `ok` |  |

## `pm_upy/lib/network`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_network_available` | `pm::upy::network::network_available` | — / module `network` if built | — | `probe` | |
| `pm_upy_network_hostname` | `pm::upy::network::network_hostname` | `network.hostname` | — | `ok` | |
| `pm_upy_network_ifconfig` | `pm::upy::network::network_ifconfig` | nic.ifconfig | — | `ok` |  |
| `pm_upy_network_active` | `pm::upy::network::network_active` | nic.active | — | `ok` |  |
| `pm_upy_network_connect` | `pm::upy::network::network_connect` | nic.connect | — | `ok` |  |
| `pm_upy_network_status` | `pm::upy::network::network_status` | nic.status | — | `ok` |  |

## `pm_upy/lib/lwip`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_lwip_available` | `pm::upy::lwip::lwip_available` | — / module `lwip` if built | — | `probe` | |
| `pm_upy_lwip_init` | `pm::upy::lwip::lwip_init` | — | — | `ok` | |
| `pm_upy_lwip_poll` | `pm::upy::lwip::lwip_poll` | — | — | `ok` | |
| `pm_upy_lwip_gethostbyname` | `pm::upy::lwip::lwip_gethostbyname` | socket.getaddrinfo | — | `ok` |  |

## `pm_upy/lib/bluetooth`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_bluetooth_available` | `pm::upy::bluetooth::bluetooth_available` | — / module `bluetooth` if built | — | `probe` | |
| `pm_upy_bluetooth_init` | `pm::upy::bluetooth::bluetooth_init` | bluetooth.BLE | — | `ok` | engine |

## `pm_upy/lib/websocket`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_websocket_available` | `pm::upy::websocket::websocket_available` | — / module `websocket` if built | — | `probe` | |
| `pm_upy_websocket_wrap` | `pm::upy::websocket::websocket_wrap` | websocket | — | `ok` | engine |

## `pm_upy/lib/asyncio`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_asyncio_available` | `pm::upy::asyncio::asyncio_available` | — / module `asyncio` if built | — | `probe` | |
| `pm_upy_asyncio_run` | `pm::upy::asyncio::asyncio_run` | asyncio.run | — | `ok` | engine |
| `pm_upy_asyncio_create_task` | `pm::upy::asyncio::asyncio_create_task` | asyncio.create_task | — | `ok` |  |

## `pm_upy/lib/ssl`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_ssl_available` | `pm::upy::ssl::ssl_available` | — / module `ssl` if built | — | `probe` | |
| `pm_upy_ssl_wrap_socket` | `pm::upy::ssl::ssl_wrap_socket` | ssl.wrap_socket | — | `ok` | engine |

## `pm_upy/lib/hw`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_hw_available` | `pm::upy::misc::hw_available` | — / module `hw` if built | — | `probe` | |
| `pm_upy_machine_pin` | `pm::upy::hw::machine_pin` | machine.Pin | — | `ok` | machine_* family |
| `pm_upy_machine_i2c` | `pm::upy::hw::machine_i2c` | machine.I2C | — | `ok` |  |
| `pm_upy_machine_spi` | `pm::upy::hw::machine_spi` | machine.SPI | — | `ok` |  |
| `pm_upy_machine_uart` | `pm::upy::hw::machine_uart` | machine.UART | — | `ok` |  |
| `pm_upy_framebuf_new` | `pm::upy::hw::framebuf_new` | framebuf.FrameBuffer | — | `ok` |  |

## `pm_upy/util/warning`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_warning` | `pm::upy::util::warning` | — | — | `ok` | |

## `pm_upy/util/mperrno`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_errno_get` | `pm::upy::util::errno_get` | — | — | `ok` | |

## `pm_upy/util/libc_policy`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_libc_policy` | `pm::upy::util::libc_policy` | — | — | `ok` | |

## `pm_upy/util/mpz`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_mpz_available` | `pm::upy::util::mpz_available` | — / module `mpz` if built | — | `probe` | |
| `pm_upy_mpz_from_int` | `pm::upy::util::mpz_from_int` | — | — | `ok` |  |

## `pm_upy/util/pairheap`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_pairheap_available` | `pm::upy::util::pairheap_available` | — / module `pairheap` if built | — | `probe` | |
| `pm_upy_pairheap_init` | `pm::upy::util::pairheap_init` | — | — | `ok` |  |

## `pm_upy/util/ringbuf`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_ringbuf_available` | `pm::upy::util::ringbuf_available` | — / module `ringbuf` if built | — | `probe` | |
| `pm_upy_ringbuf_put` | `pm::upy::util::ringbuf_put` | — | — | `ok` |  |
| `pm_upy_ringbuf_get` | `pm::upy::util::ringbuf_get` | — | — | `ok` |  |

## `pm_upy/util/formatfloat`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_formatfloat_available` | `pm::upy::util::formatfloat_available` | — / module `formatfloat` if built | — | `probe` | |
| `pm_upy_format_float` | `pm::upy::util::format_float` | — | — | `ok` |  |

## `pm_upy/util/parsenum`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_parsenum_available` | `pm::upy::util::parsenum_available` | — / module `parsenum` if built | — | `probe` | |
| `pm_upy_parse_num` | `pm::upy::util::parse_num` | — | — | `ok` |  |

## `pm_upy/util/scope`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_scope_available` | `pm::upy::util::scope_available` | — / module `scope` if built | — | `probe` | |
| `pm_upy_scope_new` | `pm::upy::util::scope_new` | — | — | `ok` |  |

## `pm_upy/util/asm`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_asm_available` | `pm::upy::util::asm_available` | — / module `asm` if built | — | `probe` | |
| `pm_upy_asm_emit` | `pm::upy::util::asm_emit` | — | — | `ok` |  |

## `pm_upy/util/stackalt`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_stackalt_available` | `pm::upy::util::stackalt_available` | — / module `stackalt` if built | — | `probe` | |
| `pm_upy_pystack_init` | `pm::upy::util::pystack_init` | — | — | `ok` |  |

## `pm_upy/util/autoload`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_autoload_available` | `pm::upy::util::autoload_available` | — / module `autoload` if built | — | `probe` | |
| `pm_upy_autoload` | `pm::upy::util::autoload` | — | — | `ok` |  |

## `pm_upy/util/persistentcode`

| C | Rust | Python | Guest | Status | Notes |
|---|------|--------|-------|--------|-------|
| `pm_upy_persistentcode_available` | `pm::upy::util::persistentcode_available` | — / module `persistentcode` if built | — | `probe` | |
| `pm_upy_persistentcode_save_fun` | `pm::upy::util::persistentcode_save_fun` | — | — | `ok` |  |
