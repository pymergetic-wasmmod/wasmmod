# pymergetic.util.pysample — training leaf (impl = "py").
# Type hints are the SoT for C/RS access faces (see docs/SOURCETREE.md).
# mem/obj are face aliases (discover + checkers); callables still see bytes/object.

mem = bytes
obj = object


def hello() -> int:
    return 42


def echo_len(data: bytes) -> int:
    return len(data)


def echo_mem(data: mem) -> int:
    return len(data)


def is_none(o: obj) -> int:
    return 1 if o is None else 0
