"""pymergetic.util.pysample — hello-world training module: a real `impl =
"py"` leaf with an actual Python body, so this tree has a live reason to
talk about the Python export face instead of only C/Rust ones.

No decorator, no manifest entry, no `sig` anywhere — the signature *is*
the face. Parsed statically via `ast` (see SOURCETREE.md "Py export face
— parsed from type hints, not declared anywhere"), same posture as
bindgen/cbindgen reading real C/Rust source.
"""


def hello() -> int:
    """No args, `int` return — the plain i32 shape, the default for
    anything the hint table doesn't call out specially."""
    return 42


def echo_len(data: bytes) -> int:
    """`bytes` param -> the `mem` shape: the native caller only ever
    holds a pointer+length (or, across a wasm boundary, an int32 memory
    cookie); the packer marshals that into a real Python `bytes` object
    before this function ever runs. The `bytes` annotation here already
    *is* that marshaling declaration — nothing else to write down."""
    return len(data)
