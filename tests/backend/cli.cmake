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
    neura(result "${TEST_DIR}" build --error-format=short ${ARGN} -o "${image}")
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

# --tier-stats ends with one line of wall time (docs/runtime/NVM.md,
# "Controls"): every category, in order, none negative (no minus sign), and
# summing to the total within the rounding of ten figures to 0.1 ms.
function(expect_tier_time label stderr)
    set(ms "[0-9]+[.][0-9]")
    string(CONCAT line "tier: time: total ${ms} ms: load ${ms}, proving ${ms}, searching ${ms}, tier 0 ${ms}, "
                       "tier 1 ${ms}, regions ${ms}, collection ${ms}, host ${ms}, other ${ms}")
    if(NOT stderr MATCHES "\n(${line})\n$")
        message(FATAL_ERROR "${label}: the last line is not the wall-time line\n${stderr}")
    endif()
    string(REGEX MATCHALL "${ms}" figures "${CMAKE_MATCH_1}")
    set(sum 0)
    set(total "")
    foreach(figure IN LISTS figures)
        string(REPLACE "." "" tenths "${figure}")
        string(REGEX REPLACE "^0+([0-9])" "\\1" tenths "${tenths}")
        if(total STREQUAL "")
            set(total "${tenths}")
        else()
            math(EXPR sum "${sum} + ${tenths}")
        endif()
    endforeach()
    math(EXPR difference "${sum} - ${total}")
    if(difference GREATER 5 OR difference LESS -5)
        message(FATAL_ERROR "${label}: the categories sum to ${sum} tenths of a ms, the total is ${total}\n${stderr}")
    endif()
endfunction()

# The step-count lines of a fixed run are those of the runtime without the
# time line, which follows them.
string(CONCAT hello_tier_lines
    "tier: blocks 59, hinted 1, attempted 0, lifted 0 (0 by hint, 0 by search), hints rejected 0; proving 0.0 ms, searching 0.0 ms\n"
    "tier: steps 24: tier 2 0.0% (in regions 0), tier 1 91.7% (lifted 0, wired 0, empty 22), tier 0 8.3% (reference 2, declined 0)\n"
    "tier: jit off\n"
    "tier: tier 0 steps 1 in function 2 block 9: below the threshold\n"
    "tier: tier 0 steps 1 in function 0 block 11: below the threshold\n")
foreach(tier IN ITEMS 2 0)
    if(tier STREQUAL "0")
        set(expected_lines "tier: tier 0\n")
    else()
        set(expected_lines "${hello_tier_lines}")
    endif()
    neura(tier_stats "${TEST_DIR}" run --tier=${tier} --jit=off --tier-stats "${TEST_DIR}/hello.nvr")
    if(NOT tier_stats_status STREQUAL "0" OR NOT tier_stats_stdout STREQUAL "Hello, World!\n")
        message(FATAL_ERROR "--tier-stats at tier ${tier} changed the run\n${tier_stats_stdout}${tier_stats_stderr}")
    endif()
    string(FIND "${tier_stats_stderr}" "${expected_lines}tier: time: " at)
    if(NOT at EQUAL 0)
        message(FATAL_ERROR "--tier-stats at tier ${tier}: expected\n${expected_lines}followed by the time line, "
                            "got\n${tier_stats_stderr}")
    endif()
    expect_tier_time("--tier-stats at tier ${tier}" "${tier_stats_stderr}")
endforeach()
neura(no_tier_stats "${TEST_DIR}" run --tier=2 --jit=off "${TEST_DIR}/hello.nvr")
if(no_tier_stats_stderr MATCHES "tier:")
    message(FATAL_ERROR "a run without --tier-stats printed tier statistics\n${no_tier_stats_stderr}")
endif()
# A run that collects, lifts, and forms regions; members of `run --each`,
# which share one timeline; and `neura test`.
build_image("${TEST_DIR}/binary-trees.nvr" "${SOURCE_DIR}/tests/bench-wall/neura/binary-trees.nra")
neura(trees "${TEST_DIR}" run --tier-stats "${TEST_DIR}/binary-trees.nvr" -- 8)
if(NOT trees_status STREQUAL "0")
    message(FATAL_ERROR "binary-trees with --tier-stats failed\n${trees_stderr}")
endif()
expect_tier_time("binary-trees with --tier-stats" "${trees_stderr}")
file(WRITE "${TEST_DIR}/tree-sizes.txt" "4\n6\n8\n")
neura(trees_each "${TEST_DIR}" run --tier-stats --each "${TEST_DIR}/tree-sizes.txt" "${TEST_DIR}/binary-trees.nvr")
expect_tier_time("run --each with --tier-stats" "${trees_each_stderr}")
neura(tier_test "${TEST_DIR}" test --tier-stats ${roots} "${SOURCE_DIR}/examples/testing/math-tests.nra")
expect_tier_time("test with --tier-stats" "${tier_test_stderr}")
# With parallel workers, whose time overlaps the run's, the line is omitted.
neura(tier_workers "${TEST_DIR}" run --tier-stats --parallel-workers 2 "${TEST_DIR}/hello.nvr")
if(NOT tier_workers_stderr MATCHES "tier: steps" OR tier_workers_stderr MATCHES "tier: time:")
    message(FATAL_ERROR "--tier-stats with parallel workers\n${tier_workers_stderr}")
endif()

# NEURA_TIER_CHECK=2 (docs/runtime/NVM.md, "Controls"): a run checked against
# its shadow, recorded or not, prints and records what it does unchecked, and
# a replayed run, which the oracle does not check, says so in one line on
# stderr.
build_image("${TEST_DIR}/regions.nvr" "${tests}/tier/regions.nra")
set(shadow_runs "regions.nvr" "binary-trees.nvr --max-heap-cells 3000 -- 8")
foreach(shadow_run IN LISTS shadow_runs)
    separate_arguments(shadow_arguments UNIX_COMMAND "${shadow_run}")
    neura(unchecked "${TEST_DIR}" run --stats ${shadow_arguments})
    set(ENV{NEURA_TIER_EAGER} 1)
    set(ENV{NEURA_TIER_CHECK} 2)
    neura(checked "${TEST_DIR}" run --stats ${shadow_arguments})
    unset(ENV{NEURA_TIER_CHECK})
    unset(ENV{NEURA_TIER_EAGER})
    if(NOT checked_status STREQUAL unchecked_status OR NOT checked_stdout STREQUAL unchecked_stdout OR
       NOT checked_stderr STREQUAL unchecked_stderr)
        message(FATAL_ERROR "NEURA_TIER_CHECK=2 changed the run of ${shadow_run}: status ${checked_status}, "
                            "unchecked ${unchecked_status}\n${checked_stdout}${checked_stderr}")
    endif()
endforeach()
neura(unchecked "${TEST_DIR}" run --record "${TEST_DIR}/regions-unchecked.trace" "${TEST_DIR}/regions.nvr")
set(ENV{NEURA_TIER_EAGER} 1)
set(ENV{NEURA_TIER_CHECK} 2)
neura(checked_record "${TEST_DIR}" run --record "${TEST_DIR}/regions.trace" "${TEST_DIR}/regions.nvr")
neura(checked_replay "${TEST_DIR}" run --replay "${TEST_DIR}/regions.trace" "${TEST_DIR}/regions.nvr")
unset(ENV{NEURA_TIER_CHECK})
unset(ENV{NEURA_TIER_EAGER})
if(NOT checked_record_status STREQUAL "0" OR NOT checked_record_stdout STREQUAL unchecked_stdout OR
   NOT checked_record_stderr STREQUAL "")
    message(FATAL_ERROR "NEURA_TIER_CHECK=2 with --record: status ${checked_record_status}\n"
                        "${checked_record_stdout}${checked_record_stderr}")
endif()
same_bytes("a trace recorded under NEURA_TIER_CHECK=2" "${TEST_DIR}/regions-unchecked.trace" "${TEST_DIR}/regions.trace")
if(NOT checked_replay_status STREQUAL "0" OR NOT checked_replay_stdout STREQUAL unchecked_stdout OR
   NOT checked_replay_stderr STREQUAL "neura: NEURA_TIER_CHECK=2 is not applied: the run replays a trace\n")
    message(FATAL_ERROR "NEURA_TIER_CHECK=2 with --replay: status ${checked_replay_status}\n"
                        "${checked_replay_stdout}${checked_replay_stderr}")
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
expect_run("no arguments" "${TEST_DIR}/first-argument.nvr" "" 139
    "trap: index out of bounds: index 0 for length 0 in function [0-9]+ block [0-9]+ at first-argument[.]nra:8:40")

# Runnable examples.
build_image("${TEST_DIR}/module-interface.nvr" "${SOURCE_DIR}/examples/modules/module-interface.nra")
expect_run("module-interface" "${TEST_DIR}/module-interface.nvr"
    "stdout -> Application started\nstdout -> [INFO] Module interface successfully inherited\n" 0 "")
build_image("${TEST_DIR}/multi-file.nvr" -I "${SOURCE_DIR}/examples/modules/multi-file"
    "${SOURCE_DIR}/examples/modules/multi-file/main.nra")
expect_run("multi-file" "${TEST_DIR}/multi-file.nvr" "Hello, Neura!\n" 0 "")

# Field accessors (Types.md, Accessors), optimized and unoptimized.
build_image("${TEST_DIR}/accessors.nvr" "${SOURCE_DIR}/examples/types/accessors.nra")
build_image("${TEST_DIR}/accessors-unoptimized.nvr" --no-optimize "${SOURCE_DIR}/examples/types/accessors.nra")
foreach(variant IN ITEMS "" -unoptimized)
    expect_run("accessors example${variant}" "${TEST_DIR}/accessors${variant}.nvr" "payload\nXY729H08\nAB12\n1\n" 0 "")
endforeach()

# The same accessors on the fields of an anonymous class (Types.md, Anonymous
# classes).
build_image("${TEST_DIR}/anonymous-accessors.nvr" "${SOURCE_DIR}/examples/types/anonymous-accessors.nra")
build_image("${TEST_DIR}/anonymous-accessors-unoptimized.nvr" --no-optimize
    "${SOURCE_DIR}/examples/types/anonymous-accessors.nra")
foreach(variant IN ITEMS "" -unoptimized)
    expect_run("anonymous accessors example${variant}" "${TEST_DIR}/anonymous-accessors${variant}.nvr"
        "payload\nXY729H08\n1\n" 0 "")
endforeach()

# Tags with `last` fields, accessors, and anonymous classes (Tags.md).
build_image("${TEST_DIR}/download.nvr" "${SOURCE_DIR}/examples/tags/download.nra")
build_image("${TEST_DIR}/download-unoptimized.nvr" --no-optimize "${SOURCE_DIR}/examples/tags/download.nra")
foreach(variant IN ITEMS "" -unoptimized)
    expect_run("tags download example${variant}" "${TEST_DIR}/download${variant}.nvr"
        "https://example.com/data x5, legacy x3\n256/1024\n750\n" 0 "")
endforeach()

# The checked tags: a monitor shared by tasks, an #Immutable class, and #Pure
# and #ThreadSafe functions. The same output under any quantum, optimized or not.
build_image("${TEST_DIR}/monitor.nvr" "${SOURCE_DIR}/examples/concurrency/monitor.nra")
build_image("${TEST_DIR}/monitor-unoptimized.nvr" --no-optimize "${SOURCE_DIR}/examples/concurrency/monitor.nra")
foreach(variant IN ITEMS "" -unoptimized)
    foreach(quantum IN ITEMS 1 64 1024)
        expect_run("checked tags example${variant}, quantum ${quantum}" "${TEST_DIR}/monitor${variant}.nvr"
            "greenhouse: 8 readings, average 22 C (71 F), warmest 26 C\n" 0 "" --task-quantum ${quantum})
    endforeach()
endforeach()

# Scoped tasks, a cancellable operation, and shared state: the same output
# under any quantum.
build_image("${TEST_DIR}/service.nvr" "${SOURCE_DIR}/examples/concurrency/service.nra")
foreach(quantum IN ITEMS 1 64 1024)
    expect_run("concurrency example, quantum ${quantum}" "${TEST_DIR}/service.nvr"
        "tea 3, cake 5, soup -1\nfast answered; the others stopped early: true\nhome 400, about 200\n" 0 ""
        --task-quantum ${quantum})
endforeach()

# Target-typed new, typed destructuring, and an anonymous class.
build_image("${TEST_DIR}/construction.nvr" "${SOURCE_DIR}/examples/types/construction.nra")
expect_run("construction example" "${TEST_DIR}/construction.nvr"
    "Ada has 250\nGrace has 10\n4 accounts, 260 in total\n" 0 "")

# Library examples run through drivers that import them.
foreach(driver IN ITEMS core overlay classes collections polymorphism errors)
    # The errors driver shows a denied read, so it runs with files denied.
    set(driver_options)
    if(driver STREQUAL "errors")
        set(driver_options --deny-fs)
    endif()
    build_image("${TEST_DIR}/${driver}-driver.nvr" ${roots} "${tests}/examples/${driver}-driver.nra")
    file(READ "${tests}/examples/${driver}-driver.out" expected)
    expect_run("${driver} driver" "${TEST_DIR}/${driver}-driver.nvr" "${expected}" 0 "" ${driver_options})
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
    expect_build_failure("${library}" "error(\\[E0803\\])?: the program has no entry point" "${TEST_DIR}/${name}.nvr"
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
foreach(name IN ITEMS operators closures aggregates class-nil generic-closures generic-enums interfaces
                      stdlib-primitives control-and-updates ranges-and-variadics overloads primitive-references
                      last-bindings target-typed-new anonymous-classes)
    neura(emit "${TEST_DIR}" emit-ir "${tests}/programs/${name}.nra" -o "${TEST_DIR}/${name}.nir")
    if(NOT emit_status STREQUAL "0")
        message(FATAL_ERROR "${name}: emitting NIR failed with status ${emit_status}\n${emit_stderr}")
    endif()
    build_image("${TEST_DIR}/${name}-from-nir.nvr" "${TEST_DIR}/${name}.nir")
    same_bytes("${name} from NIR" "${TEST_DIR}/${name}.nvr" "${TEST_DIR}/${name}-from-nir.nvr")
endforeach()

neura(ranges_text "${TEST_DIR}" disassemble "${TEST_DIR}/ranges-and-variadics.nvr"
      -o "${TEST_DIR}/ranges-and-variadics.nvra")
if(NOT ranges_text_status STREQUAL "0")
    message(FATAL_ERROR "range image disassembly failed: ${ranges_text_stderr}")
endif()
neura(ranges_assembled "${TEST_DIR}" assemble "${TEST_DIR}/ranges-and-variadics.nvra"
      -o "${TEST_DIR}/ranges-and-variadics-reassembled.nvr")
if(NOT ranges_assembled_status STREQUAL "0")
    message(FATAL_ERROR "range image assembly failed: ${ranges_assembled_stderr}")
endif()
same_bytes("range image reassembly" "${TEST_DIR}/ranges-and-variadics.nvr"
           "${TEST_DIR}/ranges-and-variadics-reassembled.nvr")

# Anonymous classes are named by their fields (and, for a class of its own,
# where it is written). The tools read those names from the image.
neura(anonymous_text "${TEST_DIR}" disassemble "${TEST_DIR}/anonymous-classes.nvr"
      -o "${TEST_DIR}/anonymous-classes.nvra")
neura(anonymous_assembled "${TEST_DIR}" assemble "${TEST_DIR}/anonymous-classes.nvra"
      -o "${TEST_DIR}/anonymous-classes-reassembled.nvr")
if(NOT anonymous_text_status STREQUAL "0" OR NOT anonymous_assembled_status STREQUAL "0")
    message(FATAL_ERROR "anonymous class image round trip failed: ${anonymous_text_stderr}${anonymous_assembled_stderr}")
endif()
same_bytes("anonymous class image reassembly" "${TEST_DIR}/anonymous-classes.nvr"
           "${TEST_DIR}/anonymous-classes-reassembled.nvr")
foreach(provenance IN ITEMS "" --no-provenance)
    neura(anonymous_decompiled "${TEST_DIR}" decompile ${provenance} "${TEST_DIR}/anonymous-classes.nvr"
          -o "${TEST_DIR}/anonymous-classes${provenance}.decompiled")
    if(NOT anonymous_decompiled_status STREQUAL "0")
        message(FATAL_ERROR "decompiling anonymous classes${provenance} failed: ${anonymous_decompiled_stderr}")
    endif()
endforeach()
file(READ "${TEST_DIR}/anonymous-classes.decompiled" anonymous_decompiled)
if(NOT anonymous_decompiled MATCHES "provenance: class[(]x: int32, label: string[)]" OR
   NOT anonymous_decompiled MATCHES "provenance: Main[.]class[(]count: int32[)]@anonymous-classes[.]nra:42:18")
    message(FATAL_ERROR "decompiled anonymous classes lack their names\n${anonymous_decompiled}")
endif()

# Runtime traps: stdout up to the trap, a diagnostic, and the status of the
# trap's class.
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
    string(REGEX REPLACE "^// expect: ([0-9]+:[0-9]+) (.*)$" "\\1: error(\\\\[[EW][0-9]+\\\\])?: \\2" pattern "${expectation}")
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
if(NOT help_stdout MATCHES "neura explain <code> [|] --explain <code> [|] explain --list" OR
   NOT help_stdout MATCHES "--max-errors N")
    message(FATAL_ERROR "usage should document explain and --max-errors\n${help_stdout}")
endif()
# neura explain prints a code's page, which is embedded in the binary; an
# unknown code is an error that names the closest registered code.
neura(explain "${TEST_DIR}" explain E0401)
if(NOT explain_stdout MATCHES "^# E0401: assignment to a constant binding\n")
    message(FATAL_ERROR "neura explain E0401 should print its page\n${explain_stdout}")
endif()
neura(explain_option "${TEST_DIR}" --explain e0401)
if(NOT explain_option_stdout STREQUAL explain_stdout)
    message(FATAL_ERROR "neura --explain e0401 should print the same page\n${explain_option_stdout}")
endif()
neura(explain_list "${TEST_DIR}" explain --list)
if(NOT explain_list_stdout MATCHES "\nE0401  assignment to a constant binding\n")
    message(FATAL_ERROR "neura explain --list should list the codes\n${explain_list_stdout}")
endif()
expect_failure("unknown code" "'E0499' is not a diagnostic code; did you mean E0409[?]" explain E0499)
