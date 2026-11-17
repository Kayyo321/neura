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
# The lines are those of a run without tier 3: --jit=off leaves the default
# tier 3 off, and the exact text holds whatever the environment of the suite
# asks (NEURA_TIER3, NEURA_TIER_EAGER, NEURA_TIER_PROGRAMS change the lines).
foreach(setting IN ITEMS NEURA_TIER3 NEURA_TIER_EAGER NEURA_TIER_PROGRAMS NEURA_TIER3_AFTER)
    set("saved_${setting}" "$ENV{${setting}}")
    set("saved_${setting}_defined" FALSE)
    if(DEFINED ENV{${setting}})
        set("saved_${setting}_defined" TRUE)
    endif()
    unset(ENV{${setting}})
endforeach()
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
foreach(setting IN ITEMS NEURA_TIER3 NEURA_TIER_EAGER NEURA_TIER_PROGRAMS NEURA_TIER3_AFTER)
    if(saved_${setting}_defined)
        set(ENV{${setting}} "${saved_${setting}}")
    endif()
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
# The default tier 3 (docs/runtime/NVM.md, "Function programs"): with
# NEURA_TIER3 unset a run forms native function programs where native code is
# available (x86-64 Linux, --jit not off); NEURA_TIER3=off, --jit=off and
# NEURA_JIT=off give the run of a build without tier 3, which forms none. The
# runs print the same and take the same steps either way. NEURA_TIER_EAGER=1
# makes the short run form programs.
function(tier3_run prefix tier3 jit)
    set(settings NEURA_TIER3 NEURA_TIER_EAGER NEURA_TIER_PROGRAMS NEURA_TIER3_AFTER NEURA_JIT)
    foreach(setting IN LISTS settings)
        set(was_defined_${setting} FALSE)
        if(DEFINED ENV{${setting}})
            set(was_defined_${setting} TRUE)
            set(was_${setting} "$ENV{${setting}}")
        endif()
        unset(ENV{${setting}})
    endforeach()
    if(NOT tier3 STREQUAL "unset")
        set(ENV{NEURA_TIER3} "${tier3}")
    endif()
    set(ENV{NEURA_TIER_EAGER} 1)
    neura(run "${TEST_DIR}" run --stats --tier-stats --jit=${jit} "${TEST_DIR}/binary-trees.nvr" -- 8)
    foreach(setting IN LISTS settings)
        unset(ENV{${setting}})
        if(was_defined_${setting})
            set(ENV{${setting}} "${was_${setting}}")
        endif()
    endforeach()
    string(REGEX MATCH "stats: steps [0-9]+, peak heap cells [0-9]+" counts "${run_stderr}")
    if(NOT run_status STREQUAL "0" OR counts STREQUAL "")
        message(FATAL_ERROR "binary-trees with NEURA_TIER3=${tier3}, --jit=${jit}: status ${run_status}\n${run_stderr}")
    endif()
    set("${prefix}_stdout" "${run_stdout}" PARENT_SCOPE)
    # The steps the tier line adds up are the run's steps.
    string(REGEX MATCH "stats: steps ([0-9]+)," run_steps "${run_stderr}")
    set(run_steps "${CMAKE_MATCH_1}")
    string(REGEX MATCH "\ntier: steps ([0-9]+):" tier_steps "${run_stderr}")
    if(NOT run_steps STREQUAL CMAKE_MATCH_1)
        message(FATAL_ERROR "tier: steps ${CMAKE_MATCH_1} is not stats: steps ${run_steps} "
                            "(NEURA_TIER3=${tier3}, --jit=${jit})\n${run_stderr}")
    endif()
    set("${prefix}_counts" "${counts}" PARENT_SCOPE)
    set("${prefix}_stderr" "${run_stderr}" PARENT_SCOPE)
    set("${prefix}_native" FALSE PARENT_SCOPE)
    set("${prefix}_programs" FALSE PARENT_SCOPE)
    if(run_stderr MATCHES "\ntier: native programs [1-9]")
        set("${prefix}_native" TRUE PARENT_SCOPE)
    endif()
    if(run_stderr MATCHES "\ntier: programs [1-9]" OR run_stderr MATCHES "\ntier: native")
        set("${prefix}_programs" TRUE PARENT_SCOPE)
    endif()
endfunction()
tier3_run(tier3_default unset auto)
tier3_run(tier3_off off auto)
tier3_run(tier3_nojit unset off)
tier3_run(tier3_bogus bogus auto)
if(NOT tier3_off_stdout STREQUAL trees_stdout OR NOT tier3_default_stdout STREQUAL trees_stdout)
    message(FATAL_ERROR "NEURA_TIER3 changed the output of binary-trees")
endif()
foreach(other IN ITEMS off nojit bogus)
    if(NOT tier3_${other}_counts STREQUAL tier3_default_counts)
        message(FATAL_ERROR "NEURA_TIER3 changed the counts of binary-trees: ${tier3_default_counts} and "
                            "${tier3_${other}_counts} (${other})")
    endif()
    if(tier3_${other}_programs)
        message(FATAL_ERROR "binary-trees formed function programs with tier 3 off (${other})")
    endif()
endforeach()
# With tier 3 off the statistics hold the lines of a run without tier 3 only.
foreach(other IN ITEMS off nojit bogus)
    if(NOT tier3_${other}_stderr MATCHES "\ntier: blocks [0-9]+, hinted [0-9]+" OR
       NOT tier3_${other}_stderr MATCHES "\ntier: steps [0-9]+: tier 2 " OR
       tier3_${other}_stderr MATCHES "\ntier: (no )?(native|program)")
        message(FATAL_ERROR "tier statistics with tier 3 off (${other}):\n${tier3_${other}_stderr}")
    endif()
endforeach()
# (CMAKE_HOST_SYSTEM_PROCESSOR is empty in script mode.)
execute_process(COMMAND uname -m OUTPUT_VARIABLE machine OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND machine MATCHES "^(x86_64|amd64)$")
    if(NOT tier3_default_native)
        message(FATAL_ERROR "the default run of binary-trees formed no native program")
    endif()
else()
    # Hosts without tier 3 native code run as they did before tier 3.
    if(tier3_default_programs)
        message(FATAL_ERROR "the default run of binary-trees formed function programs on a host without tier 3 code")
    endif()
endif()

# A block that adds 300 parameters is too large for the form search, which
# would otherwise need gigabytes (tests/backend/tier/wide-sum.nra): the run
# finishes in seconds, the block stays on tier 0, and the statistics say why.
build_image("${TEST_DIR}/wide-sum.nvr" "${tests}/tier/wide-sum.nra")
set(ENV{NEURA_TIER_EAGER} 1)
execute_process(COMMAND "${NEURA}" run --tier-stats "${TEST_DIR}/wide-sum.nvr" TIMEOUT 120
                RESULT_VARIABLE wide_status OUTPUT_VARIABLE wide_stdout ERROR_VARIABLE wide_stderr)
unset(ENV{NEURA_TIER_EAGER})
if(NOT wide_status STREQUAL "0" OR NOT wide_stdout STREQUAL "sum 3591\n" OR
   NOT wide_stderr MATCHES "tier: not lifted [1-9][0-9]*: not recognized: graph too large to search")
    message(FATAL_ERROR "wide-sum: status ${wide_status}\n${wide_stdout}${wide_stderr}")
endif()
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

# Function programs (NEURA_TIER_PROGRAMS=1): a run whose console writes happen
# in called functions records the trace tier 0 records, and replays it.
build_image("${TEST_DIR}/program-calls.nvr" "${tests}/tier/program-calls.nra")
neura(calls_t0 "${TEST_DIR}" run --tier=0 --record "${TEST_DIR}/calls-t0.trace" "${TEST_DIR}/program-calls.nvr" -- calls 6)
set(ENV{NEURA_TIER_PROGRAMS} 1)
set(ENV{NEURA_TIER_EAGER} 1)
neura(calls_programs "${TEST_DIR}" run --tier-stats --record "${TEST_DIR}/calls-programs.trace"
      "${TEST_DIR}/program-calls.nvr" -- calls 6)
neura(calls_replay "${TEST_DIR}" run --replay "${TEST_DIR}/calls-t0.trace" "${TEST_DIR}/program-calls.nvr" -- calls 6)
unset(ENV{NEURA_TIER_EAGER})
unset(ENV{NEURA_TIER_PROGRAMS})
if(NOT calls_t0_status STREQUAL "0" OR NOT calls_programs_status STREQUAL "0" OR
   NOT calls_programs_stdout STREQUAL calls_t0_stdout OR NOT calls_programs_stderr MATCHES "tier: programs [1-9]")
    message(FATAL_ERROR "program-calls with function programs: status ${calls_programs_status}\n"
                        "${calls_programs_stdout}${calls_programs_stderr}")
endif()
same_bytes("a trace recorded with function programs" "${TEST_DIR}/calls-t0.trace" "${TEST_DIR}/calls-programs.trace")
if(NOT calls_replay_status STREQUAL "0" OR NOT calls_replay_stdout STREQUAL calls_t0_stdout)
    message(FATAL_ERROR "program-calls replayed with function programs: status ${calls_replay_status}\n"
                        "${calls_replay_stdout}${calls_replay_stderr}")
endif()

# Allocation, text and kernel calls run as helpers inside programs, and the run
# prints what tier 0 prints.
build_image("${TEST_DIR}/program-helpers.nvr" "${tests}/tier/program-helpers.nra")
foreach(helper_case IN ITEMS alloc kernel text)
    neura(helpers_t0 "${TEST_DIR}" run --tier=0 "${TEST_DIR}/program-helpers.nvr" -- ${helper_case} 9)
    set(ENV{NEURA_TIER_PROGRAMS} 1)
    set(ENV{NEURA_TIER_EAGER} 1)
    neura(helpers_programs "${TEST_DIR}" run --tier-stats "${TEST_DIR}/program-helpers.nvr" -- ${helper_case} 9)
    unset(ENV{NEURA_TIER_EAGER})
    unset(ENV{NEURA_TIER_PROGRAMS})
    if(NOT helpers_t0_status STREQUAL "0" OR NOT helpers_programs_status STREQUAL "0" OR
       NOT helpers_programs_stdout STREQUAL helpers_t0_stdout OR
       NOT helpers_programs_stderr MATCHES "tier: programs [1-9][^\n]*helpers [1-9]")
        message(FATAL_ERROR "program-helpers ${helper_case} with function programs: status ${helpers_programs_status}\n"
                            "${helpers_programs_stdout}${helpers_programs_stderr}")
    endif()
endforeach()

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
# Startup blocks (Modules.md, Startup blocks): source order across files, a
# file imported twice, an unused file, an import cycle, a module's block, and
# a class first used in a block, from source and from .nir, and a library
# whose blocks a second .nir module links again runs them once.
foreach(mode IN ITEMS "" --no-optimize)
    build_image("${TEST_DIR}/startup${mode}.nvr" ${mode} "${tests}/startup/main.nra")
    file(READ "${tests}/startup/main.out" expected)
    expect_run("startup blocks ${mode}" "${TEST_DIR}/startup${mode}.nvr" "${expected}" 0 "")
endforeach()
neura(emit "${TEST_DIR}" emit-ir "${tests}/startup/main.nra" -o "${TEST_DIR}/startup.nir")
build_image("${TEST_DIR}/startup-from-nir.nvr" "${TEST_DIR}/startup.nir")
same_bytes("startup blocks from .nir" "${TEST_DIR}/startup.nvr" "${TEST_DIR}/startup-from-nir.nvr")
file(MAKE_DIRECTORY "${TEST_DIR}/startup-nir")
neura(emit "${TEST_DIR}" emit-ir "${tests}/startup/registry.nra" -o "${TEST_DIR}/startup-nir/registry.nir")
file(WRITE "${TEST_DIR}/startup-nir/user.nra" [=[
let registry = import:"registry.nir";
export let User = module (
    export let count = func() -> int32 => registry.count;
);
]=])
neura(emit "${TEST_DIR}/startup-nir" emit-ir "${TEST_DIR}/startup-nir/user.nra" -o "${TEST_DIR}/startup-nir/user.nir")
file(WRITE "${TEST_DIR}/startup-nir/app.nra" [=[
let registry = import:"registry.nir";
let user = import:"user.nir";
let console = import:Std.Io.Console;
let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line((registry.count + user.count()).to_string());
        return program.ExitCode.Ok;
    };
);
]=])
build_image("${TEST_DIR}/startup-nir/app.nvr" "${TEST_DIR}/startup-nir/app.nra")
expect_run("startup blocks of a .nir module linked twice" "${TEST_DIR}/startup-nir/app.nvr"
    "registry: block, count 2\n4\n" 0 "")
expect_build_failure("startup library" "error(\\[E0803\\])?: the program has no entry point"
    "${TEST_DIR}/startup-library.nvr" "${tests}/startup/registry.nra")
file(READ "${tests}/testing/startup-suite.out" expected)
expect_tests("startup blocks run again for each test" "${expected}" 0 "${tests}/testing/startup-suite.nra")

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
                      last-bindings target-typed-new anonymous-classes inline-tuples inline-tuples-tasks)
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

# Storage regions (NVR 1.7): --stats reports the peak stack cells of an image that uses regions and
# of no other, --max-stack-cells limits them, and a trap of the region operations is the status of
# its class. The compiler does not emit regions yet, so the images are assembled from text.
set(region_output "2\n1\n300\n5\n77\n18\n150\n42\n43\n44\n100\n99\n8\n")
neura(region_assemble "${TEST_DIR}" assemble "${tests}/tier/region-storage.nvra" -o "${TEST_DIR}/region-storage.nvr")
if(NOT region_assemble_status STREQUAL "0")
    message(FATAL_ERROR "assembling the region image failed\n${region_assemble_stderr}")
endif()
neura(region_tasks_assemble "${TEST_DIR}" assemble "${tests}/tier/region-tasks.nvra" -o "${TEST_DIR}/region-tasks.nvr")
neura(region_stats "${TEST_DIR}" run --stats --tier=0 "${TEST_DIR}/region-storage.nvr")
if(NOT region_stats_status STREQUAL "0" OR NOT region_stats_stdout STREQUAL region_output OR
   NOT region_stats_stderr MATCHES "stats: steps [0-9]+, peak heap cells [0-9]+, peak stack cells 160, host requests 13,")
    message(FATAL_ERROR "--stats of a region image\n${region_stats_stdout}${region_stats_stderr}")
endif()
neura(plain_stats "${TEST_DIR}" run --stats "${TEST_DIR}/hello.nvr")
if(plain_stats_stderr MATCHES "stack cells")
    message(FATAL_ERROR "--stats of an image without regions mentions stack cells\n${plain_stats_stderr}")
endif()
neura(task_stats "${TEST_DIR}" run --stats "${TEST_DIR}/region-tasks.nvr")
if(NOT task_stats_stdout STREQUAL "11\n22\n5\n" OR NOT task_stats_stderr MATCHES "peak heap cells [0-9]+, peak stack cells 10,")
    message(FATAL_ERROR "--stats of a region image with tasks\n${task_stats_stdout}${task_stats_stderr}")
endif()
file(WRITE "${TEST_DIR}/region-lists.txt" "one\n")
neura(region_each "${TEST_DIR}" run --stats --each "${TEST_DIR}/region-lists.txt" "${TEST_DIR}/region-storage.nvr")
if(NOT region_each_stderr MATCHES "peak heap cells [0-9]+, peak stack cells 160,")
    message(FATAL_ERROR "--stats of --each over a region image\n${region_each_stderr}")
endif()
foreach(tier IN ITEMS 0 1 2)
    expect_run("region image at tier ${tier}" "${TEST_DIR}/region-storage.nvr" "${region_output}" 0 "" --tier=${tier})
endforeach()
expect_run("stack limit" "${TEST_DIR}/region-storage.nvr" "${region_output}" 0 "" --max-stack-cells 160)
neura(stack_limit "${TEST_DIR}" run --max-stack-cells 159 "${TEST_DIR}/region-storage.nvr")
if(NOT stack_limit_status STREQUAL "139" OR NOT stack_limit_stderr MATCHES "trap: stack limit exceeded: region allocation of 4 cells")
    message(FATAL_ERROR "--max-stack-cells 159: status ${stack_limit_status}\n${stack_limit_stderr}")
endif()
neura(region_trap "${TEST_DIR}" run "${TEST_DIR}/region-storage.nvr" -- x)
if(NOT region_trap_status STREQUAL "139" OR NOT region_trap_stdout STREQUAL "${region_output}" OR
   NOT region_trap_stderr MATCHES "trap: released storage accessed: handle [0-9]+")
    message(FATAL_ERROR "a read after release: status ${region_trap_status}\n${region_trap_stderr}")
endif()
neura(region_replay_record "${TEST_DIR}" run --record "${TEST_DIR}/region-trap.trace" "${TEST_DIR}/region-storage.nvr" -- x)
neura(region_replay "${TEST_DIR}" run --replay "${TEST_DIR}/region-trap.trace" "${TEST_DIR}/region-storage.nvr" -- x)
if(NOT region_replay_status STREQUAL "139" OR NOT region_replay_stderr MATCHES "trap: released storage accessed")
    message(FATAL_ERROR "replay of a region trap: status ${region_replay_status}\n${region_replay_stderr}")
endif()
file(READ "${TEST_DIR}/region-trap.trace" region_trace)
if(NOT region_trace MATCHES "outcome trap released-storage ")
    message(FATAL_ERROR "the trace file does not name the trap\n${region_trace}")
endif()
neura(invalid_region "${TEST_DIR}" run "${TEST_DIR}/region-storage.nvr" -- x x x x)
if(NOT invalid_region_status STREQUAL "132" OR NOT invalid_region_stderr MATCHES "trap: invalid region operation: region [0-9]+ does not enclose")
    message(FATAL_ERROR "a move to a region that does not enclose: ${invalid_region_status}\n${invalid_region_stderr}")
endif()
expect_failure("--max-stack-cells on build" "applies only to run and test" build --max-stack-cells 5 "${hello}" -o "${TEST_DIR}/never.nvr")
expect_failure("--max-stack-cells needs a number" "expected a positive integer" run --max-stack-cells 0 "${TEST_DIR}/hello.nvr")

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
