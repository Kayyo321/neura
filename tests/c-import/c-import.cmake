# C imports (ForeignInterop.md, C imports): build programs that import
# tests/native/mathops.h with the static libmathops.a and the system's
# stdlib.h, run them, and check the image's NATV section, reproducible
# builds, .nir round trips, assembly, decompilation, and the diagnostics for
# unsupported inputs.
foreach(variable NEURA SOURCE_DIR TEST_DIR NATIVE_DIR C_COMPILER)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, TEST_DIR, NATIVE_DIR, and C_COMPILER")
    endif()
endforeach()
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(tests "${SOURCE_DIR}/tests/c-import")
file(COPY "${tests}/c-import.nra" "${tests}/qsort.nra" "${SOURCE_DIR}/tests/native/mathops.h"
     "${SOURCE_DIR}/tests/native/mathops.c" "${NATIVE_DIR}/libmathops.a" DESTINATION "${TEST_DIR}")

function(neura prefix)
    execute_process(COMMAND "${NEURA}" ${ARGN} WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    set("${prefix}_status" "${status}" PARENT_SCOPE)
    set("${prefix}_stdout" "${stdout}" PARENT_SCOPE)
    set("${prefix}_stderr" "${stderr}" PARENT_SCOPE)
endfunction()

function(expect_ok prefix)
    neura(${prefix} ${ARGN})
    if(NOT ${prefix}_status STREQUAL "0")
        message(FATAL_ERROR "${ARGN} returned ${${prefix}_status}\n${${prefix}_stdout}${${prefix}_stderr}")
    endif()
    set("${prefix}_stdout" "${${prefix}_stdout}" PARENT_SCOPE)
endfunction()

function(expect_error source pattern)
    file(WRITE "${TEST_DIR}/rejected.nra" "${source}")
    neura(rejected build rejected.nra -o rejected.nvr)
    if(rejected_status STREQUAL "0" OR NOT rejected_stderr MATCHES "${pattern}")
        message(FATAL_ERROR "expected a build error matching '${pattern}', got status ${rejected_status}\n"
                            "${rejected_stderr}")
    endif()
endfunction()

# Programs and their output.
foreach(name IN ITEMS c-import qsort)
    file(READ "${tests}/${name}.out" expected)
    expect_ok(build build ${name}.nra -o ${name}.nvr)
    expect_ok(run run --allow-native ${name}.nvr)
    if(NOT run_stdout STREQUAL expected)
        message(FATAL_ERROR "${name}: output differs:\n${run_stdout}")
    endif()
endforeach()

# A repeated build reproduces the image, the image round-trips through
# assembly, and a .nir build carries the native library.
expect_ok(again build c-import.nra -o again.nvr)
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${TEST_DIR}/c-import.nvr" "${TEST_DIR}/again.nvr"
                RESULT_VARIABLE different)
if(NOT different STREQUAL "0")
    message(FATAL_ERROR "repeating the build changed the image")
endif()
expect_ok(dis disassemble c-import.nvr -o c-import.nvra)
expect_ok(asm assemble c-import.nvra -o reassembled.nvr)
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${TEST_DIR}/c-import.nvr"
                "${TEST_DIR}/reassembled.nvr" RESULT_VARIABLE different)
if(NOT different STREQUAL "0")
    message(FATAL_ERROR "the image does not round-trip through assembly")
endif()
expect_ok(nir emit-ir c-import.nra -o c-import.nir)
expect_ok(from_nir build c-import.nir -o from-nir.nvr)
expect_ok(nir_run run --allow-native from-nir.nvr)
file(READ "${tests}/c-import.out" expected)
if(NOT nir_run_stdout STREQUAL expected)
    message(FATAL_ERROR "the image built from .nir behaves differently:\n${nir_run_stdout}")
endif()
expect_ok(decompiled decompile c-import.nvr)
if(NOT decompiled_stdout MATCHES "native library \"[^\"]+\", [0-9]+ bytes  \\[opaque" OR
   NOT decompiled_stdout MATCHES "native symbol \"neura_inline_mathops_square\"")
    message(FATAL_ERROR "decompilation should show the native library as an opaque region")
endif()
expect_ok(check check c-import.nra)

# Without the capability the first native call traps; on another target the
# embedded library cannot load.
neura(denied run c-import.nvr)
if(NOT denied_status STREQUAL "134" OR NOT denied_stderr MATCHES "native access denied: run with --allow-native")
    message(FATAL_ERROR "a run without --allow-native should trap with 134\n${denied_stderr}")
endif()
file(READ "${TEST_DIR}/c-import.nvra" assembly)
string(REGEX REPLACE "native-library \"[^\"]+\"" "native-library \"riscv64-unknown-elf\"" assembly "${assembly}")
file(WRITE "${TEST_DIR}/foreign.nvra" "${assembly}")
expect_ok(foreign_asm assemble foreign.nvra -o foreign.nvr)
neura(foreign run --allow-native foreign.nvr)
if(NOT foreign_status STREQUAL "134" OR NOT foreign_stderr MATCHES "native embedded library load failed")
    message(FATAL_ERROR "an image linked for another target should fail to load its library\n${foreign_stderr}")
endif()

# The generated bindings, with the test directory replaced.
expect_ok(bindings c-bindings --linked mathops.h)
string(REPLACE "${TEST_DIR}/" "" bindings "${bindings_stdout}")
file(READ "${tests}/mathops.bindings" expected_bindings)
if(NOT bindings STREQUAL expected_bindings)
    file(WRITE "${TEST_DIR}/mathops.bindings" "${bindings}")
    message(FATAL_ERROR "c-bindings output differs from tests/c-import/mathops.bindings; see "
                        "${TEST_DIR}/mathops.bindings")
endif()

# Diagnostics.
set(prefix "let program = import:Std.Program;\nlet m = import:c(\"mathops.h\", \"libmathops.a\");\n")
set(body "export let Main = module:program.Entry (\n    export let.override main = func(args: string[]) -> program.ExitCode {\n")
set(tail "        return program.ExitCode.Ok;\n    };\n);\n")
expect_error("${prefix}${body}        m.MATHOPS_TWICE(2);\n${tail}"
             "the C declaration 'MATHOPS_TWICE' was not imported: it is a function-like macro")
expect_error("${prefix}${body}        let flags = new m.mathops_flags();\n${tail}"
             "the C declaration 'mathops_flags' was not imported: it is bit-field ready")
expect_error("let missing = import:c(\"no-such-header.h\");\n" "cannot find C header 'no-such-header.h'")
expect_error("let shared = import:c(\"mathops.h\", \"libmathops.so\");\n" "cannot find C library|links static libraries")
file(COPY "${NATIVE_DIR}/libmathops.so" DESTINATION "${TEST_DIR}")
expect_error("let shared = import:c(\"mathops.h\", \"libmathops.so\");\n"
             "a C import links static libraries \\(.a, .lib\\) and object files")
execute_process(COMMAND "${C_COMPILER}" -c -fno-PIC -O1 mathops.c -o nopic.o WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE compiled)
if(compiled STREQUAL "0")
    execute_process(COMMAND "${CMAKE_AR}" rcs libnopic.a nopic.o WORKING_DIRECTORY "${TEST_DIR}")
    file(WRITE "${TEST_DIR}/nopic.nra" "let program = import:Std.Program;\nlet m = import:c(\"mathops.h\", \"libnopic.a\");\n${body}        m.add(1, 2);\n${tail}")
    neura(nopic build nopic.nra -o nopic.nvr)
    if(nopic_status STREQUAL "0")
        message(STATUS "the toolchain linked a non-PIC archive into a shared object; no diagnostic to check")
    elseif(NOT nopic_stderr MATCHES "linking the native library of the C imports failed" OR
           NOT nopic_stderr MATCHES "compiled with -fPIC")
        message(FATAL_ERROR "a non-PIC archive should fail to link with a -fPIC hint\n${nopic_stderr}")
    endif()
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env NEURA_LIBCLANG=/no/such/libclang.so "${NEURA}" check c-import.nra
                WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(status STREQUAL "0" OR NOT stderr MATCHES "C imports need libclang")
    message(FATAL_ERROR "a missing libclang should be a diagnostic\n${stderr}")
endif()
message(STATUS "C imports: programs, NATV, reproducibility, .nir, assembly, and diagnostics checked")
