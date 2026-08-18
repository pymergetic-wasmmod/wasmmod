"""pymergetic.wasmmod — complete, single-distribution package: everything
under this path is defined right here in the wasmmod project, nothing
external ever contributes a child. That's what makes this a **regular**
package (`impl = "py"`, real `__init__.py`) rather than `pep420 = true`
like `pymergetic`/`pymergetic.util` — those two genuinely can gain
siblings from other distributions; this one can't and shouldn't pretend
to.
"""

# This package's callables are attached at import time by the C binding
# (ports/micropython/modwasmmod.c → mp_module_pymergetic_wasmmod). They are
# declared here so a static type checker (pyright) sees them. `pymergetic.util.gen`
# treats `__init__.py` as the typing source of truth for an `impl = "py"` module
# and never emits a sibling `__init__.pyi` — keep the C-attached surface in sync
# with the globals table in modwasmmod.c.
from __future__ import annotations

from typing import Any

__version__: str


def __init__(*args: Any, **kwargs: Any) -> None: ...


def version() -> str: ...


def has(name: str) -> bool: ...


def modules() -> list[str]: ...


def load(*args: Any, **kwargs: Any) -> Any: ...


def unload(name: str) -> Any: ...


def call(*args: Any, **kwargs: Any) -> Any: ...


def connect(*args: Any, **kwargs: Any) -> Any: ...


def verify(*args: Any, **kwargs: Any) -> bool: ...


def trust_add(cert: bytes | str, *, zlib_len: int | None = None) -> int: ...


def trust_apply(bundle: bytes) -> None: ...


def trust_reset() -> None: ...


def trust_policy() -> dict[str, Any]: ...


def source_list(*args: Any, **kwargs: Any) -> Any: ...


def path(*args: Any, **kwargs: Any) -> Any: ...


def path_append(*args: Any, **kwargs: Any) -> Any: ...


def cdn(*args: Any, **kwargs: Any) -> Any: ...


def cdn_prepend(*args: Any, **kwargs: Any) -> Any: ...


def cdn_reset(*args: Any, **kwargs: Any) -> Any: ...


def catalog(*args: Any, **kwargs: Any) -> Any: ...


def search(*args: Any, **kwargs: Any) -> Any: ...


def filter(*args: Any, **kwargs: Any) -> Any: ...


def session_id(*args: Any, **kwargs: Any) -> Any: ...


def publish(*args: Any, **kwargs: Any) -> Any: ...


def publish_file(*args: Any, **kwargs: Any) -> Any: ...


def install_hook(*args: Any, **kwargs: Any) -> Any: ...


def uninstall_hook(*args: Any, **kwargs: Any) -> Any: ...


def publish_presence(*args: Any, **kwargs: Any) -> Any: ...


def test(*args: Any, **kwargs: Any) -> Any: ...


def test_all(*args: Any, **kwargs: Any) -> Any: ...


def tests(*args: Any, **kwargs: Any) -> Any: ...


def test_count(*args: Any, **kwargs: Any) -> Any: ...


def bench(*args: Any, **kwargs: Any) -> Any: ...


def bench_all(*args: Any, **kwargs: Any) -> Any: ...


def benches(*args: Any, **kwargs: Any) -> Any: ...


def bench_count(*args: Any, **kwargs: Any) -> Any: ...


def bind_py(*args: Any, **kwargs: Any) -> Any: ...


def gen(*args: Any, **kwargs: Any) -> Any: ...


def guest(*args: Any, **kwargs: Any) -> Any: ...


def net(*args: Any, **kwargs: Any) -> Any: ...
