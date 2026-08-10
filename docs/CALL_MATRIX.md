# Call matrix — every language, every direction

**Status: verified, not aspirational.** Every cell below is a passing case in
`examples/run_matrix.py` (WASM/WAMR container) or `examples/run_elf.py` (ELF
container). Run them yourself:

```sh
make -C extmod/wasmmod/examples test       # 64/64 cases — WASM container
make -C extmod/wasmmod/examples test-elf   # ELF container
```

If a cell were missing, `run_matrix.py` fails on its own: it builds the four
3×3 tables below from the recorded cases and raises if any cell is empty
(`MISSING = need a case (bug)`).

## The claim

Any module — C, Rust, Python, or a compiled Wasm/ELF guest — can call any
other module's exports, **in any direction**, using one resolution
mechanism: `__pm_modules` (see `include/pm_mod.h`, `mod.c`). There is no
separate registry per language and no slot/index dispatch. A caller either:

- resolves a **native function pointer** once (`pm_mod_resolve_native`) and
  calls it directly (or through a thunk — see below), or
- imports a **Python object** the normal way (`sys.modules`, attribute
  access) when the target is a plain Python callable with no native thunk.

Four directions, three "languages" on each axis (`Py` = host or
pack-embedded Python — same resolution path either way):

| Code | Direction | Example |
|------|-----------|---------|
| **H** | host → guest | host Python calls into a loaded pack's C/Rust/Python |
| **P** | guest → host | a pack's C/Rust/Python calls a host-registered callback |
| **G** | guest → guest | one pack calls into a different, already-loaded pack |
| **S** | same-pack | C/Rust/Python inside *one* `.wasm`/`.elf` call each other |

## Why a thunk is sometimes needed

`__pm_modules[module].native[func]` must hold a raw, callable C-ABI function
pointer. That's trivial for a real C/Rust export. It is **not** trivial for:

- a **Wasm export** — calling it needs the WAMR runtime's own marshaling, not
  a bare function pointer → `thunk.c` generates one fixed-signature native
  trampoline per numeric shape (0–3 `i32` args, `i64`, `f32`, `f64`, `3×f64`)
  and installs it as `native[func]` (`pm_mod_thunk_export`, wired from
  `packload.c`'s `bind_pack_exports`/`bind_export_cb`).
- a **pure Python callable** — same problem, opposite direction:
  `pyexport.c` generates the trampoline pool instead (`pm_mod_export_py` and
  the typed variants `_i64`/`_f32`/`_f64`/`_mem`/`_obj`/`_bufptr`), each
  wrapping `mp_call_function_n_kw` + the matching `mp_obj_new_*`/`mp_obj_get_*`
  conversion, GC-rooted so the callable can't be collected while a native
  pointer still points at its trampoline slot.

Either way, once installed, the caller's path is identical: resolve
`(module, func)` once, get back a real pointer, call it like any other
native export. `wasm.export_py*` is exactly how a host (or a pack, from its
own `__init__.py` — see `examples/bridge/src/__init__.py`) publishes a
Python callable this way.

For a **Wasm/WAMR guest**, the actual call site is a declared `MP_WASM_IMPORT`
— the loader's generic forwarder (`forward_raw` in `forward.c`) is what
dispatches on the resolved native's real param/result kinds (not just i32;
hardened to cover every thunk/pyexport shape). For an **ELF guest**, there's
no WAMR indirection at all: `elf_resolve_import` (`runtime.c`) resolves the
same `(module, func)` straight to a callable pointer at load time, with
`__pm_modules` checked *before* any peer-pack ELF/PLT lookup (a pack name can
be both a loaded peer *and* carry host/self Python exports under the same
name — the pack's own PLT/symbol table never has those).

## H — host → guest

```text
caller\callee | Py    | C                                      | RS
--------------+-------+----------------------------------------+----------
Py            | #6 #7 | #1 #2 #3 #8 #9 #43 #45 #46 #47 #57 #58 | #4 #5 #44
C             | #61   | #59                                    | #60
RS            | #64   | #62                                    | #63
```

Representative cases: `hello.hello()` (plain C export), `bridge.rs_square(5)`
(Rust export), `hello.util.ping()` (embedded pack Python, imported like any
submodule), `wasm.c_call(...)`/`wasm.rs_call(...)`/`wasm.rs_call_attr(...)`
(host-side C/Rust calling into a guest — same resolution, just invoked from
non-Python host code instead of the REPL).

## P — guest → host

```text
caller\callee | Py                      | C   | RS
--------------+-------------------------+-----+----
Py            | #21                     | #22 | #23
C             | #24 #25 #26 #27 #28 #29 | #35 | #36
RS            | #30 #31 #32 #33 #34     | #37 | #38
```

Representative cases: `bridge.via_host(7)` → a `matrix.host`-namespaced
`wasm.export_py` callback; `bridge.via_buf()`/`via_mem()`/`via_handle()` →
the richer marshaling shapes (raw buffer pointer, durable memory cookie,
opaque object handle); `bridge.via_host_c(7)`/`via_host_rs(7)` → host-side
*native* (not Python) fun-objs, proving the host end doesn't have to be
Python either.

## G — guest → guest

```text
caller\callee | Py  | C           | RS
--------------+-----+-------------+--------
Py            | #54 | #52         | #53
C             | #55 | #39 #41 #51 | #48 #49
RS            | #56 | #40 #42     | #50
```

Representative cases: `bridge.via_hello()` (C importing a different loaded
pack's C export), `bridge.rs_via_mixed_i64(10)` (Rust importing a peer pack's
Rust export), `bridge.via_peer_py()` (C importing a peer pack's *self-exported*
Python function — `hello/src/util/__init__.py` calls `wasm.export_py` on
itself at import time, so any later-loaded peer can import it by plain
name, no dynamic string lookup).

## S — same-pack

```text
caller\callee | Py  | C   | RS
--------------+-----+-----+------------
Py            | #18 | #14 | #13
C             | #19 | #15 | #10 #11 #12
RS            | #20 | #17 | #16
```

Representative cases: `bridge.via_c_self(5)` (C calling another C export in
the same module), `bridge.rs_via_pack_py()` (Rust calling this *same* pack's
own self-exported Python function via `pm_mod_export_py`) — proving the
mechanism doesn't care whether the peer is "this module" or some other
loaded module; it's the same `__pm_modules` lookup either way.

## ELF container (`run_elf.py`)

ELF packs (`type = "package"`, built via `wasmmod.py pack-elf`) go through
the in-tree ET_REL loader instead of WAMR — no sandboxed linear memory, no
marshaling, real host addresses. Covered there:

- `hello.elf` — plain C exports, embedded Python (`hello.util.ping_code()`).
- `client.elf` — guest → guest (imports `hello`'s C export by name).
- `hostcall.elf` — guest → host, including the ELF-only shape
  `wasm.export_py_bufptr` (raw `(ptr, len)` — only ever safe for ELF, since a
  Wasm guest's "pointer" is a linear-memory offset, not a real host address)
  and guest → host **Python** (`via_py` → `pymergetic.wasmmod_examples.hello`'s
  self-exported `_elf_abs`, i.e. an ELF guest calling a plain Python `abs`
  through the exact same `pm_mod_export_py` path a Wasm guest would use).
- Negative cases: loading a `micropython.*`-importing ELF pack fails
  (reserved namespace), loading a wrong-arch ELF fails (`e_machine` check).

## Where imports/exports actually come from

- **Imports** are auto-discovered from the compiled `.wasm`'s own standard
  import section (`discover_wasm_imports` in `tools/pack.py`) — whatever a
  pack's C/Rust source declares via `MP_WASM_IMPORT`/`#[link(wasm_import_module
  = ...)]` *is* the import list; `pack.toml [[imports]]` is no longer read.
  (ELF packs still declare `[[imports]]` by hand: a flat ELF undefined-symbol
  table carries no module namespace, so there's nothing to auto-discover it
  from — the manifest is the only place that grouping exists.)
- **Exports** are still manifest-declared (`[[exports]]`) for WASM packs,
  because that list also drives which symbols `wasm-ld --export=`s in the
  first place — auto-discovering exports the same way imports are would need
  a separate decision about the linker-export strategy (e.g. export
  everything vs. an explicit "public API" marker), not yet made.

## Reproducing / extending

- `examples/run_matrix.py` — `case(dir, src, dst, call_desc, got, want)`
  records one cell; `print_lang_table`/`print_catalog` render the tables
  above from whatever's been recorded. Add a case, get a table cell for
  free; remove one and a cell goes back to `MISSING` and the run fails.
- `examples/run_elf.py` — same idea, no table (ELF list is short enough to
  read as plain assertions).
- `examples/bridge/` (`src/bridge.c`, `src/lib.rs`, `src/__init__.py`) is the
  one pack exercising almost every cell — read it alongside its `pack.toml`
  for the concrete `MP_WASM_IMPORT`/`#[link(...)]`/`wasm.export_py` call at
  each cell.
