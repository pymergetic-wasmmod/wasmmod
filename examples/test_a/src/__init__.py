# Embedded Python for test_a. a_ping / a_rs_ping are pack C/RS exports.


def a_py():
    return "pymergetic.wasmmod_examples.test_a"


def a_ping() -> int:
    return 11


def a_rs_ping() -> int:
    return 1
