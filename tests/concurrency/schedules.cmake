# Testing concurrent code (docs/lang/Concurrency.md section 14.4): virtual
# time, schedule exploration from a seed, and the reproduction of a saved
# failing schedule.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(sources "${SOURCE_DIR}/tests/concurrency/schedules")
set(order "${sources}/order-tests.nra")

function(neura prefix)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 60
    )
    set("${prefix}_status" "${status}" PARENT_SCOPE)
    set("${prefix}_stdout" "${stdout}" PARENT_SCOPE)
    set("${prefix}_stderr" "${stderr}" PARENT_SCOPE)
endfunction()

function(expect label status pattern)
    neura(result ${ARGN})
    if(NOT result_status STREQUAL "${status}" OR NOT "${result_stdout}${result_stderr}" MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: expected status ${status} and '${pattern}', got ${result_status}\n${result_stdout}\n${result_stderr}")
    endif()
endfunction()

# The reference scheduler never shows the bug.
neura(reference test "${order}")
if(NOT reference_status STREQUAL "0" OR
   NOT reference_stdout STREQUAL "PASS OrderTests.test_sum_is_order_free\nPASS OrderTests.test_assumes_spawn_order\nPASS OrderTests.test_without_tasks\n3 test(s), 3 passed, 0 failed\n")
    message(FATAL_ERROR "reference schedule: status ${reference_status}\n${reference_stdout}\n${reference_stderr}")
endif()

# Exploration finds it, says which schedule and seed, counts what it ran, and
# does the same on every run. A test with one task has one schedule.
neura(explored test --schedules 8 --save-schedule failing.schedule "${order}")
if(NOT explored_status STREQUAL "1" OR
   NOT explored_stdout MATCHES "^PASS OrderTests.test_sum_is_order_free \\[8 schedules\\]\nFAIL OrderTests.test_assumes_spawn_order \\[schedule ([2-8]) of 8, seed ([0-9]+)\\]: expected <1>, got <2> [^\n]*order-tests.nra:39:9\nPASS OrderTests.test_without_tasks \\[1 schedule\\]\nexplored ([0-9]+) schedule\\(s\\) of 3 test\\(s\\), seed 0\n3 test\\(s\\), 2 passed, 1 failed\n$")
    message(FATAL_ERROR "exploration: status ${explored_status}\n${explored_stdout}\n${explored_stderr}")
endif()
set(failing_number "${CMAKE_MATCH_1}")
set(failing_seed "${CMAKE_MATCH_2}")
math(EXPR expected_total "8 + ${failing_number} + 1")
if(NOT CMAKE_MATCH_3 EQUAL expected_total)
    message(FATAL_ERROR "exploration: the report counts ${CMAKE_MATCH_3} schedules, expected ${expected_total}\n${explored_stdout}")
endif()
if(NOT explored_stderr MATCHES "neura: note: the failing schedule of OrderTests.test_assumes_spawn_order was written to failing.schedule\n")
    message(FATAL_ERROR "exploration: no note about the saved schedule\n${explored_stderr}")
endif()
neura(again test --schedules 8 "${order}")
if(NOT again_stdout STREQUAL explored_stdout)
    message(FATAL_ERROR "exploration: a second run with the same seed differs\n${again_stdout}")
endif()
# Another seed is another set of schedules; one schedule is the reference.
expect("one schedule" 0 "PASS OrderTests.test_assumes_spawn_order\n" test --schedules 1 --schedule-seed 7 "${order}")

# The saved schedule names the test, the seed, and each choice that was not
# the reference scheduler's, and reproduces the failure alone.
file(READ "${TEST_DIR}/failing.schedule" saved)
if(NOT saved MATCHES "^neura-schedule 1\ntest OrderTests.test_assumes_spawn_order\nseed ${failing_seed}\nquantum 1024\nvirtual-time off\n(choose [0-9]+ [1-9][0-9]*\n)+$")
    message(FATAL_ERROR "the saved schedule is not in the documented form\n${saved}")
endif()
neura(reproduced test --schedule failing.schedule "${order}")
if(NOT reproduced_status STREQUAL "1" OR
   NOT reproduced_stdout MATCHES "^FAIL OrderTests.test_assumes_spawn_order \\[saved schedule\\]: expected <1>, got <2> [^\n]*\n1 test\\(s\\), 0 passed, 1 failed\n$")
    message(FATAL_ERROR "reproduction: status ${reproduced_status}\n${reproduced_stdout}\n${reproduced_stderr}")
endif()

# A schedule that asks for a task that is not there belongs to another
# program, and a file that is not a schedule is refused before any test runs.
file(CONFIGURE OUTPUT "${TEST_DIR}/other.schedule" CONTENT "neura-schedule 1\ntest OrderTests.test_assumes_spawn_order\nseed 1\nquantum 1024\nvirtual-time off\nchoose 1 9\n" NEWLINE_STYLE LF)
expect("foreign schedule" 1 "FAIL OrderTests.test_assumes_spawn_order \\[saved schedule\\]: the schedule does not fit this program: at decision 1 "
    test --schedule other.schedule "${order}")
file(CONFIGURE OUTPUT "${TEST_DIR}/unknown.schedule" CONTENT "neura-schedule 1\ntest OrderTests.test_missing\nseed 1\nquantum 1024\nvirtual-time off\n" NEWLINE_STYLE LF)
expect("unknown test" 1 "unknown.schedule: the schedule is for test OrderTests.test_missing, which these files do not define"
    test --schedule unknown.schedule "${order}")
foreach(case IN ITEMS
        "neura-schedule 2\ntest a\nseed 1\nquantum 1\nvirtual-time off\n|:1: unsupported schedule version"
        "neura-schedule 1\nseed 1\n|:2: expected a 'test' line"
        "neura-schedule 1\ntest a\nseed x\nquantum 1\nvirtual-time off\n|:3: expected an unsigned integer"
        "neura-schedule 1\ntest a\nseed 1\nquantum 0\nvirtual-time off\n|:4: the quantum must be positive"
        "neura-schedule 1\ntest a\nseed 1\nquantum 1\nvirtual-time maybe\n|:5: expected virtual-time on or off"
        "neura-schedule 1\ntest a\nseed 1\nquantum 1\nvirtual-time off\nchoose 4 0\n|:6: a recorded choice is never the first runnable task"
        "neura-schedule 1\ntest a\nseed 1\nquantum 1\nvirtual-time off\nchoose 4 1\nchoose 4 1\n|:7: choices must be in ascending order of tick"
        "neura-schedule 1\ntest a\nseed 1\nquantum 1\nvirtual-time off\nrun 4 1\n|:6: expected 'choose <tick> <position>'")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 text)
    list(GET parts 1 message)
    file(CONFIGURE OUTPUT "${TEST_DIR}/bad.schedule" CONTENT "${text}" NEWLINE_STYLE LF)
    expect("malformed schedule" 1 "bad.schedule${message}" test --schedule bad.schedule "${order}")
endforeach()

# Options are refused where they do not apply.
expect("schedules on run" 1 "--schedules applies only to test" run --schedules 2 "${order}")
expect("schedule with exploration" 1 "--schedule cannot be combined with --schedules, --schedule-seed, or --save-schedule"
    test --schedule failing.schedule --schedules 2 "${order}")
expect("zero schedules" 1 "expected a positive integer after --schedules" test --schedules 0 "${order}")
expect("bad seed" 1 "expected an unsigned integer after --schedule-seed" test --schedule-seed -1 "${order}")
expect("virtual time on build" 1 "--virtual-time applies only to run and test" build --virtual-time "${order}" -o x.nvr)

# Virtual time: a two-hour timer and a two-hour sleep finish at once (the
# process is stopped after 60 seconds otherwise) and move the clock exactly.
neura(virtual test --virtual-time "${sources}/virtual-time-tests.nra")
if(NOT virtual_status STREQUAL "0" OR NOT virtual_stdout MATCHES "\n3 test\\(s\\), 3 passed, 0 failed\n$")
    message(FATAL_ERROR "virtual time under test: status ${virtual_status}\n${virtual_stdout}\n${virtual_stderr}")
endif()
neura(build_sleeper build "${sources}/sleeper.nra" -o sleeper.nvr)
neura(sleeper run --virtual-time sleeper.nvr)
if(NOT sleeper_status STREQUAL "0" OR NOT sleeper_stdout STREQUAL "slept 7200000000000 ns; the two timers fired in order: short long\n")
    message(FATAL_ERROR "virtual time under run: status ${sleeper_status}\n${sleeper_stdout}\n${sleeper_stderr}\n${build_sleeper_stderr}")
endif()
# A recorded virtual-time run replays without the option and without a clock.
neura(recorded run --virtual-time --record sleeper.trace sleeper.nvr)
neura(replayed run --deny-all --replay sleeper.trace sleeper.nvr)
if(NOT recorded_status STREQUAL "0" OR NOT replayed_status STREQUAL "0" OR NOT replayed_stdout STREQUAL sleeper_stdout)
    message(FATAL_ERROR "virtual time: the recorded run does not replay\n${recorded_stderr}\n${replayed_stdout}\n${replayed_stderr}")
endif()
expect("virtual time with replay" 1 "--virtual-time cannot be combined with --replay" run --virtual-time --replay sleeper.trace sleeper.nvr)

# The service fixture of the phase 3 gate (section 16): a bounded queue sheds
# overload, a slow dependency is timed out, and shutdown drains what was
# accepted, with the same answers under every schedule explored. Its 50 ms
# budget and 200 ms dependency are virtual, so the run takes no real time.
neura(service test --virtual-time --schedules 6 "${sources}/service-tests.nra")
if(NOT service_status STREQUAL "0" OR
   NOT service_stdout MATCHES "^PASS ServiceTests.test_a_slow_dependency_is_timed_out \\[6 schedules\\]\nPASS ServiceTests.test_overload_is_shed_and_shutdown_drains_the_queue \\[6 schedules\\]\nexplored 12 schedule\\(s\\) of 2 test\\(s\\), seed 0\n2 test\\(s\\), 2 passed, 0 failed\n$")
    message(FATAL_ERROR "service fixture: status ${service_status}\n${service_stdout}\n${service_stderr}")
endif()

# Guarded state and atomics used as section 5.4 allows hold under every
# schedule explored, in both builds. The unoptimized build reads every field
# where it is written: eliminate-common-subexpressions may reuse a read across
# a poll, which the memory model allows (docs/IrOptimization.md, "Tasks,
# polls, and scheduler operations"), and that would hide a change that an
# escaped reference made. With a guard that released before the body, the
# exclusivity test fails in both builds.
foreach(build IN ITEMS --no-optimize --optimize)
    if(build STREQUAL "--optimize")
        neura(guarded test --schedules 50 --task-quantum 1 "${sources}/guarded-tests.nra")
    else()
        neura(guarded test --no-optimize --schedules 50 --task-quantum 1 "${sources}/guarded-tests.nra")
    endif()
    if(NOT guarded_status STREQUAL "0" OR
       NOT guarded_stdout MATCHES "^PASS GuardedTests.test_guarded_updates_are_exclusive \\[50 schedules\\]\nPASS GuardedTests.test_atomic_additions_are_exact \\[50 schedules\\]\nPASS GuardedTests.test_guarded_with_a_transferable_state \\[1 schedule\\]\n")
        message(FATAL_ERROR "guarded state (${build}): status ${guarded_status}\n${guarded_stdout}\n${guarded_stderr}")
    endif()
endforeach()

# LockedPointer and AtomicPointer shared between tasks hold under every
# schedule explored, in both builds.
foreach(build IN ITEMS --no-optimize --optimize)
    if(build STREQUAL "--optimize")
        neura(pointers test --schedules 50 --task-quantum 1 "${sources}/pointer-tests.nra")
    else()
        neura(pointers test --no-optimize --schedules 50 --task-quantum 1 "${sources}/pointer-tests.nra")
    endif()
    if(NOT pointers_status STREQUAL "0" OR
       NOT pointers_stdout MATCHES "^PASS PointerTests.test_locked_updates_are_exact \\[50 schedules\\]\nPASS PointerTests.test_locked_mutable_state_is_exclusive \\[50 schedules\\]\nPASS PointerTests.test_atomic_loads_are_live \\[50 schedules\\]\n")
        message(FATAL_ERROR "pointers (${build}): status ${pointers_status}\n${pointers_stdout}\n${pointers_stderr}")
    endif()
endforeach()

message(STATUS "concurrency schedules: exploration, reproduction, virtual time, the service fixture, guarded state, and pointers passed")
