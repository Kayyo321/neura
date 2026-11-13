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
set(fixtures fan-out values detached child-trap child-exit task-limit
    channel-order backpressure select-rotation cancellation tasks-helpers worker-group
    collect-parked queued-limit deadlock deadline spawn-sites)
set(status_fan-out 0)
set(status_values 0)
set(status_detached 0)
set(status_child-trap 136)
set(stderr_child-trap "child-trap.nvr: trap: source arithmetic failure \\(trap reason 1\\): \\(task 2 spawned at child-trap.nra:10:21 by task 1 spawned at child-trap.nra:18:24 by task 0\\) in function [0-9]+ block [0-9]+ at child-trap.nra:6:50\n$")
set(status_child-exit 7)
set(status_task-limit 0)
foreach(phase2 IN ITEMS channel-order backpressure select-rotation cancellation tasks-helpers worker-group
        collect-parked queued-limit deadline spawn-sites)
    set(status_${phase2} 0)
endforeach()
# A deadlock ends the run with status 134 (Abort), the diagnostic, the wait chain, and a task dump (10.5).
set(status_deadlock 134)
set(stderr_deadlock "^[^\n]*deadlock.nvr: trap: deadlock: all tasks are parked\nwait chain: task 0 \\(parked: join of task 1\\) -> task 1 \\(parked: receive on channel 1 \\(0 of 0 queued, 0 senders waiting\\)\\)\ntask 0  root  parked: join of task 1\n")
# Timers and deadlines read the monotonic clock; the output never contains a duration.
set(options_deadline --allow-clock)

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
    neura(run run ${options_${name}} "${name}.nvr")
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
        neura(optimized run --stats ${options_${name}} --task-quantum ${quantum} "${name}.nvr")
        neura(repeated run --stats ${options_${name}} --task-quantum ${quantum} "${name}.nvr")
        neura(unoptimized run ${options_${name}} --task-quantum ${quantum} "${name}.unoptimized.nvr")
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

# ---------------------------------------------------------------------------
# Phase 2 and 3 evidence beyond the shared matrix above.

# A full buffer parks its senders: the buffer never held more than its capacity (2), though three producers
# and a slow consumer ran. The fixture's own output shows the producers waiting.
neura(pressure run --stats backpressure.nvr)
if(NOT pressure_status STREQUAL "0" OR NOT pressure_stderr MATCHES "peak queued messages 2,")
    message(FATAL_ERROR "backpressure: the buffer was not bounded by its capacity\n${pressure_stderr}")
endif()
file(READ "${programs}/backpressure.expected" pressure_expected)
string(REGEX MATCH "slow consumer: ([0-9]+) of 18 sends waited" slow_line "${pressure_expected}")
set(slow_waits "${CMAKE_MATCH_1}")
string(REGEX MATCH "fast consumer: ([0-9]+) of 18 sends waited" fast_line "${pressure_expected}")
set(fast_waits "${CMAKE_MATCH_1}")
if(slow_line STREQUAL "" OR fast_line STREQUAL "" OR NOT slow_waits GREATER fast_waits OR NOT fast_waits GREATER 0)
    message(FATAL_ERROR "backpressure: a slow consumer should make more sends wait (${slow_waits}) than a fast one (${fast_waits})")
endif()

# Collection with parked tasks and buffered messages. 120 parked senders cost about 8,000 live cells, so the
# limit leaves little room: only a collector that keeps parked frames, held values, and buffered messages as roots
# and reclaims the garbage can finish. The unlimited run's peak is larger, and the output is the same.
neura(unlimited run --stats collect-parked.nvr)
neura(limited run --stats --max-heap-cells 9500 collect-parked.nvr)
file(READ "${programs}/collect-parked.expected" collect_expected)
if(NOT unlimited_status STREQUAL "0" OR NOT limited_status STREQUAL "0" OR
   NOT unlimited_stdout STREQUAL "${collect_expected}" OR NOT limited_stdout STREQUAL "${collect_expected}")
    message(FATAL_ERROR "collect-parked: expected the same complete output with and without a heap limit\n"
                        "${unlimited_stderr}\n${limited_stdout}\n${limited_stderr}")
endif()
string(REGEX MATCH "peak heap cells ([0-9]+)," unlimited_peak_text "${unlimited_stderr}")
set(unlimited_peak "${CMAKE_MATCH_1}")
string(REGEX MATCH "peak heap cells ([0-9]+)," limited_peak_text "${limited_stderr}")
set(limited_peak "${CMAKE_MATCH_1}")
if(unlimited_peak STREQUAL "" OR limited_peak STREQUAL "" OR NOT limited_peak LESS_EQUAL 9500 OR
   NOT unlimited_peak GREATER 9500)
    message(FATAL_ERROR "collect-parked: expected a peak of at most 9500 cells under the limit and more without it, "
                        "got ${limited_peak} and ${unlimited_peak}")
endif()
if(NOT limited_stderr MATCHES "peak tasks 121,")
    message(FATAL_ERROR "collect-parked: the senders were not all parked at once\n${limited_stderr}")
endif()
# A limit below the live data still traps, at the allocation that does not fit.
neura(starved run --max-heap-cells 5000 collect-parked.nvr)
if(starved_status STREQUAL "0" OR NOT starved_stderr MATCHES "trap: heap limit exceeded")
    message(FATAL_ERROR "collect-parked: a heap limit below the live data did not trap\n${starved_stderr}")
endif()

# --max-queued-messages: the ninth buffered value traps with status 137 at the send that would exceed the limit,
# naming the task that sent it; a limit equal to the peak (12) lets the run finish.
neura(queued run --max-queued-messages 8 queued-limit.nvr)
if(NOT queued_status STREQUAL "137" OR
   NOT queued_stdout MATCHES "second channel holds 2, 8 buffered in all\n$" OR queued_stdout MATCHES "second channel holds 3" OR
   NOT queued_stderr MATCHES "trap: queued message limit exceeded: buffering another message \\(task 1 spawned at queued-limit.nra:25:13 by task 0\\)")
    message(FATAL_ERROR "queued-limit: expected status 137 after eight buffered values\n${queued_status}\n${queued_stdout}\n${queued_stderr}")
endif()
neura(queued_enough run --max-queued-messages 12 queued-limit.nvr)
file(READ "${programs}/queued-limit.expected" queued_expected)
if(NOT queued_enough_status STREQUAL "0" OR NOT queued_enough_stdout STREQUAL "${queued_expected}")
    message(FATAL_ERROR "queued-limit: a limit equal to the peak should not trap\n${queued_enough_stderr}")
endif()

# Deadlock: the message, the wait chain, and the dump of every task, at status 134 (Abort).
neura(deadlocked run deadlock.nvr)
foreach(line IN ITEMS
        "trap: deadlock: all tasks are parked\n"
        "wait chain: task 0 \\(parked: join of task 1\\) -> task 1 \\(parked: receive on channel 1 "
        "\n  task 1  spawned at deadlock.nra:26:23  parked: receive on channel 1 \\(0 of 0 queued, 0 senders waiting\\)\n"
        "\n  task 2  spawned at deadlock.nra:27:23  parked: receive on channel 2 \\(0 of 0 queued, 0 senders waiting\\)\n"
        "\n      wait_for at deadlock.nra:11:12\n")
    if(NOT deadlocked_stderr MATCHES "${line}")
        message(FATAL_ERROR "deadlock: the diagnostic lacks '${line}'\n${deadlocked_stderr}")
    endif()
endforeach()
if(NOT deadlocked_status STREQUAL "134" OR NOT deadlocked_stdout STREQUAL "before the deadlock\nroot joins one\none waits\ntwo waits\n")
    message(FATAL_ERROR "deadlock: expected status 134 and the output printed before it\n${deadlocked_status}\n${deadlocked_stdout}")
endif()

# Trace events (14.2): one JSON line per scheduler event. The counts follow from the program: spawn-sites spawns
# eight tasks (five at one site, one each at three others), each starts once and finishes once, and the seven values
# sent are the seven received. Two runs give identical files, and --trace-wall-time adds the only field that
# differs.
neura(first_trace run --trace-events trace-a.jsonl spawn-sites.nvr)
neura(second_trace run --trace-events trace-b.jsonl spawn-sites.nvr)
same_bytes("trace events of two runs" trace-a.jsonl trace-b.jsonl)
file(READ "${TEST_DIR}/trace-a.jsonl" trace)
string(REGEX MATCHALL "[^\n]+" trace_lines "${trace}")
foreach(event IN ITEMS spawn start finish send receive park wake)
    set(count_${event} 0)
endforeach()
set(spawned "")
set(started "")
set(finished "")
set(sites "")
foreach(line IN LISTS trace_lines)
    string(JSON event ERROR_VARIABLE invalid GET "${line}" event)
    if(NOT invalid STREQUAL "NOTFOUND")
        message(FATAL_ERROR "trace events: not a JSON line with an event: ${line}\n${invalid}")
    endif()
    string(JSON tick GET "${line}" tick)
    string(JSON task GET "${line}" task)
    if(NOT DEFINED count_${event})
        set(count_${event} 0)
    endif()
    math(EXPR count_${event} "${count_${event}} + 1" OUTPUT_FORMAT DECIMAL)
    if(event STREQUAL "spawn")
        string(JSON child GET "${line}" child)
        string(JSON site GET "${line}" at)
        list(APPEND spawned "${child}")
        list(APPEND sites "${site}")
    elseif(event STREQUAL "start" AND NOT task STREQUAL "0")
        list(APPEND started "${task}")
    elseif(event STREQUAL "finish")
        list(APPEND finished "${task}")
    endif()
endforeach()
set(expected_spawn 8)
set(expected_finish 8)
set(expected_send 7)
set(expected_receive 7)
foreach(event IN ITEMS spawn finish send receive)
    if(NOT count_${event} EQUAL expected_${event})
        message(FATAL_ERROR "trace events: expected ${expected_${event}} '${event}' events, got ${count_${event}}")
    endif()
endforeach()
foreach(list_name IN ITEMS spawned started finished)
    list(SORT ${list_name} COMPARE NATURAL)
    if(NOT "${${list_name}}" STREQUAL "1;2;3;4;5;6;7;8")
        message(FATAL_ERROR "trace events: tasks ${list_name}: ${${list_name}}; expected each of 1 to 8 once")
    endif()
endforeach()
foreach(site_count IN ITEMS "spawn-sites.nra:27:17=5" "spawn-sites.nra:29:24=1" "spawn-sites.nra:14:9=1" "spawn-sites.nra:15:9=1")
    string(REPLACE "=" ";" site_and_count "${site_count}")
    list(GET site_and_count 0 site_name)
    list(GET site_and_count 1 site_expected)
    set(matching "${sites}")
    list(FILTER matching INCLUDE REGEX "^${site_name}$")
    list(LENGTH matching site_found)
    if(NOT site_found EQUAL site_expected)
        message(FATAL_ERROR "trace events: ${site_name} spawned ${site_found} tasks, expected ${site_expected}")
    endif()
endforeach()
neura(timed_trace run --trace-events trace-wall.jsonl --trace-wall-time spawn-sites.nvr)
file(READ "${TEST_DIR}/trace-wall.jsonl" wall_trace)
string(REGEX REPLACE ",\"wall_ns\":[0-9]+" "" without_wall "${wall_trace}")
if(NOT wall_trace MATCHES "\"wall_ns\":[0-9]+" OR NOT without_wall STREQUAL "${trace}")
    message(FATAL_ERROR "trace events: --trace-wall-time should add wall_ns and nothing else")
endif()

# The profile lists each spawn site once with its task count.
neura(profiled run --profile spawn-sites.nvr)
foreach(site IN ITEMS "27:17  tasks 5 " "14:9  tasks 1 " "15:9  tasks 1 " "29:24  tasks 1 ")
    string(REGEX MATCHALL "\n  spawn-sites.nra:${site}" found_sites "${profiled_stderr}")
    list(LENGTH found_sites site_count)
    if(NOT site_count EQUAL 1)
        message(FATAL_ERROR "profile: expected the spawn site ${site} once, found ${site_count}\n${profiled_stderr}")
    endif()
endforeach()
string(REGEX MATCHALL "\n  spawn-sites.nra:[0-9:]+  tasks" all_sites "${profiled_stderr}")
list(LENGTH all_sites all_site_count)
if(NOT all_site_count EQUAL 4 OR NOT profiled_stderr MATCHES "\n  root  tasks 1 ")
    message(FATAL_ERROR "profile: expected the root and four spawn sites\n${profiled_stderr}")
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
        neura(bad ${command} ${option} "${programs}/fan-out.nra" -o refused.nvr)
        # build records the limits only in a native executable's policy.
        set(refusal "applies only to run and test")
        if(command STREQUAL "build" AND NOT option STREQUAL "--task-dump")
            set(refusal "applies only with --native")
        endif()
        if(bad_status STREQUAL "0" OR NOT bad_stderr MATCHES "${refusal}")
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
# A trap inside a tier 2 region (an inline state operation that faults, after
# which tier 1 runs the block again) writes one dump, at the trap's block,
# exactly as tier 0 does: the dump is written when the fault becomes the
# run's trap, not when a region raises and catches it.
must("region-trap" build "${SOURCE_DIR}/tests/concurrency/region-trap.nra" -o region-trap.nvr)
neura(trap0 run --tier=0 --task-dump region-trap.nvr)
if(NOT trap0_status STREQUAL "139" OR NOT trap0_stderr MATCHES "trap: index out of bounds: index 3000 for length 3000")
    message(FATAL_ERROR "region-trap at tier 0: status ${trap0_status}\n${trap0_stderr}")
endif()
string(REGEX MATCHALL "task 0  root  running\n" trap0_dumps "${trap0_stderr}")
list(LENGTH trap0_dumps trap0_count)
if(NOT trap0_count EQUAL 1)
    message(FATAL_ERROR "region-trap at tier 0: ${trap0_count} dumps\n${trap0_stderr}")
endif()
foreach(variant IN ITEMS "default" "eager" "eager-interpreter" "programs")
    set(environment "")
    if(variant STREQUAL "eager")
        set(environment NEURA_TIER_EAGER=1)
    elseif(variant STREQUAL "eager-interpreter")
        set(environment NEURA_TIER_EAGER=1 NEURA_JIT=off)
    elseif(variant STREQUAL "programs")
        set(environment NEURA_TIER_EAGER=1 NEURA_TIER_PROGRAMS=1)
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${environment} "${NEURA}" run --task-dump --tier-stats region-trap.nvr
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE tiered_status
        OUTPUT_VARIABLE tiered_stdout
        ERROR_VARIABLE tiered_stderr)
    string(REGEX REPLACE "(^|\n)tier: [^\n]*" "" tiered_dump "${tiered_stderr}")
    if(NOT tiered_status STREQUAL trap0_status OR NOT tiered_stdout STREQUAL trap0_stdout OR
       NOT tiered_dump STREQUAL trap0_stderr OR NOT tiered_stderr MATCHES "\\(in regions [1-9]")
        message(FATAL_ERROR "region-trap (${variant}): the trap or its task dump differs from tier 0's, or no "
                            "region ran\n--- tier 0:\n${trap0_stderr}\n--- ${variant}:\n${tiered_stderr}")
    endif()
endforeach()

# SIGQUIT asks a running image for a dump and the run continues to its end.
# The run must outlast two seconds of wall time, so it stays on the reference
# tier, whose speed the step limit was chosen for.
if(UNIX)
    must("spin" build "${SOURCE_DIR}/tests/concurrency/spin.nra" -o spin.nvr)
    execute_process(
        COMMAND sh -c "\"$0\" run --tier=0 --max-steps 30000000 spin.nvr 2> spin.err & pid=$!; sleep 1; kill -QUIT $pid; sleep 1; kill -QUIT $pid; wait $pid; echo $? > spin.status" "${NEURA}"
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

# The event trace is opened in place, so a symbolic link at its path is refused and its target stays as it was.
if(UNIX)
    file(WRITE "${TEST_DIR}/victim.txt" "untouched\n")
    file(CREATE_LINK "${TEST_DIR}/victim.txt" "${TEST_DIR}/link.events" SYMBOLIC)
    neura(linked run --trace-events link.events fan-out.nvr)
    file(READ "${TEST_DIR}/victim.txt" victim)
    if(linked_status STREQUAL "0" OR NOT linked_stderr MATCHES "cannot write link.events: it is a symbolic link" OR
       NOT victim STREQUAL "untouched\n")
        message(FATAL_ERROR "--trace-events followed a symbolic link (status ${linked_status})\n${linked_stderr}\n${victim}")
    endif()
endif()

# A deadline stops a task that never parks and is the only one that can run.
# A step limit bounds the run in case the deadline is never seen.
foreach(quantum IN ITEMS 1 1024)
    must("busy-deadline" build "${SOURCE_DIR}/tests/concurrency/busy-deadline.nra" -o busy-deadline.nvr)
    neura(busy run --max-steps 2000000000 --task-quantum ${quantum} busy-deadline.nvr)
    if(NOT busy_status STREQUAL "0" OR NOT busy_stdout STREQUAL "busy task stopped: true\n" OR NOT busy_stderr STREQUAL "")
        message(FATAL_ERROR "busy-deadline, quantum ${quantum}: status ${busy_status}\n${busy_stdout}\n${busy_stderr}")
    endif()
endforeach()

message(STATUS "concurrency programs: ${fixture_count} fixtures, ${compared} quantum comparisons")
