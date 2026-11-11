# Conformance of the scoped-task forms (docs/lang/Concurrency.md section 4):
# each form is accepted, each rejected program is reported at the span the
# specification names, and programs that use `scope`, `spawn`, and `select`
# as ordinary names keep their meaning.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")

set(prelude [=[
let console = import:Std.Io.Console;
let program = import:Std.Program;
]=])
set(entry_open [=[
export let Main = module : program.Entry (
    export let.override main = func(arguments: string[]) -> program.ExitCode {
]=])
set(entry_close [=[
        return program.ExitCode.Ok;
    };
);
]=])

function(write_program name declarations body)
    file(WRITE "${TEST_DIR}/${name}.nra" "${prelude}${declarations}${entry_open}${body}${entry_close}")
endfunction()

# The call chain of a task-safety error is a list of labels: each call on the
# way and the operation itself, with their locations (`--error-format=json`).
function(check_labels name)
    execute_process(
        COMMAND "${NEURA}" check --error-format=json "${name}.nra"
        WORKING_DIRECTORY "${TEST_DIR}"
        OUTPUT_QUIET ERROR_VARIABLE stderr
    )
    foreach(label IN LISTS ARGN)
        if(NOT stderr MATCHES "${label}")
            message(FATAL_ERROR "${name}: expected a label matching '${label}'\n${stderr}")
        endif()
    endforeach()
endfunction()

# The fixture files begin with the two prelude lines, so a declaration on
# line N of `declarations` is on line N + 2, and a line of `body` follows
# the declarations and the two lines that open the entry module.
function(check_rejection name declarations body location message_pattern)
    write_program("${name}" "${declarations}" "${body}")
    execute_process(
        COMMAND "${NEURA}" check --error-format=short "${name}.nra"
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "Expected ${name} to be rejected, got status ${status}\n${stdout}\n${stderr}")
    endif()
    # The short form is the stable one-line form (Tooling.md, Diagnostics).
    set(at_location "${name}[.]nra:${location}: error(\\[[EW][0-9]+\\])?: ")
    if(NOT stderr MATCHES "${at_location}" OR NOT stderr MATCHES "${message_pattern}")
        message(FATAL_ERROR "${name}: expected a diagnostic at ${location} matching '${message_pattern}'\n${stderr}")
    endif()
    # `build` must refuse the same program: the checks do not depend on the command.
    execute_process(
        COMMAND "${NEURA}" build --error-format=short "${name}.nra" -o "${name}.nvr"
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE build_status
        OUTPUT_QUIET ERROR_VARIABLE build_stderr
    )
    if(build_status STREQUAL "0" OR NOT build_stderr MATCHES "${at_location}" OR EXISTS "${TEST_DIR}/${name}.nvr")
        message(FATAL_ERROR "${name}: build did not report the same diagnostic\n${build_stderr}")
    endif()
endfunction()

# Extra arguments are options for `neura run`, such as --allow-clock.
function(check_accepted name declarations body expected)
    write_program("${name}" "${declarations}" "${body}")
    execute_process(COMMAND "${NEURA}" check "${name}.nra" WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${name}: check failed with status ${status}\n${stderr}")
    endif()
    execute_process(COMMAND "${NEURA}" build "${name}.nra" -o "${name}.nvr" WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${name}: build failed with status ${status}\n${stderr}")
    endif()
    execute_process(COMMAND "${NEURA}" run ${ARGN} "${name}.nvr" WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL "${expected}" OR NOT stderr STREQUAL "")
        message(FATAL_ERROR "${name}: expected output '${expected}', got status ${status}\n${stdout}\n${stderr}")
    endif()
endfunction()

set(work "let work = func(x: int) -> int => x + 1;\n")

# ---------------------------------------------------------------------------
# Accepted forms.

check_accepted(scope_spawn_join "${work}" [=[
        scope {
            let task = spawn work(41);
            console.print_line(task.join().to_string());
        }
]=] "42\n")

check_accepted(spawn_statement "let say = func(text: string) { console.print_line(text); };\n" [=[
        scope {
            spawn say("child");
            console.print_line("parent");
        }
        console.print_line("after");
]=] "parent\nchild\nafter\n")

check_accepted(spawn_function_value "${work}" [=[
        let handler = func(x: int) -> int => x * 2;
        scope {
            let task = spawn handler(21);
            console.print_line(task.join().to_string());
        }
]=] "42\n")

check_accepted(spawn_module_member [=[
let Jobs = module (
    export let twice = func(x: int) -> int => x * 2;
);
]=] [=[
        scope {
            let task = spawn Jobs.twice(21);
            console.print_line(task.join().to_string());
        }
]=] "42\n")

check_accepted(nested_scopes "${work}" [=[
        scope {
            let outer = spawn work(1);
            scope {
                let inner = spawn work(outer.join());
                console.print_line(inner.join().to_string());
            }
        }
]=] "3\n")

check_accepted(join_twice "${work}" [=[
        scope {
            let task = spawn work(1);
            console.print_line((task.join() + task.join()).to_string());
            console.print_line(task.is_finished().to_string());
        }
]=] "4\ntrue\n")

# Programs that use the three words as names keep their meaning (C3).
check_accepted(words_as_names "let pick = func -> int => 2;\n" [=[
        let scope = 3;
        let spawn = func(x: int) -> int => x + scope;
        let select = spawn(4);
        scope = select + pick();
        console.print_line(scope.to_string());
        console.print_line(spawn(scope).to_string());
]=] "9\n12\n")

check_accepted(words_as_members [=[
let Words = module (
    export let scope = func(x: int) -> int => x + 1;
    export let spawn = func(x: int) -> int => x + 2;
    export let select = func(x: int) -> int => x + 3;
);
]=] [=[
        console.print_line((Words.scope(1) + Words.spawn(1) + Words.select(1)).to_string());
]=] "9\n")

# ---------------------------------------------------------------------------
# Rejected programs. Body lines start at line 6 with one declaration line
# (2 prelude + 1 declaration + 2 entry lines), at line 5 with none.

check_rejection(spawn_outside_scope "${work}" [=[
        let task = spawn work(1);
]=] "6:20" "spawn requires an enclosing scope block in the same function body")

check_rejection(spawn_in_nested_lambda "${work}" [=[
        scope {
            let f = func -> int {
                let task = spawn work(1);
                return 1;
            };
        }
]=] "8:28" "spawn requires an enclosing scope block in the same function body")

check_rejection(spawn_operand_not_a_name
    "${work}let pick = func -> func(int) -> int => work;\n" [=[
        scope {
            let task = spawn (pick())(2);
        }
]=] "8:30" "a spawn operand begins with a name")

check_rejection(spawn_not_a_call "${work}" [=[
        scope {
            let task = spawn work;
        }
]=] "7:30" "spawn requires a call")

check_rejection(spawn_constructor [=[
let Shape = enum (Circle(int), Dot);
]=] [=[
        scope {
            let task = spawn Shape.Circle(2);
        }
]=] "7:30" "spawn requires a call of a function, a method, or a function value")

check_rejection(argument_not_transferable_map [=[
let maps = import:Std.Collections.HashMap;
let update = func(cache: maps.HashMap<string, string>) { };
]=] [=[
        let cache = maps.create<string, string>();
        scope {
            spawn update(cache);
        }
]=] "9:26" "HashMap<string, string> is not transferable")

check_rejection(argument_not_transferable_array "let total = func(items: int[]) -> int => items.length;\n" [=[
        let items: int[] = [1, 2, 3];
        scope {
            let task = spawn total(items);
        }
]=] "8:36" "int32\\[\\] is not transferable")

check_rejection(receiver_not_transferable [=[
let Counter = class (
    let count: int;
    export let init = func { count = 0; };
    export let bump = func -> int { count = count + 1; return count; };
);
]=] [=[
        let counter = new Counter();
        scope {
            let task = spawn counter.bump();
        }
]=] "12:30" "Counter is not transferable")

check_rejection(closure_capturing_object [=[
let Box = class (
    export let value: int;
    export let init = func(v: int) { value = v; };
);
]=] [=[
        let box = new Box(1);
        let read = func -> int => box.value;
        scope {
            let task = spawn read();
        }
]=] "12:30" "is not transferable")

check_rejection(call_writes_global [=[
let counter: int = 0;
let bump = func { counter = counter + 1; };
let audit = func(entry: int) { bump(); };
]=] [=[
        scope {
            spawn audit(3);
        }
]=] "9:13" "error\\[E0601\\]: the spawned call is not task-safe: it reaches 'bump', which uses the reassignable binding 'counter'")
check_labels(call_writes_global
    "\"line\":9,\"column\":19,\"primary\":false,\"label\":\"calls 'audit'\""
    "\"line\":5,\"column\":32,\"primary\":false,\"label\":\"calls 'bump'\""
    "\"line\":4,\"column\":19,\"primary\":false,\"label\":\"uses the reassignable binding 'counter' here\"")

check_rejection(call_reads_mutable_global [=[
let limit: int = 3;
let over = func(x: int) -> bool => x > limit;
]=] [=[
        scope {
            let task = spawn over(4);
        }
]=] "8:24" "uses the reassignable binding 'limit'")

check_rejection(virtual_call_reaches_global [=[
let hits: int = 0;
let Shape = interface (
    let area = func -> int;
);
let Square = class implements Shape (
    export let init = func { };
    export let.override area = func -> int { hits = hits + 1; return 4; };
);
let Dot = class implements Shape (
    export let init = func { };
    export let.override area = func -> int => 0;
);
let measure = func(kind: int) -> int {
    let shape: Shape = new Dot();
    return shape.area();
};
]=] [=[
        let square = new Square();
        scope {
            let task = spawn measure(1);
        }
]=] "23:24" "not task-safe")

check_rejection(function_value_reaches_global [=[
let seen: int = 0;
let apply = func(f: func(int) -> int, x: int) -> int => f(x);
let pure = func(x: int) -> int => x;
let noisy = func(x: int) -> int { seen = seen + x; return x; };
]=] [=[
        let other = apply(noisy, 1);
        scope {
            let task = spawn apply(pure, 2);
        }
]=] "11:24" "not task-safe")

check_rejection(return_task [=[
let tasks = import:Std.Concurrent.Task;
let work = func(x: int) -> int => x + 1;
let leak = func -> tasks.Task<int> {
    scope {
        let task = spawn work(1);
        return task;
    }
};
]=] "" "8:16" "Task<int32> cannot leave its scope: it cannot be returned from a function")

check_rejection(task_field [=[
let tasks = import:Std.Concurrent.Task;
let Holder = class (
    let pending: tasks.Task<int>;
);
]=] "" "5:18" "cannot leave its scope: it cannot be stored in a field or global")

check_rejection(task_enum_payload [=[
let tasks = import:Std.Concurrent.Task;
let Pending = enum (None, Some(tasks.Task<int>));
]=] "" "4:32" "cannot leave its scope: it cannot be an enum payload")

check_rejection(task_type_argument [=[
let tasks = import:Std.Concurrent.Task;
let cells = import:Std.Core.Cell;
let work = func(x: int) -> int => x + 1;
]=] [=[
        scope {
            let cell = cells.of<tasks.Task<int>>(spawn work(1));
        }
]=] "9:33" "cannot leave its scope: it cannot be a type argument")

check_rejection(task_captured_by_lambda "${work}" [=[
        scope {
            let task = spawn work(1);
            let later = func -> int => task.join();
        }
]=] "8:40" "cannot leave its scope: a lambda cannot capture it")

check_rejection(task_passed_to_spawn [=[
let tasks = import:Std.Concurrent.Task;
let work = func(x: int) -> int => x + 1;
let wait = func(task: tasks.Task<int>) -> int => task.join();
]=] [=[
        scope {
            let first = spawn work(1);
            let second = spawn wait(first);
        }
]=] "10:37" "cannot leave its scope: it cannot be passed to spawn")

check_rejection(task_assigned_to_outer_binding "${work}" [=[
        scope {
            let outer = spawn work(1);
            scope {
                outer = spawn work(2);
            }
        }
]=] "9:25" "can be stored only in a local declared inside that scope")

check_rejection(task_appended_to_outer_list [=[
let tasks = import:Std.Concurrent.Task;
let work = func(x: int) -> int => x + 1;
]=] [=[
        scope {
            let pending: tasks.Task<int>[list] = [];
            scope {
                pending.append(spawn work(2));
            }
        }
]=] "10:32" "can be stored only in a local declared inside that scope")

check_rejection(task_stored_through_parameter [=[
let tasks = import:Std.Concurrent.Task;
let keep = func(pending: tasks.Task<int>[list], task: tasks.Task<int>) {
    pending.append(task);
};
]=] "" "5:20" "can be stored only in a local declared inside that scope")

check_rejection(scope_at_top_level [=[
scope {
    console.print_line("no");
}
]=] "" "3:1" "a scope block is only allowed inside a function body")

check_rejection(spawn_with_native_bindings [=[
let foreign = import:Std.DynamicForeign.Linker;
let work = func(x: int) -> int => x + 1;
]=] [=[
        scope {
            let task = spawn work(1);
        }
]=] "8:24" "a program that spawns a task cannot use Std.DynamicForeign or import:c")

# The same rule without a spawn: the loader refuses any image that has a scheduler operation beside native
# bindings, so the compiler says so first.
check_rejection(native_library_with_a_channel "
let foreign = import:Std.DynamicForeign.Linker;
let channels = import:Std.Concurrent.Channel;
" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
]=] "8:42" "a program that uses a scope, a channel, or another scheduler operation cannot use Std.DynamicForeign or import:c")

# `check --task-safety` reports each function of the linked program.
file(WRITE "${TEST_DIR}/report.nra" "${prelude}let counter: int = 0;
let pure = func(x: int) -> int => x + 1;
let counting = func(x: int) -> int { counter = counter + 1; return x; };
let relay = func(x: int) -> int => counting(x);
let noisy = func(x: int) -> int { console.print_line(\"x\"); return x; };
${entry_open}${entry_close}")
execute_process(COMMAND "${NEURA}" check --task-safety report.nra WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status STREQUAL "0" OR
   NOT stdout MATCHES "(^|\n)report.nra\n  Main.main: task-safe[^\n]*\n  counting: not task-safe: uses the reassignable binding 'counter' at report.nra:5:38\n  noisy: task-safe; not for a parallel map: issues a host request at [^\n]*\n  pure: task-safe\n  relay: not task-safe: uses the reassignable binding 'counter' at report.nra:5:38\n" OR
   NOT stdout MATCHES "\n[0-9]+ function\\(s\\): [0-9]+ task-safe, of which [0-9]+ can also be the worker of a parallel map\n$")
    message(FATAL_ERROR "check --task-safety: unexpected report (status ${status})\n${stdout}\n${stderr}")
endif()
# The audit of section 5.2: with the modules a service needs linked (Std.Core, Std.Text, Std.Collections,
# Std.Io.Console, Std.Net, Std.Time, Std.Log, and JSON with its big numbers), every library function is task-safe
# except console input, whose read-ahead buffer belongs to the root task. A new entry here is a library
# function that a spawned task can no longer call.
file(WRITE "${TEST_DIR}/audit.nra" "let console = import:Std.Io.Console;
let program = import:Std.Program;
let maps = import:Std.Collections.HashMap;
let lists = import:Std.Collections.ArrayList;
let trees = import:Std.Collections.TreeMap;
let floats = import:Std.Core.Float64;
let durations = import:Std.Time.Duration;
let clocks = import:Std.Time.SystemClock;
let instants = import:Std.Time.Instant;
let fixed_clocks = import:Std.Time.FixedClock;
let stopwatches = import:Std.Time.Stopwatch;
let loggers = import:Std.Log.Logger;
let streams = import:Std.Net.TcpStream;
let listeners = import:Std.Net.TcpListener;
let strings = import:Std.Text.Strings;
let regex = import:Std.Text.Regex;
let random = import:Std.Random.Random;
let json = import:Std.Encoding.Json;
export let Main = module : program.Entry (
    export let.override main = func(arguments: string[]) -> program.ExitCode {
        let table = maps.create<string, int>();
        table.set(\"a\", 1);
        console.print_line(table.to_string());
        return program.ExitCode.Ok;
    };
);
")
execute_process(COMMAND "${NEURA}" check --task-safety audit.nra WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
string(REGEX MATCHALL "\n  [^\n]*: not task-safe: [^\n]*" unsafe_lines "${stdout}")
string(REGEX REPLACE ": not task-safe: [^\n;]*" "" unsafe_names "${unsafe_lines}")
string(REGEX REPLACE "\n  " "" unsafe_names "${unsafe_names}")
if(NOT status STREQUAL "0" OR NOT stdout MATCHES "\nstd/log/logger.nra\n" OR NOT stdout MATCHES "\nstd/net/tcp-stream.nra\n" OR
   NOT unsafe_names STREQUAL "Console.discard_pending;Console.read_input;Console.read_line")
    message(FATAL_ERROR "task-safety audit: expected only console input to be unsafe, got '${unsafe_names}' (status ${status})\n${unsafe_lines}\n${stderr}")
endif()

execute_process(COMMAND "${NEURA}" build --task-safety report.nra -o report.nvr WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE stderr)
if(status STREQUAL "0" OR NOT stderr MATCHES "--task-safety applies only to check")
    message(FATAL_ERROR "build accepted --task-safety\n${stderr}")
endif()

# ---------------------------------------------------------------------------
# Phase 2 forms: `scope (name)` and `select` (sections 6 and 7).

set(channels_import "let channels = import:Std.Concurrent.Channel;\n")
set(cancellation_import "let cancellation = import:Std.Concurrent.Cancellation;\nlet observe = func -> bool => cancellation.requested();\n")
set(box_class [=[
let Box = class (
    export let value: int;
    export let init = func(v: int) { value = v; };
);
]=])

check_accepted(scope_named_cancel "${cancellation_import}" [=[
        scope (group) {
            console.print_line(group.is_cancelled().to_string());
            let early = spawn observe();
            group.cancel();
            group.cancel();
            console.print_line(group.is_cancelled().to_string());
            let late = spawn observe();
            // A task whose cancellation was requested before it first ran still starts and sees the request.
            console.print_line(early.join().to_string());
            console.print_line(late.join().to_string());
        }
        scope (other) {
            console.print_line(other.is_cancelled().to_string());
        }
]=] "false\ntrue\ntrue\ntrue\nfalse\n")

check_accepted(select_binding_arm "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        sender.send(5);
        select {
            let message = receiver.receive() => {
                match (message) {
                    channels.Received.Value(value) => { console.print_line("received " + value.to_string()); },
                    _ => { console.print_line("other"); },
                }
            },
        }
]=] "received 5\n")

# An arm without a binding still commits its operation: the value is taken and dropped.
check_accepted(select_arm_without_binding "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        sender.send(5);
        select {
            receiver.receive() => { console.print_line("ready"); },
        }
        match (receiver.try_receive()) {
            channels.TryReceived.Empty => { console.print_line("empty"); },
            _ => { console.print_line("still there"); },
        }
]=] "ready\nempty\n")

check_accepted(select_const_binding "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        sender.send(6);
        select {
            const message = receiver.receive() => {
                match (message) {
                    channels.Received.Value(value) => { console.print_line("received " + value.to_string()); },
                    _ => { console.print_line("other"); },
                }
            },
        }
]=] "received 6\n")

# The plain send before the select resolves Sender.send; see select_send_arm_alone below.
check_accepted(select_send_arm "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(2);
        sender.send(1);
        select {
            let outcome = sender.send(7) => { console.print_line("send " + outcome.to_string()); },
        }
        select {
            let outcome = sender.send(9) => { console.print_line("send " + outcome.to_string()); },
            _ => { console.print_line("full"); },
        }
        match (receiver.receive()) {
            channels.Received.Value(first) => { console.print_line("first " + first.to_string()); },
            _ => { },
        }
        match (receiver.receive()) {
            channels.Received.Value(second) => { console.print_line("second " + second.to_string()); },
            _ => { },
        }
]=] "send Sent\nfull\nfirst 1\nsecond 7\n")

check_accepted(select_join_arm "${work}" [=[
        scope {
            let task = spawn work(41);
            select {
                let value = task.join() => { console.print_line("joined " + value.to_string()); },
            }
        }
]=] "joined 42\n")

check_accepted(select_timer_arm [=[
let result = import:Std.Core.Result;
let timers = import:Std.Concurrent.Timer;
let durations = import:Std.Time.Duration;
]=] [=[
        match (timers.after(durations.of_millis(1))) {
            result.Result.Ok(timer) => {
                select {
                    let outcome = timer.wait() => { console.print_line("timer " + outcome.to_string()); },
                }
            },
            result.Result.Err(_) => { console.print_line("no clock"); },
        }
]=] "timer Fired\n" --allow-clock)

check_accepted(select_default_arm "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        select {
            let message = receiver.receive() => { console.print_line("received"); },
            _ => { console.print_line("nothing was ready"); },
        }
        sender.send(1);
        select {
            let message = receiver.receive() => { console.print_line("received"); },
            _ => { console.print_line("nothing was ready"); },
        }
]=] "nothing was ready\nreceived\n")

# A `_` arm that is not last still runs only when no other arm is ready.
check_accepted(select_default_arm_first "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        select {
            _ => { console.print_line("nothing was ready"); },
            let message = receiver.receive() => { console.print_line("received"); },
        }
        sender.send(1);
        select {
            _ => { console.print_line("nothing was ready"); },
            let message = receiver.receive() => { console.print_line("received"); },
        }
]=] "nothing was ready\nreceived\n")

check_accepted(select_nested "${channels_import}" [=[
        let [first_sender, first, first_closer] = channels.open<int>(1);
        let [second_sender, second, second_closer] = channels.open<int>(1);
        first_sender.send(1);
        second_sender.send(2);
        select {
            let outer = first.receive() => {
                console.print_line("outer");
                select {
                    let inner = second.receive() => {
                        match (inner) {
                            channels.Received.Value(value) => { console.print_line("inner " + value.to_string()); },
                            _ => { console.print_line("inner other"); },
                        }
                    },
                }
            },
        }
]=] "outer\ninner 2\n")

# The contextual words stay ordinary names next to the forms they begin: a local named select, a function named
# scope called as a statement, and fields named spawn and select.
check_accepted(words_next_to_forms [=[
let channels = import:Std.Concurrent.Channel;
let scope = func(x: int) { console.print_line("function scope " + x.to_string()); };
let Box = class (
    export let spawn: int = 4;
    export let select: int = 5;
    export let init = func { };
);
]=] [=[
        let select = 3;
        scope(select);
        let box = new Box();
        console.print_line((box.spawn + box.select + select).to_string());
        let [sender, receiver, closer] = channels.open<int>(1);
        sender.send(select);
        scope {
            select {
                let message = receiver.receive() => { console.print_line("received"); },
            }
        }
        scope (group) {
            console.print_line(group.is_cancelled().to_string());
        }
]=] "function scope 3\n12\nreceived\nfalse\n")

# ---------------------------------------------------------------------------
# Phase 2 rejections. Positions follow from the layout above: a declaration
# block of N lines puts the first body line at N + 5.

check_rejection(select_arm_not_a_call "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        select {
            let value = 5 => { },
        }
]=] "8:25" "a select arm is a call of send, receive, join, or wait")

check_rejection(select_arm_plain_function_call "${channels_import}${work}" [=[
        select {
            work(1) => { },
        }
]=] "8:13" "a select arm is a call of send, receive, join, or wait")

check_rejection(select_arm_not_selectable_member "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        select {
            receiver.try_receive() => { },
        }
]=] "8:13" "a select arm is a call of send, receive, join, or wait")

check_rejection(select_arm_without_the_member "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        select {
            sender.receive() => { },
        }
]=] "8:13" "type Sender<int32> has no selectable member 'receive'")

check_rejection(select_arm_on_a_number "" [=[
        let count = 5;
        select {
            count.receive() => { },
        }
]=] "7:13" "type int32 has no selectable member 'receive'")

check_rejection(select_two_default_arms "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        select {
            let message = receiver.receive() => { },
            _ => { },
            _ => { },
        }
]=] "10:13" "a select has at most one '_' arm")

check_rejection(select_only_default_arm "" [=[
        select {
            _ => { },
        }
]=] "5:9" "a select needs at least one arm that waits")

check_rejection(select_at_top_level [=[
select {
    _ => { },
}
]=] "" "3:1" "select is only allowed inside a function body")

check_rejection(channel_of_class "${box_class}${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<Box>(1);
]=] "10:42" "a value of type Box is not transferable, so it cannot be a channel's element type")

check_rejection(channel_of_array "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int[]>(1);
]=] "6:42" "a value of type int32\\[\\] is not transferable, so it cannot be a channel's element type")

check_rejection(channel_of_list "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<string[list]>(1);
]=] "6:42" "a value of type string\\[list\\] is not transferable, so it cannot be a channel's element type")

check_rejection(channel_of_task [=[
let channels = import:Std.Concurrent.Channel;
let tasks = import:Std.Concurrent.Task;
]=] [=[
        let [sender, receiver, closer] = channels.open<tasks.Task<int>>(1);
]=] "7:56" "a value of type Task<int32> cannot leave its scope: it cannot be a type argument")

check_rejection(channel_of_scope_handle [=[
let channels = import:Std.Concurrent.Channel;
let scopes = import:Std.Concurrent.Scope;
]=] [=[
        let [sender, receiver, closer] = channels.open<scopes.Scope>(1);
]=] "7:56" "a value of type Scope cannot leave its scope: it cannot be a type argument")

check_rejection(channel_of_duration [=[
let channels = import:Std.Concurrent.Channel;
let durations = import:Std.Time.Duration;
]=] [=[
        let [sender, receiver, closer] = channels.open<durations.Duration>(1);
]=] "7:42" "a value of type Duration is not transferable, so it cannot be a channel's element type")

# A function value is transferable when every function value of its type that the program can create captures
# only transferable values (5.1, 5.2), so a channel of functions is accepted until the program makes a closure
# over an object with the same type.
check_accepted(channel_of_function_values "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<func() -> int>(1);
        let answer = func -> int => 42;
        sender.send(answer);
        match (receiver.receive()) {
            channels.Received.Value(function) => { console.print_line(function().to_string()); },
            _ => { console.print_line("none"); },
        }
]=] "42\n")

check_rejection(channel_of_function_values_with_a_capturing_closure "${box_class}${channels_import}" [=[
        let box = new Box(3);
        let read = func -> int => box.value;
        let [sender, receiver, closer] = channels.open<func() -> int>(1);
]=] "12:42" "a value of type func\\(\\) -> int32 is not transferable, so it cannot be a channel's element type")

# A generic function is checked at each specialization, at the user's own call.
check_rejection(channel_of_a_generic_argument "${box_class}${channels_import}\
let relay = func<T>(value: T) {
    let [sender, receiver, closer] = channels.open<T>(1);
    sender.send(value);
};
" [=[
        relay<Box>(new Box(1));
]=] "9:38" "a value of type Box is not transferable, so it cannot be a channel's element type")

# open rejects such a channel, so a program reaches the check on a sent value only by building the end itself.
check_rejection(send_of_a_class "${box_class}${channels_import}" [=[
        let sender = new channels.Sender<Box>(new channels.State());
        sender.send(new Box(1));
]=] "11:9" "a value of type Box is not transferable, so it cannot be sent on a channel")

check_rejection(select_send_of_a_class "${box_class}${channels_import}" [=[
        let [plain, receiver, closer] = channels.open<int>(1);
        plain.send(1);
        let sender = new channels.Sender<Box>(new channels.State());
        select {
            sender.send(new Box(2)) => { },
        }
]=] "13:9" "a value of type Box is not transferable, so it cannot be sent on a channel")

check_rejection(scope_handle_returned [=[
let scopes = import:Std.Concurrent.Scope;
let leak = func -> scopes.Scope {
    scope (group) {
        return group;
    }
};
]=] "" "6:16" "a value of type Scope cannot leave its scope: it cannot be returned from a function")

check_rejection(scope_handle_in_a_field [=[
let scopes = import:Std.Concurrent.Scope;
let Holder = class (
    let handle: scopes.Scope;
);
]=] "" "5:17" "a value of type Scope cannot leave its scope: it cannot be stored in a field or global")

check_rejection(scope_handle_captured_by_lambda "" [=[
        scope (group) {
            let stop = func { group.cancel(); };
            stop();
        }
]=] "6:31" "a value of type Scope cannot leave its scope: a lambda cannot capture it")

check_rejection(scope_handle_passed_to_spawn [=[
let scopes = import:Std.Concurrent.Scope;
let stop = func(group: scopes.Scope) { group.cancel(); };
]=] [=[
        scope (group) {
            spawn stop(group);
        }
]=] "8:24" "a value of type Scope cannot leave its scope: it cannot be passed to spawn")

# A spawned function reaches the handle only through a capture, which a lambda cannot make.
check_rejection(spawn_calls_cancel_on_a_captured_handle "" [=[
        scope (group) {
            let stop = func { group.cancel(); };
            spawn stop();
        }
]=] "6:31" "a value of type Scope cannot leave its scope: a lambda cannot capture it")

check_rejection(scope_handle_in_an_outer_binding "" [=[
        scope (first) {
            let kept = first;
            scope (second) {
                kept = second;
            }
        }
]=] "8:24" "a value of type Scope cannot leave its scope: it can be stored only in a local declared inside that scope")

check_rejection(scope_name_is_constant "" [=[
        scope (group) {
            group = group;
        }
]=] "6:13" "cannot assign to const 'group'")

# `scope (a, b) { }` and `scope () { }` are not scope statements (4.4 recognizes one identifier). The parser
# says what a scope takes, at the parenthesis.
check_rejection(scope_with_two_names "" [=[
        scope (first, second) {
        }
]=] "5:15" "a scope statement names one scope or none: scope \\(name\\) { } or scope { }")

check_rejection(scope_with_no_name "" [=[
        scope () {
        }
]=] "5:15" "a scope statement names one scope or none")

# ---------------------------------------------------------------------------
# Library helpers that spawn. The compiler checks the spawn inside the helper and reports it where the user's
# code calls the helper, with the call chain or the capture that breaks the rule and the library line that
# starts the task.
set(groups_import "let groups = import:Std.Concurrent.WorkerGroup;\nlet tasks = import:Std.Concurrent.Tasks;\nlet result = import:Std.Core.Result;\n")

check_rejection(group_of_class_jobs "${box_class}${groups_import}\
let handle = func(job: Box) -> void { };
" [=[
        groups.with_workers<Box>(1, 1, handle, func(group: groups.WorkerGroup<Box>) -> void { });
]=] "13:9" "a value of type Box is not transferable, so it cannot be a channel's element type")

check_rejection(group_work_writes_a_global "${groups_import}\
let total: int = 0;
let add = func(job: int) -> void { total = total + job; };
" [=[
        groups.with_workers<int>(1, 1, add, func(group: groups.WorkerGroup<int>) -> void { });
]=] "10:9"
    "the spawned call is not task-safe: it reaches 'add', which uses the reassignable binding 'total'")
check_labels(group_work_writes_a_global
    "worker-group.nra\",[^}]*,\"primary\":false,\"label\":\"calls 'run_worker<int32>'\""
    "\"line\":10,\"column\":40,\"primary\":false,\"label\":\"calls 'add'\""
    "\"line\":7,\"column\":36,\"primary\":false,\"label\":\"uses the reassignable binding 'total' here\""
    "worker-group.nra\",[^}]*,\"primary\":false,\"label\":\"the library starts the task here\"")

# When the helper's function value captures an object, the message names the capture.
check_rejection(group_work_captures_an_object "${box_class}${groups_import}" [=[
        let box = new Box(1);
        let add = func(job: int) -> void { box.value = box.value + job; };
        groups.with_workers<int>(1, 1, add, func(group: groups.WorkerGroup<int>) -> void { });
]=] "14:9"
    "a value of type func\\(int32\\) -> void is not transferable, so it cannot be given to a spawned task")
check_labels(group_work_captures_an_object
    "group_work_captures_an_object.nra\",[^}]*,\"primary\":false,\"label\":\"a function value of this type captures a value of type Box here\""
    "worker-group.nra\",[^}]*,\"primary\":false,\"label\":\"the library starts the task here\"")

check_rejection(tasks_all_call_writes_a_global "${groups_import}\
let total: int = 0;
let add = func -> int { total = total + 1; return total; };
" [=[
        let calls: (func() -> int)[list] = [add];
        tasks.all<int>(calls);
]=] "11:9"
    "the spawned call is not task-safe: it reaches 'add', which uses the reassignable binding 'total'")
check_labels(tasks_all_call_writes_a_global
    "\"line\":10,\"column\":45,\"primary\":false,\"label\":\"calls 'add'\""
    "\"line\":7,\"column\":25,\"primary\":false,\"label\":\"uses the reassignable binding 'total' here\""
    "tasks.nra\",[^}]*,\"primary\":false,\"label\":\"the library starts the task here\"")

check_rejection(tasks_all_ok_call_captures_an_object "${box_class}${groups_import}" [=[
        let box = new Box(1);
        let calls: (func() -> result.Result<int>)[list] = [func() -> result.Result<int> => result.Result<int>.Ok(box.value)];
        tasks.all_ok<int>(calls);
]=] "14:9"
    "a value of type func\\(\\) -> Result<int32> is not transferable, so it cannot be given to a spawned task")
check_labels(tasks_all_ok_call_captures_an_object
    "tasks_all_ok_call_captures_an_object.nra\",[^}]*,\"primary\":false,\"label\":\"a function value of this type captures a value of type Box here\""
    "tasks.nra\",[^}]*,\"primary\":false,\"label\":\"the library starts the task here\"")

# ---------------------------------------------------------------------------
# The union rule for function values ranges over the closures that code which can run creates. Std.Io.Console
# holds a byte source that reads the root task's read-ahead buffer, but a program that never asks for it can read
# through a Utf8Reader in a task; one that does ask for it cannot.
set(reader_declarations "let readers = import:Std.Io.Utf8Reader;
let result = import:Std.Core.Result;
let nothing = func(max: int) -> result.Result<uint8[list]> {
    let empty: uint8[list] = [];
    return result.Result<uint8[list]>.Ok(empty);
};
let lines = func -> int => match (readers.create(nothing).read_line()) {
    result.Result.Ok(_) => 1,
    result.Result.Err(_) => 0,
};
")
check_accepted(reader_in_a_task "${reader_declarations}" [=[
        scope {
            let task = spawn lines();
            console.print_line(task.join().to_string());
        }
]=] "1\n")

check_rejection(reader_in_a_task_beside_console_input "${reader_declarations}" [=[
        let standard_input = console.input();
        scope {
            let task = spawn lines();
        }
]=] "17:24" "the spawned call is not task-safe: it reaches '[a-z_]+', which uses the reassignable binding 'pending[a-z_]*'")
check_labels(reader_in_a_task_beside_console_input
    "\"line\":17,\"column\":30,\"primary\":false,\"label\":\"calls 'lines'\""
    "console.nra\",[^}]*,\"primary\":false,\"label\":\"uses the reassignable binding 'pending[a-z_]*' here\"")

# ---------------------------------------------------------------------------
# Forms that an earlier compiler refused.

# A send arm needs no earlier plain send to resolve Sender.send.
check_accepted(select_send_arm_alone "${channels_import}" [=[
        let [sender, receiver, closer] = channels.open<int>(1);
        select {
            let outcome = sender.send(7) => { console.print_line("send " + outcome.to_string()); },
        }
]=] "send Sent\n")

# `scope (name)` in a function with type parameters: each instance has its own scope identity.
check_accepted(named_scope_in_a_generic_function "let run = func<T>(value: T) -> int {
    scope (group) {
        group.cancel();
        return 5;
    }
};
" [=[
        console.print_line(run<int>(1).to_string());
        console.print_line(run<string>("a").to_string());
]=] "5\n5\n")

message(STATUS "concurrency frontend conformance passed")
