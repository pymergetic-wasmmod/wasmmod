# hello

Minimal freestanding wasmmod pack (`src/` natives + Python tree).

## Try it

```python
import hello
hello.hello()
print(hello.add(2, 3))
```

## Notes

- Native exports: `hello`, `add`
- Pack manifest: `pack.toml`
- Source is embedded in the published `.wasm` (`[source] embed`)
