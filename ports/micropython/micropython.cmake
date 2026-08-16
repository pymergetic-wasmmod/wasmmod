# MicroPython CMake fragment for wasmmod (upywm).
# Included from extmod/extmod.cmake when MICROPY_PY_WASM=1.
# Prefer micropython.mk for the unix make path; this keeps cmake ports green.

if(MICROPY_PY_WASM)
  set(WASMMOD_DIR ${MICROPY_DIR}/extmod/wasmmod)
  include_directories(${WASMMOD_DIR})
  if(NOT DEFINED MICROPY_PY_WASM_GEN)
    set(MICROPY_PY_WASM_GEN 1)
  endif()
  list(APPEND MICROPY_SOURCE_EXTMOD
    ${WASMMOD_DIR}/ports/common/boot.c
    ${WASMMOD_DIR}/ports/common/load.c
    ${WASMMOD_DIR}/ports/common/memcookie.c
    ${WASMMOD_DIR}/ports/micropython/modwasmmod.c
    ${WASMMOD_DIR}/ports/micropython/modutil.c
    ${WASMMOD_DIR}/ports/micropython/importhook.c
    ${WASMMOD_DIR}/ports/micropython/hostready.c
    ${WASMMOD_DIR}/ports/micropython/nativecall.c
    ${WASMMOD_DIR}/ports/micropython/objhandle.c
    ${WASMMOD_DIR}/src/pymergetic/wasmmod/pyexport/__impl__.c
  )
  if(MICROPY_PY_WASM_GEN)
    list(APPEND MICROPY_SOURCE_EXTMOD
      ${WASMMOD_DIR}/ports/micropython/modgen.c
    )
  endif()
  list(APPEND MICROPY_CPP_DEF_DEFS
    MICROPY_PY_WASM=1
    MICROPY_MODULE_BUILTIN_SUBPACKAGES=1
    PM_WASMMOD_GUEST=0
    MICROPY_PY_WASM_GEN=${MICROPY_PY_WASM_GEN}
  )
  # Staticlib link is make-driven today; cmake consumers must add
  # libpymergetic_wasmmod.a explicitly after cargo features matching GEN.
endif()
