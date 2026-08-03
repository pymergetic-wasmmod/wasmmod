# Screenshot shot list

README embeds these as eye-catchers. Present now: `repl-demo.png`, `matrix-table.png`, `engine-summary.png`.

Suggested terminal: dark theme, ~100–120 cols, JetBrains Mono / Cascadia / Fira Code.
Crop chrome; keep a little padding.

| File | What to capture | Command / tip |
|------|-----------------|---------------|
| `matrix-table.png` | The ASCII call-matrix TABLE block | `make -C examples test` — scroll to `TABLE  same-pack` / peer tables |
| `engine-summary.png` | Final engine scoreboard | `make -C examples test-engines` — last `Engine summary` block |
| `repl-demo.png` | Real upy banner + `help('modules')` + packs | `make -C examples demo` (= `micropython -i < demo_readme.py`) or `make repl` |
| `pack-toml.png` *(optional)* | `bridge/pack.toml` exports/imports in editor | open `examples/bridge/pack.toml` |
| `guest-imports.png` *(optional)* | `MP_WASM_IMPORT` block | `examples/bridge/src/bridge.c` ~lines 40–60 |

After you drop the files, ping the agent — we can tweak crop/alt text / ordering in the README.
