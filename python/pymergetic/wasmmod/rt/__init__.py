"""Host marker package for the wasmmod runtime.

The MicroPython ``wasm`` module and C/Rust loader live in this git repo
(``extmod/wasmmod`` as a drop-in submodule). This wheel pins a PyPI version
and will grow host bindings later.

Host CLI / inspect / ELF helpers: ``pip install pymergetic-wasmmod-tools``
(or ``pymergetic-wasmmod[tools]``).
"""

__all__: list[str] = []
