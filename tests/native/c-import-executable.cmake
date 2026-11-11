cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS NEURA SOURCE_DIR TEST_DIR MATHOPS TRIPLE HAS_STATIC)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, TEST_DIR, MATHOPS, TRIPLE, and HAS_STATIC")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(c_import "${SOURCE_DIR}/tests/c-import")
file(COPY "${c_import}/c-import.nra" "${SOURCE_DIR}/tests/native/mathops.h" DESTINATION "${TEST_DIR}")
configure_file("${MATHOPS}" "${TEST_DIR}/libmathops.a" COPYONLY)
unset(ENV{NEURA_RUN_OPTIONS})

function(invoke)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    foreach(field IN ITEMS status stdout stderr)
        set(result_${field} "${${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

function(expect_ok)
    invoke(${ARGN})
    if(NOT result_status STREQUAL "0")
        message(FATAL_ERROR "${ARGN} returned ${result_status}\n${result_stdout}${result_stderr}")
    endif()
    set(command_stderr "${result_stderr}" PARENT_SCOPE)
endfunction()

function(build_error expected)
    invoke("${NEURA}" build --native ${ARGN} -o rejected)
    if(NOT result_status STREQUAL "1" OR NOT result_stdout STREQUAL "" OR
       NOT result_stderr STREQUAL "neura: error: ${expected}\n" OR EXISTS "${TEST_DIR}/rejected")
        message(FATAL_ERROR "expected '${expected}' with no output file\n${result_stdout}${result_stderr}")
    endif()
endfunction()

expect_ok("${NEURA}" build c-import.nra -o c-import.nvr)
expect_ok("${NEURA}" build --native --allow-native c-import.nvr -o granted)
expect_ok("${NEURA}" build --native --allow-native c-import.nra -o from-source)
expect_ok("${CMAKE_COMMAND}" -E compare_files "${TEST_DIR}/granted" "${TEST_DIR}/from-source")
# Native code is granted by default, so the default policy equals --allow-native.
expect_ok("${NEURA}" build --native c-import.nvr -o default-grant)
if(NOT command_stderr STREQUAL "")
    message(FATAL_ERROR "the default policy grants native code, so there is no note\n${command_stderr}")
endif()
expect_ok("${CMAKE_COMMAND}" -E compare_files "${TEST_DIR}/granted" "${TEST_DIR}/default-grant")
expect_ok("${NEURA}" build --native --deny-native c-import.nvr -o denied)
if(NOT command_stderr STREQUAL
   "neura: note: the image uses native services, which are denied; they fail with CapabilityDenied\n")
    message(FATAL_ERROR "expected the denied native capability note\n${command_stderr}")
endif()

expect_ok("${NEURA}" disassemble c-import.nvr -o c-import.nvra)
file(STRINGS "${TEST_DIR}/c-import.nvra" native_record REGEX "^native-library " LIMIT_COUNT 1)
set(original_target "native-library \"${TRIPLE}\"")
if(NOT native_record MATCHES "^${original_target} [0-9a-fA-F]+$")
    message(FATAL_ERROR "the C-import image did not contain the expected NATV target record")
endif()
file(READ "${TEST_DIR}/c-import.nvra" assembly)
set(foreign_target riscv64-unknown-elf)
string(REPLACE "${original_target}" "native-library \"${foreign_target}\"" foreign "${assembly}")
file(WRITE "${TEST_DIR}/foreign.nvra" "${foreign}")
expect_ok("${NEURA}" assemble foreign.nvra -o foreign.nvr)
build_error("--native: the image's native library is for ${foreign_target}; the launcher is for ${TRIPLE}"
    --allow-native foreign.nvr)

if(HAS_STATIC)
    set(static_error "--static: the image uses native interop, which the static launcher does not provide")
    build_error("${static_error}" --static c-import.nvr)
    expect_ok("${NEURA}" build "${SOURCE_DIR}/tests/stdlib/dynamic-foreign/traps/denied.nra" -o ffi-only.nvr)
    build_error("${static_error}" --static ffi-only.nvr)

    # Isolate the NATV check from the FFI import check with an image that
    # has the real linked object and only a console import.
    set(plain "${SOURCE_DIR}/tests/backend/legacy/legacy-hello.nvra")
    expect_ok("${NEURA}" assemble "${plain}" -o plain.nvr)
    build_error("${static_error}" --static --allow-native plain.nvr)
    file(READ "${plain}" plain_assembly)
    string(REPLACE "nvr 1.0 profile 1" "nvr 1.4 profile 1" plain_assembly "${plain_assembly}")
    file(WRITE "${TEST_DIR}/natv-only.nvra" "${plain_assembly}\n${native_record}\n")
    expect_ok("${NEURA}" assemble natv-only.nvra -o natv-only.nvr)
    build_error("${static_error}" --static natv-only.nvr)

    # The static launcher has no native interop, so the default policy it
    # records grants every group except native; asking for native is refused.
    expect_ok("${NEURA}" build --native --static plain.nvr -o static-default)
    invoke("${NEURA}" inspect static-default)
    if(NOT result_status STREQUAL "0" OR NOT result_stdout MATCHES "\ncapabilities: console, process exit, files, network, environment, clock, entropy\n")
        message(FATAL_ERROR "a static launcher's default policy should omit native\n${result_stdout}")
    endif()
    expect_ok("${NEURA}" build --native --static --allow-native --deny-native plain.nvr -o static-denied-native)
    build_error("${static_error}" --static --deny-native --allow-native plain.nvr)

    # A static program cannot use the C library's resolver, so the static
    # launcher takes numeric addresses and "localhost" and refuses names.
    get_filename_component(native_tests "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
    expect_ok("${NEURA}" build --native --static "${native_tests}/static-names.nra" -o static-names)
    invoke("${TEST_DIR}/static-names")
    if(NOT result_status STREQUAL "0" OR
       NOT result_stdout MATCHES "^name: Unsupported: [^\n]*\nlocalhost: (connected|ConnectionRefused: [^\n]*)\nnumeric: (connected|ConnectionRefused: [^\n]*)\n$")
        message(FATAL_ERROR "the static launcher's address handling is not as documented\n${result_stdout}${result_stderr}")
    endif()
endif()

# The original archive and source play no part in either execution.
file(REMOVE "${TEST_DIR}/c-import.nra" "${TEST_DIR}/mathops.h" "${TEST_DIR}/libmathops.a" "${TEST_DIR}/c-import.nvr")
file(READ "${c_import}/c-import.out" expected)
invoke("${TEST_DIR}/granted")
if(NOT result_status STREQUAL "0" OR NOT result_stdout STREQUAL expected OR NOT result_stderr STREQUAL "")
    message(FATAL_ERROR "the embedded native library did not run with its grant\n${result_stdout}${result_stderr}")
endif()
invoke("${TEST_DIR}/denied")
if(NOT result_status STREQUAL "134" OR NOT result_stdout STREQUAL "" OR
   NOT result_stderr MATCHES "native access denied: the native capability is not granted")
    message(FATAL_ERROR "the embedded native library should trap before its first output\n${result_stdout}${result_stderr}")
endif()
message(STATUS "checked the bundled C-import library, recorded grant, and target mismatch")
