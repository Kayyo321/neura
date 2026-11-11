# Multicore and shared state (docs/lang/Concurrency.md sections 8 and 12).
# A parallel map must mean the sequential map: one worker and several agree
# on values, on the trapping item, on item limits, and on every statistic.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(programs "${SOURCE_DIR}/tests/concurrency/parallel")

function(neura prefix)
    execute_process(COMMAND "${NEURA}" ${ARGN} WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
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

foreach(name IN ITEMS map traps shared)
    must("${name}" build "${programs}/${name}.nra" -o "${name}.nvr")
    must("${name}" build --no-optimize "${programs}/${name}.nra" -o "${name}.unoptimized.nvr")
endforeach()
set(workers 1 2 3 8)

# Values: the recorded output under every worker count, optimized or not,
# with identical statistics, so the totals do not depend on the workers.
file(READ "${programs}/map.expected" expected)
set(reference_stats "")
foreach(count IN LISTS workers)
    foreach(image IN ITEMS map.nvr map.unoptimized.nvr)
        neura(run run --stats --parallel-workers ${count} ${image})
        if(NOT run_status STREQUAL "0" OR NOT run_stdout STREQUAL "${expected}")
            message(FATAL_ERROR "${image} with ${count} workers: status ${run_status}\n${run_stdout}\n${run_stderr}")
        endif()
        if(image STREQUAL "map.nvr")
            string(REGEX MATCH "stats: steps [0-9]+, peak heap cells [0-9]+" stats "${run_stderr}")
            if(stats STREQUAL "")
                message(FATAL_ERROR "map: no statistics\n${run_stderr}")
            endif()
            if(reference_stats STREQUAL "")
                set(reference_stats "${stats}")
            elseif(NOT stats STREQUAL reference_stats)
                message(FATAL_ERROR "map: ${count} workers changed the totals: '${stats}' instead of '${reference_stats}'")
            endif()
        endif()
    endforeach()
endforeach()

# The trapping item and item limits. Items 2 and 4 of `divide` both trap;
# the lowest index is reported under every worker count.
set(status_divide 136)
set(stderr_divide "trap: source arithmetic failure \\(trap reason 1\\): \\(item 2 of a parallel map\\) in function [0-9]+ block [0-9]+ at traps.nra:8:")
set(status_steps 152)
set(stderr_steps "trap: step limit exceeded: 20000 steps \\(item 2 of a parallel map\\)")
set(status_heap 137)
set(stderr_heap "trap: heap limit exceeded: [^\n]* \\(item 2 of a parallel map\\)")
foreach(case IN ITEMS divide steps heap)
    set(reference "")
    foreach(count IN LISTS workers)
        neura(run run --parallel-workers ${count} traps.nvr -- ${case})
        if(NOT run_status STREQUAL "${status_${case}}" OR NOT run_stdout STREQUAL "start\n" OR
           NOT run_stderr MATCHES "${stderr_${case}}")
            message(FATAL_ERROR "traps ${case} with ${count} workers: status ${run_status}\n${run_stdout}\n${run_stderr}")
        endif()
        if(reference STREQUAL "")
            set(reference "${run_stderr}")
        elseif(NOT run_stderr STREQUAL reference)
            message(FATAL_ERROR "traps ${case}: ${count} workers changed the diagnostic\n${reference}\n${run_stderr}")
        endif()
    endforeach()
endforeach()
# Each item is within its own limit, and the run's limit counts them all,
# in index order: four items of about 12,000 steps pass under 200,000 each
# (48,165 steps for the whole run) and exceed a run limit of 30,000 when the
# third item's steps are added, for any number of workers.
foreach(count IN LISTS workers)
    neura(run run --parallel-workers ${count} traps.nvr -- total)
    if(NOT run_status STREQUAL "0" OR NOT run_stdout STREQUAL "start\nfinished\n")
        message(FATAL_ERROR "traps total with ${count} workers: ${run_status}\n${run_stderr}")
    endif()
    neura(run run --max-steps 30000 --parallel-workers ${count} traps.nvr -- total)
    if(NOT run_status STREQUAL "152" OR NOT run_stderr MATCHES "trap: step limit exceeded: 30000 steps")
        message(FATAL_ERROR "traps total under a run limit with ${count} workers: ${run_status}\n${run_stderr}")
    endif()
endforeach()
foreach(value IN ITEMS 0 -1 x 1025)
    neura(bad run --parallel-workers ${value} map.nvr)
    if(bad_status STREQUAL "0")
        message(FATAL_ERROR "--parallel-workers ${value} was accepted")
    endif()
endforeach()
# build records the worker count only in a native executable's policy.
neura(bad build --parallel-workers 2 "${programs}/map.nra" -o map-workers.nvr)
if(bad_status STREQUAL "0" OR NOT bad_stderr MATCHES "--parallel-workers applies only with --native")
    message(FATAL_ERROR "build accepted --parallel-workers")
endif()

# Shared state: the same answers under every quantum, optimized or not.
file(READ "${programs}/shared.expected" expected)
foreach(quantum IN ITEMS 1 2 3 7 64 1024)
    foreach(image IN ITEMS shared.nvr shared.unoptimized.nvr)
        neura(run run --task-quantum ${quantum} ${image})
        if(NOT run_status STREQUAL "0" OR NOT run_stdout STREQUAL "${expected}" OR NOT run_stderr STREQUAL "")
            message(FATAL_ERROR "${image}, quantum ${quantum}: status ${run_status}\n${run_stdout}\n${run_stderr}")
        endif()
    endforeach()
endforeach()
neura(run run shared.nvr -- over-release)
if(NOT run_status STREQUAL "134" OR NOT run_stderr MATCHES "invalid state: released more permits than were created")
    message(FATAL_ERROR "shared over-release: ${run_status}\n${run_stderr}")
endif()
neura(run run shared.nvr -- reenter)
if(NOT run_status STREQUAL "134" OR NOT run_stderr MATCHES "invalid state: guard already held")
    message(FATAL_ERROR "shared reenter: ${run_status}\n${run_stderr}")
endif()
neura(run run shared.nvr -- overflow)
if(NOT run_status STREQUAL "136" OR NOT run_stderr MATCHES "source arithmetic failure")
    message(FATAL_ERROR "shared overflow: ${run_status}\n${run_stderr}")
endif()

# What the compiler refuses, each at the program's own call.
set(prelude [=[
let console = import:Std.Io.Console;
let program = import:Std.Program;
let parallel = import:Std.Concurrent.Parallel;
let guards = import:Std.Concurrent.Guarded;
let maps = import:Std.Collections.HashMap;
]=])
function(check_rejection name declarations body location message_pattern)
    file(WRITE "${TEST_DIR}/${name}.nra" "${prelude}${declarations}export let Main = module : program.Entry (\n    export let.override main = func(arguments: string[]) -> program.ExitCode {\n${body}        return program.ExitCode.Ok;\n    };\n);\n")
    neura(check check --error-format=short "${name}.nra")
    if(check_status STREQUAL "0" OR NOT check_stderr MATCHES "${name}[.]nra:${location}: error(\\[[EW][0-9]+\\])?: " OR NOT check_stderr MATCHES "${message_pattern}")
        message(FATAL_ERROR "${name}: expected a diagnostic at ${location} matching '${message_pattern}'\n${check_stderr}")
    endif()
endfunction()
check_rejection(worker_prints "let noisy = func(x: int) -> int { console.print_line(\"x\"); return x; };\n"
    "        let out = parallel.map<int, int>([1, 2], noisy, parallel.limits(1000, 1000));\n"
    "9:19" "the worker of a parallel map must compute in isolation: it reaches '[a-z_]+', which issues a host request")
check_rejection(worker_writes_global "let hits: int = 0;\nlet counting = func(x: int) -> int { hits = hits + 1; return x; };\n"
    "        let out = parallel.map<int, int>([1, 2], counting, parallel.limits(1000, 1000));\n"
    "10:19" "the worker of a parallel map is not task-safe: it reaches 'counting', which uses the reassignable binding 'hits'")
check_rejection(worker_spawns "let nested = func(x: int) -> int { scope { let t = spawn nested(x); } return x; };\n"
    "        let out = parallel.map<int, int>([1, 2], nested, parallel.limits(1000, 1000));\n"
    "9:19" "must compute in isolation: [^\n]*starts or scopes tasks")
check_rejection(result_not_transferable "let build = func(x: int) -> maps.HashMap<string, int> => maps.create<string, int>();\n"
    "        let out = parallel.map<int, maps.HashMap<string, int>>([1, 2], build, parallel.limits(1000, 1000));\n"
    "9:19" "HashMap<string, int32> is not transferable, so it cannot be the result of a parallel item")
check_rejection(guard_body_captures_object "let Box = class ( export let value: int; export let init = func(v: int) { value = v; }; );\n"
    "        let box = new Box(1);\n        let table = guards.create<maps.HashMap<string, int>>(func -> maps.HashMap<string, int> => maps.create<string, int>());\n        let leaked = table.with<int>(func(entries: maps.HashMap<string, int>) -> int { box.value = entries.size(); return 1; });\n"
    "11:22" "is not transferable")
check_rejection(guard_result_escapes ""
    "        let table = guards.create<maps.HashMap<string, int>>(func -> maps.HashMap<string, int> => maps.create<string, int>());\n        let leaked = table.with<maps.HashMap<string, int>>(func(entries: maps.HashMap<string, int>) -> maps.HashMap<string, int> => entries);\n"
    "9:22" "HashMap<string, int32> is not transferable")

message(STATUS "concurrency parallel: values, traps, limits, and shared state agree under ${workers} workers")
