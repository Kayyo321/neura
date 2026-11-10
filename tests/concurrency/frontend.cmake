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

# The fixture files begin with the two prelude lines, so a declaration on
# line N of `declarations` is on line N + 2, and a line of `body` follows
# the declarations and the two lines that open the entry module.
function(check_rejection name declarations body location message_pattern)
    write_program("${name}" "${declarations}" "${body}")
    execute_process(
        COMMAND "${NEURA}" check "${name}.nra"
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "Expected ${name} to be rejected, got status ${status}\n${stdout}\n${stderr}")
    endif()
    string(FIND "${stderr}" "${name}.nra:${location}: error: " at)
    if(at EQUAL -1 OR NOT stderr MATCHES "${message_pattern}")
        message(FATAL_ERROR "${name}: expected a diagnostic at ${location} matching '${message_pattern}'\n${stderr}")
    endif()
    # `build` must refuse the same program: the checks do not depend on the command.
    execute_process(
        COMMAND "${NEURA}" build "${name}.nra" -o "${name}.nvr"
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE build_status
        OUTPUT_QUIET ERROR_VARIABLE build_stderr
    )
    string(FIND "${build_stderr}" "${name}.nra:${location}: error: " build_at)
    if(build_status STREQUAL "0" OR build_at EQUAL -1 OR EXISTS "${TEST_DIR}/${name}.nvr")
        message(FATAL_ERROR "${name}: build did not report the same diagnostic\n${build_stderr}")
    endif()
endfunction()

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
    execute_process(COMMAND "${NEURA}" run "${name}.nvr" WORKING_DIRECTORY "${TEST_DIR}"
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
]=] "9:13" "not task-safe: it reaches 'audit' \\(called at call_writes_global.nra:9:19\\), and it reaches 'bump' \\(called at call_writes_global.nra:5:32\\), and that code uses the reassignable binding 'counter' at call_writes_global.nra:4:19")

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

message(STATUS "concurrency frontend conformance passed")
