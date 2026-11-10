# End-to-end checks of `neura build` and `neura run`: build routes, byte-level
# reproducibility, running an image away from its sources, exact stdout and
# exit status, runtime traps, and the status of every example.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(tests "${SOURCE_DIR}/tests/backend")
set(roots -I "${SOURCE_DIR}/examples/guide" -I "${SOURCE_DIR}/examples/types" -I "${SOURCE_DIR}/examples/testing")

# Runs neura and stores its status, stdout, and stderr in <prefix>_status,
# <prefix>_stdout, and <prefix>_stderr.
function(neura prefix directory)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    set("${prefix}_status" "${status}" PARENT_SCOPE)
    set("${prefix}_stdout" "${stdout}" PARENT_SCOPE)
    set("${prefix}_stderr" "${stderr}" PARENT_SCOPE)
endfunction()

function(build_image image)
    neura(result "${TEST_DIR}" build ${ARGN} -o "${image}")
    if(NOT result_status STREQUAL "0")
        message(FATAL_ERROR "build of ${image} failed with status ${result_status}\n${result_stderr}")
    endif()
    if(NOT EXISTS "${image}")
        message(FATAL_ERROR "build reported success but wrote no ${image}")
    endif()
endfunction()

function(same_bytes label left right)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${left}" "${right}" RESULT_VARIABLE different)
    if(NOT different STREQUAL "0")
        message(FATAL_ERROR "${label}: ${left} and ${right} differ")
    endif()
endfunction()

# Runs an image and compares stdout byte for byte with `expected`, the exit
# status, and stderr against a pattern (empty stderr when none is given).
# Extra arguments are run options; those after `--` go to the program.
function(expect_run label image expected expected_status stderr_pattern)
    get_filename_component(directory "${image}" DIRECTORY)
    get_filename_component(name "${image}" NAME)
    set(options)
    set(arguments)
    set(after_separator FALSE)
    foreach(argument IN LISTS ARGN)
        if(after_separator)
            list(APPEND arguments "${argument}")
        elseif(argument STREQUAL "--")
            set(after_separator TRUE)
        else()
            list(APPEND options "${argument}")
        endif()
    endforeach()
    neura(result "${directory}" run ${options} "${name}" -- ${arguments})
    if(NOT result_status STREQUAL "${expected_status}")
        message(FATAL_ERROR "${label}: expected status ${expected_status}, got ${result_status}\n${result_stderr}")
    endif()
    if(NOT result_stdout STREQUAL expected)
        message(FATAL_ERROR "${label}: stdout differs\nexpected:\n${expected}\nactual:\n${result_stdout}")
    endif()
    if(stderr_pattern STREQUAL "")
        if(NOT result_stderr STREQUAL "")
            message(FATAL_ERROR "${label}: unexpected stderr\n${result_stderr}")
        endif()
    elseif(NOT result_stderr MATCHES "${stderr_pattern}")
        message(FATAL_ERROR "${label}: stderr does not match '${stderr_pattern}'\n${result_stderr}")
    endif()
endfunction()

function(expect_build_failure label pattern image)
    neura(result "${TEST_DIR}" build ${ARGN} -o "${image}")
    if(result_status STREQUAL "0" OR NOT result_stderr MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: expected a build failure matching '${pattern}', got status "
                            "${result_status}\n${result_stderr}")
    endif()
    if(EXISTS "${image}" OR EXISTS "${image}.tmp")
        message(FATAL_ERROR "${label}: a failed build left an output file")
    endif()
endfunction()

function(expect_failure label pattern)
    neura(result "${TEST_DIR}" ${ARGN})
    if(result_status STREQUAL "0" OR NOT result_stderr MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: expected a failure matching '${pattern}', got status "
                            "${result_status}\n${result_stderr}")
    endif()
endfunction()

# Hello World: two build routes, reproducibility, and a run from a directory
# that holds only the image.
set(hello "${SOURCE_DIR}/examples/hello-world/hello-world.nra")
build_image("${TEST_DIR}/hello.nvr" "${hello}")
neura(emit "${TEST_DIR}" emit-ir "${hello}" -o "${TEST_DIR}/hello.nir")
if(NOT emit_status STREQUAL "0")
    message(FATAL_ERROR "emit-ir -o failed\n${emit_stderr}")
endif()
build_image("${TEST_DIR}/hello-from-nir.nvr" "${TEST_DIR}/hello.nir")
same_bytes("source and .nir builds" "${TEST_DIR}/hello.nvr" "${TEST_DIR}/hello-from-nir.nvr")
build_image("${TEST_DIR}/hello-again.nvr" "${hello}")
same_bytes("repeated source builds" "${TEST_DIR}/hello.nvr" "${TEST_DIR}/hello-again.nvr")
build_image("${TEST_DIR}/hello-from-nir-again.nvr" "${TEST_DIR}/hello.nir")
same_bytes("repeated .nir builds" "${TEST_DIR}/hello.nvr" "${TEST_DIR}/hello-from-nir-again.nvr")

# Std.Program's ExitCode is a payload enum, so every program now needs NVR
# 1.2. Images built before the standard library existed are NVR 1.0 and still
# run: the legacy fixtures are lossless disassemblies of a SysIo Hello World
# and of a program that used the retired Test assertion services.
file(READ "${TEST_DIR}/hello.nvr" hello_version OFFSET 4 LIMIT 4 HEX)
if(NOT hello_version STREQUAL "01000200")
    message(FATAL_ERROR "Hello World should be an NVR 1.2 image, found version bytes ${hello_version}")
endif()
neura(legacy "${TEST_DIR}" assemble "${tests}/legacy/legacy-hello.nvra" -o "${TEST_DIR}/legacy-hello.nvr")
if(NOT legacy_status STREQUAL "0")
    message(FATAL_ERROR "the legacy Hello World image does not assemble\n${legacy_stderr}")
endif()
file(READ "${TEST_DIR}/legacy-hello.nvr" legacy_version OFFSET 4 LIMIT 4 HEX)
if(NOT legacy_version STREQUAL "01000000")
    message(FATAL_ERROR "the legacy Hello World should be an NVR 1.0 image, found ${legacy_version}")
endif()
expect_run("legacy Hello World image" "${TEST_DIR}/legacy-hello.nvr" "Hello, World!\n" 0 "")
neura(legacy "${TEST_DIR}" assemble "${tests}/legacy/legacy-assert.nvra" -o "${TEST_DIR}/legacy-assert.nvr")
if(NOT legacy_status STREQUAL "0")
    message(FATAL_ERROR "the legacy assertion image does not assemble\n${legacy_stderr}")
endif()
# The runner no longer provides the retired assertion services, so an image
# that imports them is rejected when it binds, before it runs.
expect_run("legacy assertion image" "${TEST_DIR}/legacy-assert.nvr" "" 1
    "import 1 [(]neura[.]test[.]assert version 1[)] is not available")

file(MAKE_DIRECTORY "${TEST_DIR}/isolated")
file(COPY "${TEST_DIR}/hello.nvr" DESTINATION "${TEST_DIR}/isolated")
file(GLOB isolated_files "${TEST_DIR}/isolated/*")
list(LENGTH isolated_files isolated_count)
if(NOT isolated_count EQUAL 1)
    message(FATAL_ERROR "the isolated directory should hold only the image")
endif()
expect_run("hello world" "${TEST_DIR}/isolated/hello.nvr" "Hello, World!\n" 0 "")
expect_run("hello world with arguments" "${TEST_DIR}/isolated/hello.nvr" "Hello, World!\n" 0 "" -- one two)
file(STRINGS "${TEST_DIR}/hello.nvr" image_strings)
foreach(string IN LISTS image_strings)
    if(string MATCHES "${SOURCE_DIR}")
        message(FATAL_ERROR "the image contains a build-machine path: ${string}")
    endif()
endforeach()
if(NOT image_strings MATCHES "hello-world[.]nra")
    message(FATAL_ERROR "the source map should label hello-world.nra")
endif()

# The .nir listing route matches the source listing in both modes.
foreach(mode IN ITEMS "" --no-optimize)
    neura(from_source "${TEST_DIR}" emit-ir ${mode} "${hello}")
    neura(from_nir "${TEST_DIR}" emit-ir ${mode} "${TEST_DIR}/hello.nir")
    if(NOT from_source_stdout STREQUAL from_nir_stdout)
        message(FATAL_ERROR "emit-ir ${mode}: listing of hello.nir differs from the source listing")
    endif()
endforeach()

# Arguments after -- reach the entry function as UTF-8 text.
build_image("${TEST_DIR}/first-argument.nvr" "${tests}/arguments/first-argument.nra")
expect_run("first argument" "${TEST_DIR}/first-argument.nvr" "first: héllo\n" 0 "" -- "héllo" second)
expect_run("no arguments" "${TEST_DIR}/first-argument.nvr" "" 70
    "trap: index out of bounds: index 0 for length 0 in function [0-9]+ block [0-9]+ at first-argument[.]nra:8:40")

# Runnable examples.
build_image("${TEST_DIR}/module-interface.nvr" "${SOURCE_DIR}/examples/modules/module-interface.nra")
expect_run("module-interface" "${TEST_DIR}/module-interface.nvr"
    "stdout -> Application started\nstdout -> [INFO] Module interface successfully inherited\n" 0 "")
build_image("${TEST_DIR}/multi-file.nvr" -I "${SOURCE_DIR}/examples/modules/multi-file"
    "${SOURCE_DIR}/examples/modules/multi-file/main.nra")
expect_run("multi-file" "${TEST_DIR}/multi-file.nvr" "Hello, Neura!\n" 0 "")

# Library examples run through drivers that import them.
foreach(driver IN ITEMS core overlay classes collections polymorphism errors)
    build_image("${TEST_DIR}/${driver}-driver.nvr" ${roots} "${tests}/examples/${driver}-driver.nra")
    file(READ "${tests}/examples/${driver}-driver.out" expected)
    expect_run("${driver} driver" "${TEST_DIR}/${driver}-driver.nvr" "${expected}" 0 "")
endforeach()

# Examples whose builds stop at a documented limitation.

# The test runner: the math-tests example, and a suite with failures, a trap,
# and module state that each test starts fresh.
function(expect_tests label expected expected_status)
    neura(result "${TEST_DIR}" test ${ARGN})
    if(NOT result_status STREQUAL "${expected_status}")
        message(FATAL_ERROR "${label}: expected status ${expected_status}, got ${result_status}\n${result_stderr}")
    endif()
    if(NOT result_stdout STREQUAL expected)
        message(FATAL_ERROR "${label}: stdout differs\nexpected:\n${expected}\nactual:\n${result_stdout}")
    endif()
endfunction()
expect_tests("math-tests" "PASS MathTests.test_sum_to\n1 test(s), 1 passed, 0 failed\n" 0
    -I "${SOURCE_DIR}/examples/guide" "${SOURCE_DIR}/examples/testing/math-tests.nra")
file(READ "${tests}/testing/suite.out" expected)
expect_tests("test suite" "${expected}" 1 "${tests}/testing/suite.nra")
expect_tests("test suite unoptimized" "${expected}" 1 --no-optimize "${tests}/testing/suite.nra")
expect_failure("no tests" "no tests found" test "${hello}")
file(READ "${tests}/testing/exit-suite.out" expected)
expect_tests("exit suite" "${expected}" 1 "${tests}/testing/exit-suite.nra")
build_image("${TEST_DIR}/resources-driver.nvr" "${SOURCE_DIR}/examples/guide/resources.nra"
    "${tests}/examples/resources-driver.nra")
file(READ "${tests}/examples/resources-driver.out" expected)
expect_run("resources driver" "${TEST_DIR}/resources-driver.nvr" "${expected}" 0 "")

# Program initialization across files, with both build routes.
build_image("${TEST_DIR}/initialization.nvr" "${tests}/initialization/main.nra")
file(READ "${tests}/initialization/main.out" expected)
expect_run("initialization" "${TEST_DIR}/initialization.nvr" "${expected}" 0 "")
build_image("${TEST_DIR}/initialization-unoptimized.nvr" --no-optimize "${tests}/initialization/main.nra")
expect_run("initialization unoptimized" "${TEST_DIR}/initialization-unoptimized.nvr" "${expected}" 0 "")
neura(emit "${TEST_DIR}" emit-ir "${tests}/initialization/main.nra" -o "${TEST_DIR}/initialization.nir")
build_image("${TEST_DIR}/initialization-from-nir.nvr" "${TEST_DIR}/initialization.nir")
same_bytes("initialization from .nir" "${TEST_DIR}/initialization.nvr" "${TEST_DIR}/initialization-from-nir.nvr")
# Files that import each other start together, after the files the cycle
# imports, in both build modes.
foreach(mode IN ITEMS "" --no-optimize)
    build_image("${TEST_DIR}/initialization-cycle${mode}.nvr" ${mode} "${tests}/initialization-cycle/main.nra")
    file(READ "${tests}/initialization-cycle/main.out" expected)
    expect_run("initialization cycle ${mode}" "${TEST_DIR}/initialization-cycle${mode}.nvr" "${expected}" 0 "")
endforeach()
foreach(library IN ITEMS guide/core guide/errors testing/math-tests types/classes types/collections
                         types/overlay types/polymorphism modules/multi-file/acme/greetings
                         modules/multi-file/acme/greetings/punctuation)
    get_filename_component(name "${library}" NAME)
    expect_build_failure("${library}" "error: the program has no entry point" "${TEST_DIR}/${name}.nvr"
        ${roots} -I "${SOURCE_DIR}/examples/modules/multi-file" "${SOURCE_DIR}/examples/${library}.nra")
endforeach()

# Test programs, with and without optimization and source maps.
file(GLOB programs "${tests}/programs/*.nra")
list(SORT programs)
foreach(program IN LISTS programs)
    get_filename_component(name "${program}" NAME_WE)
    string(REGEX REPLACE "[.]nra$" ".out" expected_file "${program}")
    file(READ "${expected_file}" expected)
    build_image("${TEST_DIR}/${name}.nvr" "${program}")
    build_image("${TEST_DIR}/${name}-again.nvr" "${program}")
    same_bytes("${name} repeated builds" "${TEST_DIR}/${name}.nvr" "${TEST_DIR}/${name}-again.nvr")
    build_image("${TEST_DIR}/${name}-unoptimized.nvr" --no-optimize "${program}")
    build_image("${TEST_DIR}/${name}-no-map.nvr" --no-source-map "${program}")
    foreach(variant IN ITEMS "" -unoptimized -no-map)
        expect_run("${name}${variant}" "${TEST_DIR}/${name}${variant}.nvr" "${expected}" 0 "")
    endforeach()
endforeach()

# The new source constructs produce the same image after a NIR round trip.
foreach(name IN ITEMS operators closures aggregates class-nil generic-closures generic-enums interfaces stdlib-primitives)
    neura(emit "${TEST_DIR}" emit-ir "${tests}/programs/${name}.nra" -o "${TEST_DIR}/${name}.nir")
    if(NOT emit_status STREQUAL "0")
        message(FATAL_ERROR "${name}: emitting NIR failed with status ${emit_status}\n${emit_stderr}")
    endif()
    build_image("${TEST_DIR}/${name}-from-nir.nvr" "${TEST_DIR}/${name}.nir")
    same_bytes("${name} from NIR" "${TEST_DIR}/${name}.nvr" "${TEST_DIR}/${name}-from-nir.nvr")
endforeach()

# Runtime traps: stdout up to the trap, a diagnostic, and status 70.
file(GLOB traps "${tests}/traps/*.nra")
list(SORT traps)
foreach(trap IN LISTS traps)
    get_filename_component(name "${trap}" NAME_WE)
    string(REGEX REPLACE "[.]nra$" ".out" expected_file "${trap}")
    file(READ "${expected_file}" expected)
    file(STRINGS "${trap}" status_line REGEX "^// status: ")
    file(STRINGS "${trap}" stderr_line REGEX "^// stderr: ")
    file(STRINGS "${trap}" options_line REGEX "^// run-options: ")
    string(REGEX REPLACE "^// status: " "" status "${status_line}")
    string(REGEX REPLACE "^// stderr: " "" pattern "${stderr_line}")
    string(REGEX REPLACE "^// run-options: " "" options "${options_line}")
    separate_arguments(options)
    build_image("${TEST_DIR}/${name}.nvr" "${trap}")
    expect_run("trap ${name}" "${TEST_DIR}/${name}.nvr" "${expected}" "${status}" "${pattern}" ${options})
endforeach()

# Unsupported operations fail before an image is published.
file(GLOB unsupported "${tests}/unsupported/*.nra")
foreach(source IN LISTS unsupported)
    get_filename_component(name "${source}" NAME_WE)
    file(STRINGS "${source}" expectation REGEX "^// expect: ")
    string(REGEX REPLACE "^// expect: ([0-9]+:[0-9]+) (.*)$" "\\1: error: \\2" pattern "${expectation}")
    string(REPLACE "." "[.]" pattern "${pattern}")
    expect_build_failure("unsupported ${name}" "${name}[.]nra:${pattern}" "${TEST_DIR}/${name}.nvr" "${source}")
endforeach()

# Command-line errors.
expect_failure("build without -o" "build requires -o" build "${hello}")
expect_failure("run without an image" "expected an image file" run)
expect_failure("run with stray arguments" "program arguments follow --" run "${TEST_DIR}/hello.nvr" extra)
expect_failure("run a source file" "malformed image: header: not an NVR image" run "${hello}")
expect_failure("run a .nir file" "malformed image" run "${TEST_DIR}/hello.nir")
expect_failure("--no-source-map on run" "--no-source-map applies only to build" run --no-source-map "${TEST_DIR}/hello.nvr")
expect_failure("-I with .nir" "-I applies only to source inputs" build -I "${SOURCE_DIR}" "${TEST_DIR}/hello.nir" -o "${TEST_DIR}/x.nvr")
expect_failure("bad step limit" "expected a positive integer after --max-steps" run --max-steps 0 "${TEST_DIR}/hello.nvr")
neura(help "${TEST_DIR}" --help)
if(NOT help_stdout MATCHES "neura build [^\n]*-o <file[.]nvr>" OR NOT help_stdout MATCHES "neura run [^\n]*-- args")
    message(FATAL_ERROR "usage should document build and run\n${help_stdout}")
endif()
