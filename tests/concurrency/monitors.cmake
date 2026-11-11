# Monitors (docs/lang/Concurrency.md section 8.1): classes whose
# #Synchronized methods take a per-instance guard, shared by tasks. Each
# program under monitors/ is built from source with and without optimization
# and through a .nir file; the routes give the same bytes, every build gives
# the same output and status under each quantum, run after run, and the
# default quantum gives the recorded <name>.expected. Then: schedule
# exploration, record and replay, a monitor exported by a prebuilt .nir
# module, the compile-time rejections, and a program without tasks, where
# the tag costs nothing.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(programs "${SOURCE_DIR}/tests/concurrency/monitors")

function(neura prefix)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 120
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

function(reject label pattern)
    execute_process(COMMAND "${NEURA}" check --error-format=short ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status STREQUAL "1" OR NOT error MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: expected '${pattern}', got (${status})\n${error}")
    endif()
endfunction()

# name, expected status, and a pattern the standard error of a run must
# match (empty: standard error must be empty).
set(fixtures counter cancel deadlock trap)
set(status_counter 0)
set(status_cancel 0)
# Two monitors that call each other: the deadlock report shows both guards.
set(status_deadlock 134)
set(stderr_deadlock "trap: deadlock: all tasks are parked\nwait chain: task 0 \\(parked: join of task 1\\) -> task 1 \\(parked: guard 2 \\(held by task 2\\)\\) -> task 2 \\(parked: guard 1 \\(held by task 1\\)\\)\n")
# A trap inside a synchronized method ends the run, naming the task.
set(status_trap 136)
set(stderr_trap "trap: source arithmetic failure \\(trap reason 1\\): \\(task 3 spawned at trap.nra:26:25 by task 0\\)[^\n]* at trap.nra:13:16\n$")

set(quanta 1 2 3 7 64 1024)
foreach(name IN LISTS fixtures)
    set(source "${programs}/${name}.nra")
    file(READ "${programs}/${name}.expected" expected)
    must("${name}" build "${source}" -o "${name}.nvr")
    must("${name}" build --no-optimize "${source}" -o "${name}.unoptimized.nvr")
    must("${name}" emit-ir "${source}" -o "${name}.nir")
    must("${name}" build "${name}.nir" -o "${name}.from-nir.nvr")
    same_bytes("${name}: source and .nir routes" "${name}.nvr" "${name}.from-nir.nvr")

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

    foreach(quantum IN LISTS quanta)
        neura(optimized run --stats --task-quantum ${quantum} "${name}.nvr")
        neura(repeated run --stats --task-quantum ${quantum} "${name}.nvr")
        neura(unoptimized run --task-quantum ${quantum} "${name}.unoptimized.nvr")
        if(NOT optimized_status STREQUAL "${status_${name}}")
            message(FATAL_ERROR "${name}, quantum ${quantum}: status ${optimized_status}\n${optimized_stderr}")
        endif()
        if(NOT repeated_status STREQUAL optimized_status OR NOT repeated_stdout STREQUAL optimized_stdout OR
           NOT repeated_stderr STREQUAL optimized_stderr)
            message(FATAL_ERROR "${name}, quantum ${quantum}: a repeated run differs")
        endif()
        if(NOT unoptimized_status STREQUAL optimized_status OR NOT unoptimized_stdout STREQUAL optimized_stdout)
            message(FATAL_ERROR "${name}, quantum ${quantum}: optimized and unoptimized builds differ\n${optimized_stdout}\n--\n${unoptimized_stdout}")
        endif()
    endforeach()
endforeach()

# The shared total is exact under every quantum: each addition reads, makes a
# call that is a scheduling point, and writes, so only the guard keeps updates
# from being lost. The image enters and leaves the guard.
foreach(quantum IN LISTS quanta)
    neura(tally run --task-quantum ${quantum} counter.nvr)
    if(NOT tally_stdout STREQUAL "shared tally: 400\nhelper calls: 400\n")
        message(FATAL_ERROR "counter, quantum ${quantum}: the total is not exact\n${tally_stdout}")
    endif()
endforeach()
must("counter" disassemble counter.nvr)
if(NOT must_stdout MATCHES "exit scheduler guard-open" OR NOT must_stdout MATCHES "exit scheduler guard-enter" OR
   NOT must_stdout MATCHES "exit scheduler guard-exit")
    message(FATAL_ERROR "counter: the image does not use the guard operations")
endif()

# Schedule exploration: the exact total under every explored schedule.
neura(explored test --schedules 24 --task-quantum 1 "${programs}/tally-tests.nra")
if(NOT explored_status STREQUAL "0" OR
   NOT explored_stdout MATCHES "PASS TallyTests.test_total_is_exact \\[24 schedules\\]\n")
    message(FATAL_ERROR "exploration: status ${explored_status}\n${explored_stdout}\n${explored_stderr}")
endif()

# Record and replay: two recordings of a monitor program are the same bytes,
# and the replay reproduces the run with every service denied.
must("record" run --task-quantum 3 --record first.trace counter.nvr)
set(recorded "${must_stdout}")
must("record" run --task-quantum 3 --record second.trace counter.nvr)
same_bytes("record: two recordings" first.trace second.trace)
must("replay" run --replay first.trace counter.nvr)
if(NOT must_stdout STREQUAL recorded)
    message(FATAL_ERROR "replay: the output differs\n${recorded}\n--\n${must_stdout}")
endif()

# A program without tasks: the methods stay plain, so the run takes the same
# steps and cells with and without the tag, and the image has no guard.
file(READ "${programs}/plain.nra" plain_source)
string(REPLACE "#Synchronized\n" "// untagged\n" untagged_source "${plain_source}")
file(WRITE "${TEST_DIR}/untagged.nra" "${untagged_source}")
must("plain" build "${programs}/plain.nra" -o plain.nvr)
must("plain" build untagged.nra -o untagged.nvr)
neura(tagged run --stats plain.nvr)
neura(untagged run --stats untagged.nvr)
string(REGEX MATCH "steps [0-9]+, peak heap cells [0-9]+" tagged_stats "${tagged_stderr}")
string(REGEX MATCH "steps [0-9]+, peak heap cells [0-9]+" untagged_stats "${untagged_stderr}")
if(NOT tagged_status STREQUAL "0" OR NOT tagged_stdout STREQUAL "55\n" OR tagged_stats STREQUAL "" OR
   NOT tagged_stats STREQUAL untagged_stats OR NOT untagged_stdout STREQUAL tagged_stdout)
    message(FATAL_ERROR "plain: expected the same output and statistics with and without the tag\n${tagged_stderr}\n${untagged_stderr}")
endif()
must("plain" disassemble plain.nvr)
if(must_stdout MATCHES "guard-" OR must_stdout MATCHES " poll ")
    message(FATAL_ERROR "plain: a program without tasks uses the guard")
endif()

# A prebuilt .nir module that exports a monitor, an #Immutable class, and a
# #Pure function. The attributes survive the file; a program shares the
# monitor between tasks and calls the pure function from its own #Pure one.
# An untagged function of the module is not pure to the program, because the
# tag is part of the module's interface.
file(WRITE "${TEST_DIR}/lib.nra" [=[
let Lib = module (
    #Synchronized
    export let Counter = class (
        let count: int64 = 0;
        export let add = func(amount: int64) -> int64 {
            count = count + amount;
            return count;
        };
        export let total = func -> int64 => count;
    );

    #Immutable
    export let Point = class (
        export last x: int64;
        export let init = func(value: int64) {
            x = value;
        };
    );

    #Pure
    export let double = func(value: int64) -> int64 => value * 2;

    export let triple = func(value: int64) -> int64 => value * 3;
);
]=])
must("library" emit-ir lib.nra -o lib.nir)
must("library" emit-ir --no-optimize lib.nir)
foreach(attribute IN ITEMS "\"Synchronized\"" "@class.shareable\\(true\\)" "@synchronized\\(true\\)" "\"Immutable\"" "\"Pure\"")
    if(NOT must_stdout MATCHES "${attribute}")
        message(FATAL_ERROR "library: lib.nir lost ${attribute}\n${must_stdout}")
    endif()
endforeach()
file(WRITE "${TEST_DIR}/app.nra" [=[
let lib = import:"lib.nir";
let console = import:Std.Io.Console;
let program = import:Std.Program;

#Pure
let quadruple = func(value: int64) -> int64 => lib.double(lib.double(value));

let work = func(counter: lib.Counter, point: lib.Point) -> int64 => counter.add(quadruple(point.x));

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let counter = new lib.Counter();
        scope {
            for (let i: int64 = 1; i <= 4; i++) {
                spawn work(counter, new lib.Point(i));
            }
        }
        console.print_line(counter.total().to_string());
        return program.ExitCode.Ok;
    };
);
]=])
must("library" build app.nra -o app.nvr)
must("library" emit-ir app.nra -o app.nir)
must("library" build app.nir -o app-from-nir.nvr)
same_bytes("library: source and .nir routes" app.nvr app-from-nir.nvr)
foreach(quantum IN LISTS quanta)
    neura(library run --task-quantum ${quantum} app.nvr)
    if(NOT library_status STREQUAL "0" OR NOT library_stdout STREQUAL "40\n")
        message(FATAL_ERROR "library, quantum ${quantum}: expected 40\n${library_stdout}\n${library_stderr}")
    endif()
endforeach()
file(READ "${TEST_DIR}/app.nra" app_source)
string(REPLACE "lib.double(lib.double(value))" "lib.triple(value)" untagged_app "${app_source}")
file(WRITE "${TEST_DIR}/untagged-app.nra" "${untagged_app}")
reject("library" "untagged-app.nra:5:1: error\\[E0917\\]: #Pure function 'quadruple' is not pure: it reaches 'triple', which is declared in a prebuilt module without #Pure" untagged-app.nra)

# Without #Synchronized the spawn is rejected, naming the member that keeps
# the class from being shared; a state access through another instance does
# too.
file(READ "${programs}/counter.nra" counter_source)
string(REPLACE "    export let total = func -> int64 => count;" "    export let total = func -> int64 => count;\n\n    #Synchronized\n    export let peek = func -> int64 => count;" with_peek "${counter_source}")
string(REPLACE "#Synchronized\nlet Tally" "let Tally" unsynchronized "${with_peek}")
string(REPLACE "    export let add = func" "    #Synchronized\n    export let add = func" unsynchronized "${unsynchronized}")
string(REPLACE "    #Synchronized\n    export let peek" "    export let peek" unsynchronized "${unsynchronized}")
file(WRITE "${TEST_DIR}/unsynchronized.nra" "${unsynchronized}")
reject("unsynchronized" "unsynchronized.nra:[0-9]+:[0-9]+: error\\[E0602\\]: a value of type Tally is not transferable" unsynchronized.nra)
neura(rendered check unsynchronized.nra)
if(NOT rendered_stderr MATCHES "'Tally' is not shareable: method 'add_twice' is exported and not synchronized")
    message(FATAL_ERROR "unsynchronized: the reason is not labelled\n${rendered_stderr}")
endif()
file(WRITE "${TEST_DIR}/stray.nra" [=[
#Synchronized
let Account = class (
    let balance: int64 = 0;

    export let take_all = func(other: Account) -> int64 {
        balance = balance + other.balance;
        other.balance = 0;
        return balance;
    };
);

let work = func(account: Account) {};

let run = func() {
    let account = new Account();
    scope {
        spawn work(account);
    }
};
]=])
neura(stray check stray.nra)
if(NOT stray_status STREQUAL "1" OR
   NOT stray_stderr MATCHES "'Account' is not shareable: 'take_all' uses the field 'balance' of an instance other than its receiver")
    message(FATAL_ERROR "stray: the access through another instance was not reported\n${stray_stderr}")
endif()

# An #Immutable class's instances are shared by tasks; a parallel item gets a
# copy, but a monitor cannot enter one.
file(WRITE "${TEST_DIR}/items.nra" [=[
let parallel = import:Std.Concurrent.Parallel;

#Synchronized
let Counter = class (
    let count: int64 = 0;
    export let add = func -> int64 {
        count = count + 1;
        return count;
    };
);

let use = func(counter: Counter) -> int64 => 1;

let run = func(counters: Counter[]) -> int64[] => parallel.map<Counter, int64>(counters, use, parallel.limits(1000, 1000));
]=])
reject("items" "error\\[E0602\\]: a value of type Counter is not transferable, so it cannot be given to a parallel item" items.nra)
