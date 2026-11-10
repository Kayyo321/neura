# End-to-end evidence for scoped tasks (docs/lang/Concurrency.md, phase 1):
# each fixture under programs/ is built from source with and without
# optimization and through a .nir file, and every build must give the same
# output and status under each quantum, run after run. The default quantum
# is compared with the recorded <name>.expected. Then the image-level tools are
# checked on the same images without their sources.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(programs "${SOURCE_DIR}/tests/concurrency/programs")

function(neura prefix)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    set("${prefix}_status" "${status}" PARENT_SCOPE)
    set("${prefix}_stdout" "${stdout}" PARENT_SCOPE)
    set("${prefix}_stderr" "${stderr}" PARENT_SCOPE)
endfunction()

function(must label)
    neura(result ${ARGN})
    if(NOT result_status STREQUAL "0")
        message(FATAL_ERROR "${label}: ${ARGN} failed with status ${result_status}\n${result_stderr}")
    endif()
    set(must_stdout "${result_stdout}" PARENT_SCOPE)
    set(must_stderr "${result_stderr}" PARENT_SCOPE)
endfunction()

function(same_bytes label left right)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${TEST_DIR}/${left}" "${TEST_DIR}/${right}"
        RESULT_VARIABLE different)
    if(NOT different STREQUAL "0")
        message(FATAL_ERROR "${label}: ${left} and ${right} differ")
    endif()
endfunction()

# name, expected status, and a pattern the standard error of a run must match
# (empty: standard error must be empty). Extra arguments are run options.
set(fixtures fan-out values detached child-trap child-exit task-limit)
set(status_fan-out 0)
set(status_values 0)
set(status_detached 0)
set(status_child-trap 136)
set(stderr_child-trap "child-trap.nvr: trap: source arithmetic failure \\(trap reason 1\\): \\(task 2 spawned at child-trap.nra:10:21 by task 1 spawned at child-trap.nra:18:24 by task 0\\) in function [0-9]+ block [0-9]+ at child-trap.nra:6:50\n$")
set(status_child-exit 7)
set(status_task-limit 0)

file(GLOB found "${programs}/*.nra")
list(LENGTH found found_count)
list(LENGTH fixtures fixture_count)
if(NOT found_count EQUAL fixture_count)
    message(FATAL_ERROR "programs/ holds ${found_count} fixtures but ${fixture_count} are listed")
endif()

set(quanta 1 2 3 7 64 1024)
set(compared 0)
foreach(name IN LISTS fixtures)
    set(source "${programs}/${name}.nra")
    file(READ "${programs}/${name}.expected" expected)
    must("${name}" build "${source}" -o "${name}.nvr")
    must("${name}" build --no-optimize "${source}" -o "${name}.unoptimized.nvr")
    must("${name}" emit-ir "${source}" -o "${name}.nir")
    must("${name}" build "${name}.nir" -o "${name}.from-nir.nvr")
    same_bytes("${name}: source and .nir routes" "${name}.nvr" "${name}.from-nir.nvr")
    must("${name}" build "${source}" -o "${name}.again.nvr")
    same_bytes("${name}: repeated build" "${name}.nvr" "${name}.again.nvr")

    # The recorded output, at the default quantum.
    neura(run run "${name}.nvr")
    if(NOT run_status STREQUAL "${status_${name}}" OR NOT run_stdout STREQUAL "${expected}")
        message(FATAL_ERROR "${name}: expected status ${status_${name}} and the recorded output, got ${run_status}\n${run_stdout}\n${run_stderr}")
    endif()
    if(DEFINED stderr_${name})
        if(NOT run_stderr MATCHES "${stderr_${name}}")
            message(FATAL_ERROR "${name}: unexpected diagnostic\n${run_stderr}")
        endif()
    elseif(NOT run_stderr STREQUAL "")
        message(FATAL_ERROR "${name}: unexpected standard error\n${run_stderr}")
    endif()

    # Optimized and unoptimized builds agree under every quantum, and a
    # repeated run from clean state reproduces each of them, statistics
    # included.
    set(distinct "")
    foreach(quantum IN LISTS quanta)
        neura(optimized run --stats --task-quantum ${quantum} "${name}.nvr")
        neura(repeated run --stats --task-quantum ${quantum} "${name}.nvr")
        neura(unoptimized run --task-quantum ${quantum} "${name}.unoptimized.nvr")
        if(NOT optimized_status STREQUAL "${status_${name}}")
            message(FATAL_ERROR "${name}, quantum ${quantum}: status ${optimized_status}\n${optimized_stderr}")
        endif()
        if(NOT repeated_status STREQUAL optimized_status OR NOT repeated_stdout STREQUAL optimized_stdout OR
           NOT repeated_stderr STREQUAL optimized_stderr)
            message(FATAL_ERROR "${name}, quantum ${quantum}: a repeated run differs\n${optimized_stderr}\n${repeated_stderr}")
        endif()
        if(NOT unoptimized_status STREQUAL optimized_status OR NOT unoptimized_stdout STREQUAL optimized_stdout)
            message(FATAL_ERROR "${name}, quantum ${quantum}: optimized and unoptimized builds differ\n${optimized_stdout}\n--\n${unoptimized_stdout}")
        endif()
        if(NOT optimized_stderr MATCHES "peak tasks [1-9]")
            message(FATAL_ERROR "${name}: --stats does not report peak tasks\n${optimized_stderr}")
        endif()
        string(SHA256 digest "${optimized_stdout}")
        list(APPEND distinct "${digest}")
        math(EXPR compared "${compared} + 1")
    endforeach()
    list(REMOVE_DUPLICATES distinct)
    list(LENGTH distinct schedules_${name})

    # The image without its sources: lossless assembly, and decompilation
    # of a build that carries neither a source map nor names.
    must("${name}" disassemble "${name}.nvr" -o "${name}.nvra")
    must("${name}" assemble "${name}.nvra" -o "${name}.reassembled.nvr")
    same_bytes("${name}: assembly round trip" "${name}.nvr" "${name}.reassembled.nvr")
    file(READ "${TEST_DIR}/${name}.nvra" assembly)
    if(NOT assembly MATCHES "\nnvr 1\\.5 profile 1\n" OR NOT assembly MATCHES "exit scheduler scope-enter" OR
       NOT assembly MATCHES "exit scheduler spawn" OR NOT assembly MATCHES "transferable-layouts \\[")
        message(FATAL_ERROR "${name}: the assembly does not show a 1.5 task image")
    endif()
    must("${name}" build --no-source-map "${source}" -o "${name}.bare.nvr")
    must("${name}" decompile "${name}.bare.nvr")
    if(NOT must_stdout MATCHES "scope-enter \\[decoded\\] \\[reconstructed: scope " OR
       NOT must_stdout MATCHES "spawn f[0-9]+\\([^)]*\\) \\[decoded\\]")
        message(FATAL_ERROR "${name}: decompilation does not label scheduler operations\n${must_stdout}")
    endif()
    if(must_stdout MATCHES "scope pair unresolved")
        message(FATAL_ERROR "${name}: the compiler's own scopes were not paired\n${must_stdout}")
    endif()
endforeach()

# The quantum changes the interleaving of these fixtures, so the comparison
# above is not between runs that happen to be identical.
if(schedules_fan-out LESS 3)
    message(FATAL_ERROR "fan-out printed ${schedules_fan-out} distinct outputs across quanta; expected at least 3")
endif()

# Polls ride on call and back-edge exits of a program that spawns.
file(READ "${TEST_DIR}/fan-out.nvra" assembly)
if(NOT assembly MATCHES "exit call poll" OR NOT assembly MATCHES "exit continue poll")
    message(FATAL_ERROR "fan-out carries no polls on calls and back edges")
endif()
# A handle that is discarded gives a spawn without a result slot.
file(READ "${TEST_DIR}/detached.nvra" assembly)
if(NOT assembly MATCHES "exit scheduler spawn target [0-9]+ arguments \\[ [^]]* \\] result - ")
    message(FATAL_ERROR "detached does not spawn without a result slot")
endif()

# A program without tasks carries no poll and no 1.5 feature.
must("hello" build "${SOURCE_DIR}/examples/hello-world/hello-world.nra" -o hello.nvr)
must("hello" disassemble hello.nvr)
if(must_stdout MATCHES " poll " OR must_stdout MATCHES "transferable-layouts" OR must_stdout MATCHES "\nnvr 1\\.5 ")
    message(FATAL_ERROR "a program without tasks was built as a task image")
endif()

# Tasks whose handle is discarded do not accumulate: 20,000 of them fit in
# a heap far smaller than their records would need together (20 cells each).
# The loop polls once per iteration, so the root starts 1,024 tasks in each
# turn: at most those and the root are live.
neura(bounded run --stats --max-heap-cells 60000 detached.nvr)
if(NOT bounded_status STREQUAL "0" OR NOT bounded_stderr MATCHES "peak tasks 1025, peak queued messages 0, peak pending requests 0\n")
    message(FATAL_ERROR "detached: expected the tasks of one quantum to be live at most\n${bounded_stderr}")
endif()

# Limits and their statuses.
neura(limited run --max-tasks 16 task-limit.nvr)
if(NOT limited_status STREQUAL "137" OR NOT limited_stdout STREQUAL "5\n" OR
   NOT limited_stderr MATCHES "trap: task limit exceeded: spawning another task \\(task 20 spawned at task-limit.nra:10:22 by task 19 ")
    message(FATAL_ERROR "task-limit: expected the task limit trap\n${limited_status}\n${limited_stdout}\n${limited_stderr}")
endif()
if(NOT limited_stderr MATCHES " \\.\\.\\. 7 more spawns \\.\\.\\. ")
    message(FATAL_ERROR "task-limit: a long spawn chain is not abbreviated\n${limited_stderr}")
endif()
foreach(option IN ITEMS --max-tasks --task-quantum --max-queued-messages --max-pending-requests)
    foreach(value IN ITEMS 0 -1 x 18446744073709551616)
        neura(bad run ${option} ${value} fan-out.nvr)
        if(bad_status STREQUAL "0" OR NOT bad_stdout STREQUAL "")
            message(FATAL_ERROR "${option} ${value} was accepted")
        endif()
    endforeach()
endforeach()
foreach(command IN ITEMS build check emit-ir disassemble)
    foreach(option IN ITEMS "--max-tasks;4" "--task-quantum;4" "--max-queued-messages;4"
            "--max-pending-requests;4" "--task-dump")
        neura(bad ${command} ${option} "${programs}/fan-out.nra")
        if(bad_status STREQUAL "0" OR NOT bad_stderr MATCHES "applies only to run and test")
            message(FATAL_ERROR "${command} accepted ${option}\n${bad_stderr}")
        endif()
    endforeach()
endforeach()

# Task dumps. A trap prints the trapping task and its spawn chain always,
# and the whole tree with --task-dump.
neura(dump run --task-dump child-trap.nvr)
if(NOT dump_status STREQUAL "136" OR
   NOT dump_stderr MATCHES "^task 0  root  parked: join of task 1\n    [^\n]*join at std/concurrent/task.nra:[0-9:]+\n    Main.main at child-trap.nra:20:13\n" OR
   NOT dump_stderr MATCHES "\n  task 1  spawned at child-trap.nra:18:24  parked: join of task 2\n" OR
   NOT dump_stderr MATCHES "\n    task 2  spawned at child-trap.nra:10:21  running\n        divide at child-trap.nra:6:[0-9]+\n" OR
   NOT dump_stderr MATCHES "child-trap.nvr: trap: source arithmetic failure")
    message(FATAL_ERROR "child-trap: unexpected task dump\n${dump_stderr}")
endif()
neura(json run --task-dump=json child-trap.nvr)
if(NOT json_status STREQUAL "136" OR
   NOT json_stderr MATCHES "^{\"tasks\":\\[{\"id\":0,\"parent\":null,\"spawned_at\":null,\"state\":\"parked: join of task 1\"" OR
   NOT json_stderr MATCHES "{\"id\":2,\"parent\":1,\"spawned_at\":\"child-trap.nra:10:21\",\"state\":\"running\",\"stack\":\\[\"divide at child-trap.nra:6:[0-9]+\"")
    message(FATAL_ERROR "child-trap: unexpected JSON task dump\n${json_stderr}")
endif()
neura(bad run --task-dump=xml child-trap.nvr)
if(bad_status STREQUAL "0" OR NOT bad_stderr MATCHES "--task-dump expects text or json")
    message(FATAL_ERROR "--task-dump=xml was accepted")
endif()
# Without source map or names the dump shows function and block numbers.
neura(bare run --task-dump child-trap.bare.nvr)
if(NOT bare_stderr MATCHES "\n    task 2  spawned at function [0-9]+ block [0-9]+  running\n        function [0-9]+ block [0-9]+\n")
    message(FATAL_ERROR "child-trap: the dump of a bare image names no function and block\n${bare_stderr}")
endif()

# SIGQUIT asks a running image for a dump and the run continues to its end.
if(UNIX)
    must("spin" build "${SOURCE_DIR}/tests/concurrency/spin.nra" -o spin.nvr)
    execute_process(
        COMMAND sh -c "\"$0\" run --max-steps 30000000 spin.nvr 2> spin.err & pid=$!; sleep 1; kill -QUIT $pid; sleep 1; kill -QUIT $pid; wait $pid; echo $? > spin.status" "${NEURA}"
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE shell_status)
    file(READ "${TEST_DIR}/spin.err" spin_err)
    file(READ "${TEST_DIR}/spin.status" spin_status)
    string(STRIP "${spin_status}" spin_status)
    string(REGEX MATCHALL "task 0  root  parked: join of task 1\n" dumps "${spin_err}")
    list(LENGTH dumps dump_count)
    if(NOT spin_status STREQUAL "152" OR NOT dump_count EQUAL 2 OR
       NOT spin_err MATCHES "\n  task 1  spawned at spin.nra:15:27  running\n      spin at spin.nra:[0-9:]+\n" OR
       NOT spin_err MATCHES "trap: step limit exceeded")
        message(FATAL_ERROR "spin: expected two SIGQUIT dumps and then the step limit (status ${spin_status})\n${spin_err}")
    endif()
    # An image without tasks answers at its next host request.
    must("hello" run hello.nvr)
endif()

message(STATUS "concurrency programs: ${fixture_count} fixtures, ${compared} quantum comparisons")
