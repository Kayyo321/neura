if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(MAKE_DIRECTORY "${TEST_DIR}")

function(run_success output)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${ARGN} failed with status ${status}\n${stderr}")
    endif()
    set("${output}" "${stdout}" PARENT_SCOPE)
endfunction()

function(check_rejection name source location message_pattern)
    set(file "${name}.nra")
    file(WRITE "${TEST_DIR}/${file}" "// Rejected frontend fixture.\n${source}\n")
    execute_process(
        COMMAND "${NEURA}" check --error-format=short "${file}"
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "Expected ${name} to fail with a diagnostic, got ${status}\n${stdout}\n${stderr}")
    endif()
    if(NOT stderr MATCHES "${name}[.]nra:${location}: error(\\[[EW][0-9]+\\])?:" OR NOT stderr MATCHES "${message_pattern}")
        message(FATAL_ERROR "Missing diagnostic for ${name}\n${stderr}")
    endif()
endfunction()

file(GLOB_RECURSE examples "${SOURCE_DIR}/examples/*.nra")
if(NOT examples)
    message(FATAL_ERROR "No Neura examples were found")
endif()
# A program cannot both spawn a task and use native bindings
# (docs/lang/Concurrency.md, C15), and the examples include one of each, so
# the concurrency examples are checked as programs of their own.
set(concurrency_examples ${examples})
list(FILTER concurrency_examples INCLUDE REGEX "/examples/concurrency/")
list(FILTER examples EXCLUDE REGEX "/examples/concurrency/")
if(NOT concurrency_examples)
    message(FATAL_ERROR "No concurrency examples were found")
endif()
list(SORT examples)
run_success(unused check ${examples})
foreach(example IN LISTS concurrency_examples)
    run_success(unused check "${example}")
endforeach()

set(multi_file "${SOURCE_DIR}/examples/modules/multi-file")
run_success(unused check -I "${multi_file}" "${multi_file}/main.nra")
run_success(unused check -I "${SOURCE_DIR}/examples/guide" "${SOURCE_DIR}/examples/testing/math-tests.nra")

set(core "${SOURCE_DIR}/examples/guide/core.nra")
set(types "${SOURCE_DIR}/examples/types")
set(predicate "${TEST_DIR}/predicate.nra")
file(WRITE "${predicate}" [=[
let both = func(left: bool, right: bool) -> bool {
    return left && right;
};
]=])
set(ir_inputs "${core}" "${types}/classes.nra" "${types}/collections.nra"
    "${types}/polymorphism.nra" "${predicate}")
# Inspect lowering output directly; the optimizer has its own tests.
run_success(ir emit-ir --no-optimize ${ir_inputs})
run_success(repeated_ir emit-ir --no-optimize ${ir_inputs})
if(NOT ir MATCHES "^neura[.]ir" OR NOT ir MATCHES "sum_to" OR NOT ir MATCHES "int32")
    message(FATAL_ERROR "Expected typed NeuraIR for the frontend fixtures\n${ir}")
endif()
foreach(operation IN ITEMS loop branch short_circuit virtual_call append remove_at)
    if(NOT ir MATCHES "\n +${operation} ")
        message(FATAL_ERROR "Expected a ${operation} operation in NeuraIR\n${ir}")
    endif()
endforeach()
if(NOT ir MATCHES "type_arguments=<Alert#[0-9]+>" OR NOT ir MATCHES "bound=Renderable#[0-9]+")
    message(FATAL_ERROR "Expected resolved generic arguments and bounds\n${ir}")
endif()
if(NOT ir STREQUAL repeated_ir)
    message(FATAL_ERROR "Repeated frontend runs emitted different NeuraIR for the same input")
endif()

check_rejection(lexical "let count = @;" "2:13" "[@]")

# The lexer and the parser report every syntax error in source order, up to
# --max-errors, and the first one is the one a single-error run reported.
function(count_errors output)
    execute_process(
        COMMAND "${NEURA}" check --error-format=short ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        ERROR_VARIABLE stderr
    )
    string(REGEX MATCHALL "error\\[E[0-9]+\\]" found "${stderr}")
    list(LENGTH found count)
    if(status STREQUAL "0")
        message(FATAL_ERROR "Expected ${ARGN} to fail\n${stderr}")
    endif()
    set("${output}" "${count}" PARENT_SCOPE)
    set("${output}_stderr" "${stderr}" PARENT_SCOPE)
endfunction()
file(WRITE "${TEST_DIR}/several-lexical.nra" "let a = 1 $ 2;\nlet b = 3 @ 4;\nlet c = 5 ` 6;\n")
count_errors(lexical several-lexical.nra)
if(NOT lexical EQUAL 3 OR NOT lexical_stderr MATCHES "^[^\n]*several-lexical[.]nra:1:11: error\\[E0101\\]")
    message(FATAL_ERROR "Expected three lexical errors, the first at 1:11\n${lexical_stderr}")
endif()
count_errors(limited --max-errors 2 several-lexical.nra)
if(NOT limited EQUAL 2)
    message(FATAL_ERROR "--max-errors 2 should stop after two errors\n${limited_stderr}")
endif()
file(WRITE "${TEST_DIR}/several-syntax.nra" [=[
let a = func() -> int {
    let x = (1 + 2;
    return x;
};
let b = ;
let C = class (
    export let size;
    export let label: string = "c";
);
let d = 4
let e = 5;
]=])
count_errors(syntax several-syntax.nra)
if(NOT syntax EQUAL 4 OR NOT syntax_stderr MATCHES "2:19: error\\[E0203\\][^\n]*\n[^\n]*5:9: error\\[E0204\\][^\n]*\n[^\n]*7:20: error\\[E0205\\][^\n]*\n[^\n]*10:10: error\\[E0201\\]")
    message(FATAL_ERROR "Expected four syntax errors with no cascade\n${syntax_stderr}")
endif()
count_errors(one --max-errors=1 several-syntax.nra)
if(NOT one EQUAL 1)
    message(FATAL_ERROR "--max-errors=1 should stop after one error\n${one_stderr}")
endif()
check_rejection(syntax "let count: int32 = ;" "2:20" "expected")
check_rejection(old-parameter-syntax "let f = func(int value) {};" "2:18" "expected ':'")
check_rejection(rest-position "let f = func(args: ..., last: int) {};" "2:23" "variadic parameter must be last")
check_rejection(range-float "let values = 1.5 .. 4.5;" "2:14" "range requires integer bounds")
check_rejection(type "let count: int32 = true;" "2:[1-9][0-9]*" "int32|bool|type")
check_rejection(import "let missing = import:Missing.Module;" "2:[1-9][0-9]*" "Missing|import")
check_rejection(return [=[
let compute = func() -> int32 {
    return true;
};
]=] "[23]:[1-9][0-9]*" "int32|bool|type")
check_rejection(assignment [=[
const Limit: int32 = 4;
Limit = 5;
]=] "3:[1-9][0-9]*" "const|assign|writable|mutable")
check_rejection(array-mutation [=[
let names: string[] = ["Ada"];
names.append("Lin");
]=] "3:[1-9][0-9]*" "append|list|array|member")
check_rejection(closure-capture-assignment [=[
let make = func() -> func() -> int {
    let number = 1;
    return func() -> int {
        number = 2;
        return number;
    };
};
]=] "5:[1-9][0-9]*" "a captured binding cannot be reassigned")
check_rejection(closure-argument-type [=[
let run = func() -> int {
    let callback = func(number: int) -> int { return number; };
    return callback(true);
};
]=] "4:[1-9][0-9]*" "expected int32, got bool")
check_rejection(forward-value [=[
let earlier: int32 = later;
let later: int32 = 0;
]=] "2:[1-9][0-9]*" "before its declaration")
check_rejection(duplicate-super [=[
let Base = class ();
let Derived = class:Base (
    export let init = func() -> void {
        super.init();
        super.init();
    };
);
]=] "6:[1-9][0-9]*" "super.init must be the first statement")

check_rejection(duplicate-overload [=[
let identify = func(value: int) -> int => value
    or func(other: int32) -> bool => other == 0;
]=] "3:[1-9][0-9]*" "duplicate|same parameter")
check_rejection(ambiguous-overload [=[
let identify = func(value: int16) -> bool => true
    or func(value: int64) -> bool => false;
let use = func -> bool => identify(2);
]=] "4:[1-9][0-9]*" "ambiguous")
check_rejection(crossed-overload [=[
let identify = func(first: int32, second: int16) -> bool => true
    or func(first: int16, second: int32) -> bool => false;
let use = func -> bool => identify(1, 2);
]=] "4:[1-9][0-9]*" "ambiguous")
check_rejection(no-overload [=[
let identify = func(value: int16) -> bool => true
    or func(value: string) -> bool => false;
let use = func -> bool => identify(nil, nil);
]=] "4:[1-9][0-9]*" "overload|argument")
check_rejection(cast-result [=[
let Box = class (
    export let.as int64 = func -> string => "wrong type";
);
]=] "3:[1-9][0-9]*" "int64.*string|string.*int64|cast.*(result|return)")
check_rejection(cast-parameter [=[
let Box = class (
    export let.as int64 = func(value: int64) -> int64 => value;
);
]=] "3:[1-9][0-9]*" "parameter|argument")
check_rejection(duplicate-cast [=[
let Box = class (
    export let.as int = func -> int => 1;
    export let.as int32 = func -> int32 => 2;
);
]=] "4:[1-9][0-9]*" "duplicate|already")
check_rejection(private-cast [=[
let Box = class (
    let.as int64 = func -> int64 => 1;
);
let use = func -> int64 => new Box() as int64;
]=] "5:[1-9][0-9]*" "cast|convert|private|export")
check_rejection(private-argument-cast [=[
let Box = class (
    let.as int64 = func -> int64 => 1;
);
let accept = func(value: int64) -> int64 => value;
let use = func -> int64 => accept(new Box());
]=] "6:[1-9][0-9]*" "int64|cast|convert|private|export")
check_rejection(no-chained-cast [=[
let Middle = class (
    export let.as int64 = func -> int64 => 1;
);
let Box = class (
    export let.as Middle = func -> Middle => new Middle();
);
let accept = func(value: int64) -> int64 => value;
let use = func -> int64 => accept(new Box());
]=] "9:[1-9][0-9]*" "int64|cast|convert")

check_rejection(reference-nil [=[
let unavailable: &int = nil;
]=] "2:[1-9][0-9]*" "nil requires a class reference type")
check_rejection(reference-literal [=[
let temporary: &int = 10;
]=] "2:[1-9][0-9]*" "requires mutable storage")
check_rejection(reference-const [=[
const Constant = 10;
let alias: &int = Constant;
]=] "3:[1-9][0-9]*" "requires mutable storage")
check_rejection(reference-collection [=[
let invalid: &int[] = [1];
]=] "2:[1-9][0-9]*" "primitive reference needs")
check_rejection(reference-field-implicit [=[
let View = class (
    let value: &int;
);
]=] "3:[1-9][0-9]*" "needs an initializer or constructor")
check_rejection(reference-field-missing-path [=[
let View = class (
    let value: &int;
    let init = func(storage: &int, bind: bool) {
        if (bind) {
            value = storage;
        }
    };
);
]=] "4:[1-9][0-9]*" "exit before binding reference field 'value'")
check_rejection(reference-field-ambiguous [=[
let View = class (
    let value: &int;
    let init = func(storage: &int, bind: bool) {
        if (bind) {
            value = storage;
        }
        value = storage;
    };
);
]=] "8:[1-9][0-9]*" "reference field.*value.*bound")
check_rejection(reference-field-read [=[
let View = class (
    let value: &int;
    let snapshot: int = value;
    let init = func(storage: &int) {
        value = storage;
    };
);
]=] "4:[1-9][0-9]*" "reference field.*value.*bound")

# Loops, conversions, and remainders lower to existing control flow plus the
# NeuraIR 1.1 operations; a for loop's step is a latch.
set(loops "${TEST_DIR}/loops.nra")
file(WRITE "${loops}" [=[
let count = func(values: int32[list], limit: int) -> int {
    let total: int = 0;
    for (let i: int = 0; i < limit; i = i + 1) {
        total = total + i % 3;
    }
    for (let value in values) {
        total = total + value;
    }
    return total + (limit as int8) as int32;
};
]=])
run_success(loop_ir emit-ir --no-optimize "${loops}")
foreach(operation IN ITEMS loop latch convert collection_version fail length index)
    if(NOT loop_ir MATCHES "\n +${operation}")
        message(FATAL_ERROR "Expected ${operation} in the lowered loops\n${loop_ir}")
    endif()
endforeach()
if(loop_ir MATCHES "[:<, ]int[^0-9a-z_]" OR loop_ir MATCHES "byte")
    message(FATAL_ERROR "int and byte should lower to int32 and uint8\n${loop_ir}")
endif()

check_rejection(float-remainder "let r: float64 = 1.5 % 2.0;" "2:[1-9][0-9]*" "operator '%' is not defined for float64")
check_rejection(do-condition "do {} while (1);" "2:14" "expected bool, got int32")
check_rejection(conditional-arm "let result: int = true ? 1 : false;" "2:30" "expected int32, got bool")
check_rejection(const-update [=[
const Fixed = 1;
Fixed++;
]=] "3:1" "cannot assign to const 'Fixed'")
check_rejection(compound-remainder [=[
let value: float64 = 2.0;
value %= 1.0;
]=] "3:1" "operator '%' is not defined for float64")
check_rejection(bool-conversion "let n: int32 = true as int32;" "2:[1-9][0-9]*" "cannot convert bool to int32")
check_rejection(string-conversion [=[
let n = func(text: string) -> int32 {
    return text as int32;
};
]=] "3:[1-9][0-9]*" "cannot convert string to int32")
check_rejection(for-each-scalar [=[
let f = func(value: int32) -> void {
    for (let item in value) {
    }
};
]=] "3:22" "for-each requires an array or list or Iterable<T>, got int32")
check_rejection(for-each-element-type [=[
let f = func(values: int32[]) -> void {
    for (let item: string in values) {
    }
};
]=] "3:20" "expected string, got int32")
check_rejection(for-each-class [=[
let Bag = class (
    export let size: int32 = 0;
);
let f = func(bag: Bag) -> void {
    for (let item in bag) {
    }
};
]=] "6:22" "for-each requires an array or list or Iterable<T>, got Bag")
check_rejection(for-each-iterable-element-type [=[
let ranges = import:Std.Core.IntRange;
let f = func(r: ranges.IntRange) -> void {
    for (let item: string in r) {
    }
};
]=] "4:20" "expected string, got int32")
check_rejection(for-step-break [=[
let f = func() -> void {
    for (let i: int32 = 0; i < 3; break) {
    }
};
]=] "3:35" "a for loop step must be an assignment or an expression")
check_rejection(for-scope [=[
let f = func() -> int32 {
    for (let i: int32 = 0; i < 3; i = i + 1) {
    }
    return i;
};
]=] "5:12" "unknown name 'i'")
check_rejection(element-scope [=[
let f = func(values: int32[]) -> int32 {
    for (let value in values) {
    }
    return value;
};
]=] "5:12" "unknown name 'value'")
check_rejection(for-each-const [=[
let f = func(values: int32[]) -> void {
    for (const value in values) {
        value = 1;
    }
};
]=] "4:9" "cannot assign to const 'value'")
check_rejection(in-keyword "let in = 3;" "2:5" "expected an identifier")

# Generic rules: bounds are checked at the type argument, generic inheritance
# is not part of the language, and generic methods do not override.
check_rejection(unsatisfied-bound [=[
let Shape = class (
    export let sides = func() -> int32 {
        return 0;
    };
);
let count = func<T:Shape>(item: T) -> int32 {
    return item.sides();
};
let use = func() -> int32 {
    return count<int32>(3);
};
]=] "11:18" "type argument int32 does not satisfy the bound Shape of type parameter 'T'")
check_rejection(generic-inheritance [=[
let Box = class<T> (
    export let value: T;
);
let Labeled = class:Box (
);
]=] "5:21" "generic inheritance is not supported")
check_rejection(generic-override [=[
let Base = class (
    export let make = func() -> int32 {
        return 0;
    };
);
let Derived = class:Base (
    export let.override make = func<T>() -> int32 {
        return 1;
    };
);
]=] "8:5" "override does not match inherited member 'make'")
check_rejection(missing-type-arguments [=[
let identity = func<T>(value: T) -> T {
    return value;
};
let use = func() -> int32 {
    return identity(3);
};
]=] "6:12" "expected 1 type argument")

# Enums: variants are qualified values of their enum; only equality and
# conversion to an integer apply.
check_rejection(unknown-variant [=[
let Light = enum (Red, Green);
let pick = func() -> Light {
    return Light.Blue;
};
]=] "4:12" "unknown or inaccessible name 'Light.Blue'|unknown or inaccessible member 'Blue'")
check_rejection(empty-enum "let Nothing = enum ();" "2:21" "an enum needs at least one variant")
check_rejection(enum-ordering [=[
let Light = enum (Red, Green);
let before = func(a: Light, b: Light) -> bool {
    return a < b;
};
]=] "4:12" "operator '<' is not defined for Light")
check_rejection(integer-to-enum [=[
let Light = enum (Red, Green);
let pick = func() -> Light {
    return 0 as Light;
};
]=] "4:12" "cannot convert int32 to Light")
check_rejection(duplicate-variant "let Light = enum (Red, Red);" "2:24" "duplicate declaration of 'Red'")

check_rejection(nonexhaustive-match [=[
let Option = enum (None, Some(int));
let unwrap = func(value: Option) -> int {
    return match (value) { Option.Some(n) => n };
};
]=] "4:12" "non-exhaustive match, missing variant 'None'")
check_rejection(wrong-payload-type [=[
let Option = enum (None, Some(int));
let result = Option.Some(true);
]=] "3:[1-9][0-9]*" "expected int32, got bool")
check_rejection(wrong-tuple-arity [=[
let f = func() -> [int, bool] { return [1, true]; };
let [value] = f();
]=] "3:1" "destructuring requires a tuple with one element per binding")

check_rejection(chain-with-comparison [=[
let wrong = func(x: int, y: int) -> bool {
    return x == 0 and y == 1;
};
]=] "3:25" "a comparison chain expects another value")
check_rejection(mixed-chain-connectors [=[
let wrong = func(x: int) -> bool {
    return x == 1 or 2 and 3;
};
]=] "3:24" "a comparison chain must use only")
check_rejection(noninteger-shift-count [=[
let wrong = func(x: int) -> int {
    return x << true;
};
]=] "3:17" "a shift count must be an integer")

check_rejection(implicit-base-constructor [=[
let Base = class (
    export let init = func(value: int) -> void {};
);
let Middle = class:Base ();
let Derived = class:Middle (
    export let init = func() -> void {
        super.init();
    };
);
]=] "8:9" "class 'Middle' needs a constructor to initialize its base")

# Float literals convert directly to their format; values that do not fit are
# compile-time errors.
check_rejection(float16-overflow "let big: float16 = 65520.0;" "2:20" "float literal is outside the range of float16")
check_rejection(float64-overflow "let big: float64 = 1e309;" "2:20" "float literal is outside the range of float64")
check_rejection(float32-underflow "let tiny: float32 = 1e-50;" "2:21" "nonzero float literal rounds to zero in float32")
check_rejection(mixed-float "let x: float32 = 1.0; let y: float64 = x;" "2:40" "expected float64, got float32")
check_rejection(float-int-mix [=[
let f = func(x: float32, n: int32) -> float32 {
    return x * n;
};
]=] "3:16" "expected float32, got int32")

check_rejection(nil-scalar "let number: int = nil;" "2:19" "nil requires a class reference type")
check_rejection(construct-umbrella "let value = new class();" "2:13" "class is an umbrella type and cannot be constructed")

check_rejection(read-discard "let _ = 3; let read = _;" "2:23" "unknown name '_'")
check_rejection(missing-discard-value "let _: int;" "2:1" "requires a value initializer")
check_rejection(invalid-promotion "let.promote x = 3;" "2:1" "let[.]promote requires")
check_rejection(named-wildcard "let.promote time = import:Std.Time.*;" "2:20" "wildcard imports require")
check_rejection(ambiguous-promoted-name [=[
let.promote _ = import:Std.Time.*;
let parse_time = parse;
]=] "3:18" "ambiguous promoted name 'parse'")
check_rejection(static-receiver [=[
let Record = class (
    let value = 1;
    export static let read = func -> int => value;
);
]=] "4:45" "instance member 'value' requires a receiver")
check_rejection(static-type-parameter [=[
let Record = class<T> (
    export static let read = func(value: T) -> T => value;
);
]=] "3:[1-9][0-9]*" "static member cannot use an enclosing class type parameter")
check_rejection(singleton-new "let one = singleton (); let another = new one();" "2:39" "singleton cannot be constructed")
check_rejection(singleton-destroy "let one = singleton (); destroy one;" "2:33" "singleton cannot be destroyed")

# Accessors of exported instance fields (Types.md, Accessors).
check_rejection(accessor-read-only [=[
let Ticket = class (
    export let code = "XY";
        | get: code;
);
let ticket = new Ticket();
ticket.code = "AB";
]=] "7:1" "E0510.*'code' is read-only outside its class")
check_rejection(accessor-read-only-subclass [=[
let Ticket = class (
    export let code = "XY";
        | get: code;
);
let Renewed = class:Ticket (
    export let renew = func {
        code = "AB";
    };
);
]=] "8:9" "E0510.*'code' is read-only outside its class")
check_rejection(accessor-write-only [=[
let Sink = class (
    export let level: int = 0;
        | set: level = value;
);
let sink = new Sink();
let current = sink.level;
]=] "7:15" "E0515.*'level' is write-only outside its class")
check_rejection(accessor-write-only-update [=[
let Sink = class (
    export let level: int = 0;
        | set: level = value;
);
let sink = new Sink();
sink.level--;
]=] "7:1" "E0515.*'level' is write-only outside its class")
check_rejection(accessor-not-exported [=[
let Account = class (
    let balance = 0;
        | get: balance;
);
]=] "4:9" "E0516.*accessors require an exported field")
check_rejection(accessor-static [=[
let Limits = class (
    export static let calls = 0;
        | get: calls;
);
]=] "4:9" "E0517.*a static field cannot have accessors")
check_rejection(accessor-method [=[
let Shape = class (
    export let area = func -> int => 4;
        | get: 4;
);
]=] "4:9" "E0518.*'area' is a function")
check_rejection(accessor-module [=[
let Settings = module (
    export let retries = 3;
        | get: retries;
);
]=] "4:9" "E0519.*only allowed on class fields")
check_rejection(accessor-interface [=[
let Sized = interface (
    let size = func -> int;
        | get: 0;
);
]=] "4:9" "E0519.*an interface cannot declare accessors")
check_rejection(accessor-duplicate [=[
let Counter = class (
    export let count = 0;
        | set: count = value;
        | set: count = value + 1;
);
]=] "5:11" "E0520.*duplicate 'set' accessor for 'count'")
check_rejection(accessor-constant [=[
let Config = class (
    export const name = "default";
        | set: name = value;
);
]=] "4:11" "E0521.*a .const. field cannot have a setter")
check_rejection(accessor-value-field [=[
let Cell = class (
    export let value = 0;
        | set: value = value;
);
]=] "4:11" "E0522.*a field named 'value' cannot have a setter")
check_rejection(accessor-reference-field [=[
let Tracker = class (
    export let target: &int;
        | get: target;
);
]=] "4:9" "E0523.*a primitive reference field cannot have accessors")
check_rejection(accessor-reference-binding [=[
let Counter = class (
    export let count: int = 0;
        | get: count;
        | set: count = value;
);
let counter = new Counter();
let alias: &int = counter.count;
]=] "8:19" "E0524.*cannot refer to 'count', which has accessors")
check_rejection(accessor-getter-type [=[
let Counter = class (
    export let count: int = 0;
        | get: count > 0;
);
]=] "4:16" "E0403.*expected int32, got bool")
check_rejection(accessor-clause-word [=[
let Counter = class (
    export let count = 0;
        | put: count;
);
]=] "4:11" "E0202.*expected 'get' or 'set' after '[|]'")

# Host callbacks are reserved for the trusted standard library.
check_rejection(user-host-callback [=[
let Tools = module (
    let handler = func(value: int64) -> int64 {
        return value;
    } callback("user.callback", 1);
);
]=] "3:5" "host callbacks are only allowed in standard library modules")

# `last` bindings (CoreLanguage.md, "Deferred assignment"). `last` is a
# contextual word: it declares only when an identifier follows it.
file(WRITE "${TEST_DIR}/last-accepted.nra" [=[
let Collection = class (
    export let last = func -> int => 3;
    export last first: int;
    export last size: int = 2;
    static last limit: int = 9;
    export let init = func(value: int) {
        if (value > 0) {
            first = value;
        } else {
            instance.first = 0;
        }
    };
);
let last = func(values: int[]) -> int => values[values.length - 1];
let tail = func(values: int[]) -> int {
    let last = [0, 0];
    last[0] = values[0];
    last = [1, 2];
    return last[0];
};
let latest = func(items: Collection) -> int => items.last() + last([1, 2]);
let choose = func(flag: bool, other: bool) -> int {
    last value: int;
    if (flag && other) {
        value = 1;
    } else if (flag || other) {
        value = 2;
    } else {
        return 0;
    }
    last doubled = value * 2;
    let read = func() -> int => doubled + value;
    return read();
};
let guarded = func(flag: bool) -> int {
    last value: int;
    if (!flag) {
        trap_with_message("unreachable");
    }
    value = 1;
    while (true) {
        last step: int;
        step = value;
        if (step > 0) {
            break;
        }
    }
    return value;
};
]=])
run_success(unused check "${TEST_DIR}/last-accepted.nra")
run_success(last_ir emit-ir --no-optimize "${TEST_DIR}/last-accepted.nra")
if(NOT last_ir MATCHES "@binding[.]last[(]true[)]" OR NOT last_ir MATCHES "@binding[.]last[(]false[)]")
    message(FATAL_ERROR "Expected binding.last attributes on the `last` bindings\n${last_ir}")
endif()

check_rejection(last-twice [=[
let f = func() -> int {
    last x: int;
    x = 10;
    x = 20;
    return x;
};
]=] "5:5" "\\[E0440\\]: cannot assign twice to 'x'")
check_rejection(last-maybe-twice [=[
let f = func(flag: bool) -> int {
    last x: int;
    if (flag) {
        x = 1;
    }
    x = 2;
    return x;
};
]=] "7:5" "\\[E0440\\]: cannot assign twice to 'x'")
check_rejection(last-update [=[
let f = func() -> int {
    last x: int;
    x = 1;
    x += 1;
    return x;
};
]=] "5:5" "\\[E0440\\]: cannot assign twice to 'x'")
check_rejection(last-unassigned-read [=[
let f = func() -> int {
    last x: int;
    return x;
};
]=] "4:12" "\\[E0441\\]: 'x' is used before it is assigned")
check_rejection(last-maybe-read [=[
let f = func(flag: bool) -> int {
    last x: int;
    if (flag) {
        x = 1;
    }
    return x;
};
]=] "7:12" "\\[E0441\\]: 'x' may be used before it is assigned")
check_rejection(last-match-read [=[
let Shape = enum(Circle(int), Dot);
let f = func(shape: Shape) -> int {
    last x: int;
    match (shape) {
        Shape.Circle(r) => {
            x = r;
        },
        Shape.Dot => {
        },
    }
    return x;
};
]=] "12:12" "\\[E0441\\]: 'x' may be used before it is assigned")
check_rejection(last-short-circuit-read [=[
let f = func(flag: bool) -> bool {
    last x: bool;
    return flag && x;
};
]=] "4:20" "\\[E0441\\]")
check_rejection(last-in-loop [=[
let f = func(count: int) -> int {
    last x: int;
    while (count > 0) {
        x = count;
        break;
    }
    return 0;
};
]=] "5:9" "\\[E0442\\]: 'x' is assigned inside a loop")
check_rejection(last-early-capture [=[
let f = func() -> int {
    last x: int;
    let read = func() -> int => x;
    x = 1;
    return read();
};
]=] "4:33" "\\[E0443\\]: closure captures 'x' before it is assigned")
check_rejection(last-without-type [=[
let f = func() -> int {
    last x;
    x = 1;
    return x;
};
]=] "3:10" "\\[E0444\\]: a deferred 'last' binding needs a type")
check_rejection(last-reassigned [=[
let f = func() -> int {
    last x = 10;
    x = 20;
    return x;
};
]=] "4:5" "\\[E0401\\]: cannot assign twice to 'x'")
check_rejection(last-field-method [=[
let Box = class (
    export last size: int;
    export let init = func(initial: int) {
        size = initial;
    };
    export let grow = func {
        size = size + 1;
    };
);
]=] "8:9" "\\[E0445\\]: cannot assign to 'last' field 'size' here")
check_rejection(last-field-outside [=[
let Box = class (
    export last size: int;
    export let init = func(initial: int) {
        size = initial;
    };
);
let resize = func(box: Box) {
    box.size = 3;
};
]=] "9:5" "\\[E0445\\]")
check_rejection(last-field-skipped [=[
let Box = class (
    export last size: int;
    export let init = func(initial: int) {
        if (initial > 0) {
            size = initial;
        }
    };
);
]=] "4:16" "\\[E0446\\]: 'init' may end without assigning 'last' field 'size'")
check_rejection(last-field-overload [=[
let Box = class (
    export last size: int;
    export let init = func(initial: int) {
        size = initial;
    } or func() {
    };
);
]=] "6:10" "\\[E0446\\]")
check_rejection(last-field-early-return [=[
let Box = class (
    export last size: int;
    export let init = func(initial: int) {
        if (initial < 0) {
            return;
        }
        size = initial;
    };
);
]=] "6:13" "\\[E0446\\]: 'init' may return without assigning 'last' field 'size'")
check_rejection(last-field-no-init [=[
let Box = class (
    export last size: int;
);
]=] "3:17" "\\[E0446\\]: class 'Box' has no 'init'")
check_rejection(last-field-read-in-init [=[
let Box = class (
    export last size: int;
    export let doubled: int = 0;
    export let init = func(initial: int) {
        doubled = instance.size * 2;
        size = initial;
    };
);
]=] "6:19" "\\[E0441\\]: 'size' is used before it is assigned")
check_rejection(last-field-with-value [=[
let Box = class (
    export last size: int = 3;
    export let init = func(initial: int) {
        size = initial;
    };
);
]=] "5:9" "\\[E0401\\]: cannot assign twice to 'size'")
check_rejection(last-global-deferred "last limit: int;" "2:6" "\\[E0447\\]")
check_rejection(last-static-deferred [=[
let Counter = class (
    static last limit: int;
);
]=] "3:17" "\\[E0447\\]")
check_rejection(let-without-value-mentions-last [=[
let f = func() -> int {
    let x: int;
    return 0;
};
]=] "3:5" "\\[E0404\\]")
