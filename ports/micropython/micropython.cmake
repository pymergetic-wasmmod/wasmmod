# MicroPython / metalpython CMake fragment for wasmmod + nested WAMR.
# Included from extmod/extmod.cmake when MICROPY_PY_WASM is on.

if(NOT DEFINED MICROPY_WASMMOD_DIR)
    set(MICROPY_WASMMOD_DIR "${MICROPY_DIR}/extmod/wasmmod")
endif()
set(MICROPY_LIB_WAMR_DIR "${MICROPY_WASMMOD_DIR}/third_party/wamr")

list(APPEND GIT_SUBMODULES extmod/wasmmod)

if(NOT UPDATE_SUBMODULES AND NOT EXISTS ${MICROPY_LIB_WAMR_DIR}/core/iwasm/include/wasm_export.h)
    message(FATAL_ERROR
        " MICROPY_PY_WASM is enabled but wasmmod/WAMR is not initialised.\n"
        " Run 'git submodule update --init --recursive extmod/wasmmod'")
endif()

if(NOT DEFINED MICROPY_PY_WASM_AOT)
    set(MICROPY_PY_WASM_AOT 0)
endif()
if(NOT DEFINED MICROPY_PY_WASM_JIT)
    set(MICROPY_PY_WASM_JIT 0)
endif()
if(NOT DEFINED MICROPY_PY_WASM_FAST_JIT)
    set(MICROPY_PY_WASM_FAST_JIT 0)
endif()
if(NOT DEFINED MICROPY_PY_WASM_MATRIX)
    set(MICROPY_PY_WASM_MATRIX 0)
endif()
if(NOT DEFINED MICROPY_WASM_VERIFY)
    set(MICROPY_WASM_VERIFY 0)
endif()

list(APPEND MICROPY_SOURCE_EXTMOD
    ${MICROPY_WASMMOD_DIR}/wasmmod.c
    ${MICROPY_WASMMOD_DIR}/fetch.c
    ${MICROPY_WASMMOD_DIR}/finder.c
    ${MICROPY_WASMMOD_DIR}/forward.c
    ${MICROPY_WASMMOD_DIR}/host.c
    ${MICROPY_WASMMOD_DIR}/pack.c
    ${MICROPY_WASMMOD_DIR}/runtime.c
    ${MICROPY_WASMMOD_DIR}/verify.c
)

set(WAMR_BUILD_DIR "${CMAKE_BINARY_DIR}/wamr")
include(ExternalProject)
ExternalProject_Add(wamr_vmlib
    SOURCE_DIR ${MICROPY_LIB_WAMR_DIR}/product-mini/platforms/linux
    BINARY_DIR ${WAMR_BUILD_DIR}
    CMAKE_ARGS
        -DWAMR_BUILD_INTERP=1
        -DWAMR_BUILD_AOT=${MICROPY_PY_WASM_AOT}
        -DWAMR_BUILD_JIT=${MICROPY_PY_WASM_JIT}
        -DWAMR_BUILD_FAST_JIT=${MICROPY_PY_WASM_FAST_JIT}
        -DWAMR_BUILD_LIBC_BUILTIN=1
        -DWAMR_BUILD_LIBC_WASI=0
        -DWAMR_BUILD_SIMD=0
        -DWAMR_BUILD_MULTI_MODULE=0
        -DWAMR_DISABLE_HW_BOUND_CHECK=1
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${WAMR_BUILD_DIR} --target vmlib
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${WAMR_BUILD_DIR}/libiwasm.a
)

list(APPEND MICROPY_INC_CORE "${MICROPY_LIB_WAMR_DIR}/core/iwasm/include")
list(APPEND MICROPY_DEF_CORE
    MICROPY_PY_WASM=1
    MICROPY_PY_WASM_AOT=${MICROPY_PY_WASM_AOT}
    MICROPY_PY_WASM_JIT=${MICROPY_PY_WASM_JIT}
    MICROPY_PY_WASM_FAST_JIT=${MICROPY_PY_WASM_FAST_JIT}
    MICROPY_PY_WASM_MATRIX=${MICROPY_PY_WASM_MATRIX}
    MICROPY_WASM_VERIFY=${MICROPY_WASM_VERIFY}
)
target_link_libraries(${MICROPY_TARGET} ${WAMR_BUILD_DIR}/libiwasm.a pthread dl m)
if(MICROPY_PY_WASM_JIT OR MICROPY_PY_WASM_FAST_JIT)
    target_link_libraries(${MICROPY_TARGET} stdc++)
endif()
if(MICROPY_PY_WASM_JIT)
    find_package(LLVM REQUIRED CONFIG)
    if(LLVM_LINK_LLVM_DYLIB)
        target_link_libraries(${MICROPY_TARGET} LLVM)
    else()
        target_link_libraries(${MICROPY_TARGET} ${LLVM_AVAILABLE_LIBS})
    endif()
    target_link_directories(${MICROPY_TARGET} PRIVATE ${LLVM_LIBRARY_DIRS})
endif()
add_dependencies(${MICROPY_TARGET} wamr_vmlib)
