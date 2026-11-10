# Runs the standard library tests of one area, tests/stdlib/<AREA>, with the
# installed tool from the stdlib-install fixture.
#
#   <area>/*.nra           Test files for `neura test`. Each runs optimized
#                          twice (output and step counts must be identical)
#                          and unoptimized (the same tests must pass). An
#                          optional `// test-options: ...` header line adds
#                          options such as capabilities.
#   <area>/support/        Modules the test files import; passed with -I.
#   <area>/traps/*.nra     Programs that must stop with a diagnostic. Header
#                          lines give `// status: N`, `// stderr: <regex>`,
#                          and optionally `// run-options: ...` and
#                          `// args: ...`; <name>.out holds the stdout.
#                          In option and argument text, @AREA_DIR@ stands
#                          for the area's source directory (for checked-in
#                          traces given to --replay), and @NATIVE_DIR@ for
#                          the directory of the C test library built from
#                          tests/native (libmathops).
#   <area>/programs/*.nra  Programs run to completion, with the same header
#                          lines (status defaults to 0) and a .out file. Each
#                          image also round-trips through disassemble and
#                          assemble byte for byte, and decompile runs on it.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR OR NOT DEFINED AREA)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, TEST_DIR, and AREA")
endif()

set(area_dir "${SOURCE_DIR}/tests/stdlib/${AREA}")
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(support)
if(IS_DIRECTORY "${area_dir}/support")
    set(support -I "${area_dir}/support")
endif()

function(neura prefix directory)
    execute_process(COMMAND "${NEURA}" ${ARGN} WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    set("${prefix}_status" "${status}" PARENT_SCOPE)
    set("${prefix}_stdout" "${stdout}" PARENT_SCOPE)
    set("${prefix}_stderr" "${stderr}" PARENT_SCOPE)
endfunction()

# Test files.
file(GLOB test_files "${area_dir}/*.nra")
list(SORT test_files)
foreach(file IN LISTS test_files)
    get_filename_component(name "${file}" NAME_WE)
    file(STRINGS "${file}" test_options_line REGEX "^// test-options: ")
    string(REGEX REPLACE "^// test-options: " "" test_options "${test_options_line}")
    string(REPLACE "@AREA_DIR@" "${area_dir}" test_options "${test_options}")
    string(REPLACE "@NATIVE_DIR@" "${NATIVE_TEST_DIR}" test_options "${test_options}")
    separate_arguments(test_options)
    neura(first "${TEST_DIR}" test --stats ${test_options} ${support} "${file}")
    if(NOT first_status STREQUAL "0")
        message(FATAL_ERROR "${AREA}/${name}: tests failed (status ${first_status})\n${first_stdout}${first_stderr}")
    endif()
    neura(second "${TEST_DIR}" test --stats ${test_options} ${support} "${file}")
    if(NOT second_stdout STREQUAL first_stdout)
        message(FATAL_ERROR "${AREA}/${name}: a second run differed\n${first_stdout}\n${second_stdout}")
    endif()
    neura(plain "${TEST_DIR}" test --no-optimize ${test_options} ${support} "${file}")
    string(REGEX REPLACE " \\(steps [0-9]+, peak heap cells [0-9]+\\)" "" expected "${first_stdout}")
    if(NOT plain_status STREQUAL "0" OR NOT plain_stdout STREQUAL expected)
        message(FATAL_ERROR "${AREA}/${name}: unoptimized results differ (status ${plain_status})\n"
                            "${expected}\n${plain_stdout}${plain_stderr}")
    endif()
    message(STATUS "${AREA}/${name}: ${first_stdout}")
endforeach()

# Reads the header lines of a program into <prefix>_status, _stderr,
# _options, and _args.
function(read_header prefix source default_status)
    file(STRINGS "${source}" status_line REGEX "^// status: ")
    file(STRINGS "${source}" stderr_line REGEX "^// stderr: ")
    file(STRINGS "${source}" options_line REGEX "^// run-options: ")
    file(STRINGS "${source}" args_line REGEX "^// args: ")
    set(status "${default_status}")
    if(NOT status_line AND status STREQUAL "")
        message(FATAL_ERROR "${source}: a trap program needs a '// status:' header naming its exit status")
    endif()
    if(status_line)
        string(REGEX REPLACE "^// status: " "" status "${status_line}")
    endif()
    string(REGEX REPLACE "^// stderr: " "" pattern "${stderr_line}")
    string(REGEX REPLACE "^// run-options: " "" options "${options_line}")
    string(REGEX REPLACE "^// args: " "" args "${args_line}")
    string(REPLACE "@AREA_DIR@" "${area_dir}" options "${options}")
    string(REPLACE "@AREA_DIR@" "${area_dir}" args "${args}")
    string(REPLACE "@NATIVE_DIR@" "${NATIVE_TEST_DIR}" options "${options}")
    string(REPLACE "@NATIVE_DIR@" "${NATIVE_TEST_DIR}" args "${args}")
    separate_arguments(options)
    separate_arguments(args)
    set("${prefix}_status" "${status}" PARENT_SCOPE)
    set("${prefix}_stderr" "${pattern}" PARENT_SCOPE)
    set("${prefix}_options" "${options}" PARENT_SCOPE)
    set("${prefix}_args" "${args}" PARENT_SCOPE)
endfunction()

function(check_programs kind default_status)
    file(GLOB sources "${area_dir}/${kind}/*.nra")
    list(SORT sources)
    foreach(source IN LISTS sources)
        get_filename_component(name "${source}" NAME_WE)
        string(REGEX REPLACE "[.]nra$" ".out" expected_file "${source}")
        file(READ "${expected_file}" expected)
        read_header(header "${source}" "${default_status}")
        set(run_dir "${TEST_DIR}/${kind}-${name}")
        file(MAKE_DIRECTORY "${run_dir}")
        foreach(mode IN ITEMS optimized unoptimized)
            set(flags)
            if(mode STREQUAL "unoptimized")
                set(flags --no-optimize)
            endif()
            neura(build "${run_dir}" build ${flags} ${support} "${source}" -o "${run_dir}/${mode}.nvr")
            if(NOT build_status STREQUAL "0")
                message(FATAL_ERROR "${AREA}/${kind}/${name} (${mode}): build failed\n${build_stderr}")
            endif()
            neura(result "${run_dir}" run ${header_options} "${mode}.nvr" -- ${header_args})
            if(NOT result_status STREQUAL header_status OR NOT result_stdout STREQUAL expected)
                message(FATAL_ERROR "${AREA}/${kind}/${name} (${mode}): expected status ${header_status}, got "
                                    "${result_status}\nexpected stdout:\n${expected}\nactual:\n${result_stdout}\n"
                                    "stderr:\n${result_stderr}")
            endif()
            if(header_stderr STREQUAL "")
                if(NOT result_stderr STREQUAL "")
                    message(FATAL_ERROR "${AREA}/${kind}/${name} (${mode}): unexpected stderr\n${result_stderr}")
                endif()
            elseif(NOT result_stderr MATCHES "${header_stderr}")
                message(FATAL_ERROR "${AREA}/${kind}/${name} (${mode}): stderr does not match "
                                    "'${header_stderr}'\n${result_stderr}")
            endif()
        endforeach()
        if(kind STREQUAL "programs")
            neura(dis "${run_dir}" disassemble optimized.nvr -o optimized.nvra)
            neura(asm "${run_dir}" assemble optimized.nvra -o reassembled.nvr)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${run_dir}/optimized.nvr"
                "${run_dir}/reassembled.nvr" RESULT_VARIABLE different)
            if(NOT dis_status STREQUAL "0" OR NOT asm_status STREQUAL "0" OR NOT different STREQUAL "0")
                message(FATAL_ERROR "${AREA}/${kind}/${name}: the image does not round-trip through assembly\n"
                                    "${dis_stderr}${asm_stderr}")
            endif()
            neura(decompiled "${run_dir}" decompile optimized.nvr -o optimized.decompiled)
            if(NOT decompiled_status STREQUAL "0")
                message(FATAL_ERROR "${AREA}/${kind}/${name}: decompile failed\n${decompiled_stderr}")
            endif()
        endif()
        message(STATUS "${AREA}/${kind}/${name}: status ${header_status}")
    endforeach()
endfunction()

check_programs(programs 0)
# A trap's status depends on its class (nvm::exit_status), so each trap
# program states it.
check_programs(traps "")
