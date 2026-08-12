# MicroPython CMake fragment for wasmmod (mpwm).
# Included from extmod/extmod.cmake when MICROPY_PY_WASM=1.
# Prefer micropython.mk for the unix make path; this keeps cmake ports green.

if(MICROPY_PY_WASM)
  set(WASMMOD_DIR ${MICROPY_DIR}/extmod/wasmmod)
  include_directories(${WASMMOD_DIR})
  list(APPEND MICROPY_SOURCE_EXTMOD
    ${WASMMOD_DIR}/ports/micropython/modwasmmod.c
  )
  list(APPEND MICROPY_CPP_DEF_DEFS
    MICROPY_PY_WASM=1
    MICROPY_MODULE_BUILTIN_SUBPACKAGES=1
    PM_WASMMOD_GUEST=0
  )
  # Staticlib link is make-driven today; cmake consumers must add
  # libpymergetic_wasmmod.a explicitly after `cargo rustc --features upy-host`.
endif()
