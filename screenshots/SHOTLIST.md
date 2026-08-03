# Screenshot shot list

Drop PNGs here (same names as below). README embeds them as eye-catchers.

Suggested terminal: dark theme, ~100–120 cols, JetBrains Mono / Cascadia / Fira Code.
Crop chrome; keep a little padding.

| File | What to capture | Command / tip |
|------|-----------------|---------------|
| `matrix-table.png` | The ASCII call-matrix TABLE block | `make -C examples test` — scroll to `TABLE  same-pack` / peer tables |
| `engine-summary.png` | Final engine scoreboard | `make -C examples test-engines` — last `Engine summary` block |
| `repl-demo.png` | Host REPL sample | `make -C examples demo` (or run `demo_readme.py` under `micropython`) |
| `pack-toml.png` *(optional)* | `bridge/pack.toml` exports/imports in editor | open `examples/bridge/pack.toml` |
| `guest-imports.png` *(optional)* | `MP_WASM_IMPORT` block | `examples/bridge/src/bridge.c` ~lines 40–60 |

After you drop the files, ping the agent — we can tweak crop/alt text / ordering in the README.
