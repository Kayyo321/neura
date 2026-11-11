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

# Host callbacks are reserved for the trusted standard library.
check_rejection(user-host-callback [=[
let Tools = module (
    let handler = func(value: int64) -> int64 {
        return value;
    } callback("user.callback", 1);
);
]=] "3:5" "host callbacks are only allowed in standard library modules")

# Target-typed new (ObjectLifecycle.md, Target-typed new): the expected type
# must be a concrete class, and constructor errors are those of new T(...).
set(pair_class "let Pair = class (\n    export let left: int = 0;\n    export let init = func(a: int) { left = a; };\n);\n")
check_rejection(new-no-expected-type "let a = new;" "2:9" "E0420.*target-typed 'new' needs a concrete class type")
check_rejection(new-umbrella "let a: class = new;" "2:16" "E0420")
check_rejection(new-primitive "let a: int = new;" "2:14" "E0420")
check_rejection(new-type-parameter "let make = func<T>() -> T { return new; };" "2:36" "E0420")
check_rejection(new-singleton "let one = singleton (); let a: one = new;" "2:38" "E0420")
check_rejection(new-interface [=[
let Shape = interface (
    let area = func() -> int;
);
let a: Shape = new;
]=] "5:16" "E0420")
check_rejection(new-unknown-member "let a = new.left;" "2:9" "E0420")
check_rejection(new-argument-count "${pair_class}let a: Pair = new;" "6:15" "E0407.*expected 1 argument")
check_rejection(new-argument-type "${pair_class}let a: Pair = new(\"x\");" "6:19" "E0403.*expected int32, got string")
check_rejection(new-parenthesized-type "${pair_class}let a = new (Pair)(1);" "6:9" "E0435")
check_rejection(new-ambiguous-overload [=[
let A = class ( export let x: int = 0; );
let B = class ( export let y: int = 0; );
let pick = func(a: A) -> int => 1 or func(b: B) -> int => 2;
let chosen = pick(new);
]=] "5:14" "E0409.*ambiguous overload")
check_rejection(new-abstract [=[
let Shape = class (
    export let area = func() -> int;
);
let a: Shape = new;
]=] "5:16" "E0505.*cannot instantiate abstract class")
check_rejection(new-hidden-constructor [=[
let Locked = class (
    let init = func() { };
);
let a: Locked = new;
]=] "5:17" "E0507.*constructor is not exported")
check_rejection(new-in-untyped-list "let items = [new];" "2:14" "E0420")

# Typed multi-binding (Types.md, Tuples).
check_rejection(multi-binding-value-count "let [a, b] = 1, 2, 3;" "2:14" "E0436.*3 values for 2 names")
check_rejection(multi-binding-type-count "let [a, b]: [int] = 1, 2;" "2:13" "E0436.*1 type for 2 names")
check_rejection(multi-binding-annotation "let [a, b]: int[] = 1, 2;" "2:13" "E0437")
check_rejection(multi-binding-element-type "let [a, b]: [int, string] = 1, 2;" "2:32" "E0403.*expected string, got int32")
check_rejection(multi-binding-tuple-type "let [a, b]: [int, string] = [1, 2];" "2:33" "E0403.*expected string, got int32")
check_rejection(multi-binding-names-not-in-scope [=[
let f = func() -> int {
    let [a, b] = 1, a;
    return b;
};
]=] "3:21" "E0304.*unknown name 'a'")
check_rejection(multi-binding-duplicate [=[
let f = func() -> int {
    let [a, a] = 1, 2;
    return a;
};
]=] "3:13" "E0301.*duplicate local binding 'a'")

# Anonymous classes (Types.md, Anonymous classes).
check_rejection(anonymous-distinct-occurrences [=[
let first: class (export let n: int = 0; export let get = func() -> int => n;) = new;
let second: class (export let n: int = 0; export let get = func() -> int => n;) = first;
]=] "3:83" "E0403.*expected class[(]n: int32[)]@anonymous-distinct-occurrences[.]nra:3:13, got class[(]n: int32[)]@anonymous-distinct-occurrences[.]nra:2:12")
check_rejection(anonymous-field-order [=[
let point: class (export let x: int = 0; export let label: string = "";) = new;
let swapped: class (export let label: string; export let x: int;) = point;
]=] "3:69" "E0403.*expected class[(]label: string, x: int32[)], got class[(]x: int32, label: string[)]")
check_rejection(anonymous-field-type [=[
let point: class (export let x: int = 0;) = new;
let wide: class (export let x: int64;) = point;
]=] "3:42" "E0403.*expected class[(]x: int64[)], got class[(]x: int32[)]")
check_rejection(anonymous-initializer-in-parameter "let f = func(p: class (export let x: int = 1;)) -> int => p.x;" "2:44" "E0530")
check_rejection(anonymous-initializer-in-result "let f = func() -> class (export let x: int = 1;) { return new; };" "2:46" "E0530")
check_rejection(anonymous-initializer-in-element "let items: class (export let x: int = 1;)[] = [];" "2:39" "E0530")
check_rejection(anonymous-type-parameter "let f = func<T>(v: T) -> int { let b: class (export let item: T;) = new; return 0; };" "2:63" "E0531")
check_rejection(anonymous-unexported "let p: class (let x: int = 1;) = new;" "2:19" "E0532")
check_rejection(anonymous-assignment-new [=[
let p: class (export let x: int = 1;) = new;
let reset = func() { p = new; };
]=] "3:26" "E0533")
check_rejection(anonymous-element-new "let items: class (export let x: int;)[] = [new];" "2:44" "E0533")
check_rejection(anonymous-untyped "let p: class (export let x = 1;) = new;" "2:26" "E0534")
check_rejection(anonymous-const "let p: class (export const x: int = 1;) = new;" "2:28" "E0535")
check_rejection(anonymous-duplicate-field "let p: class (export let x: int; export let x: int;) = new;" "2:45" "E0302.*duplicate declaration of 'x'")
check_rejection(anonymous-unknown-field "let p: class (export let x: int = 1;) = new; let y = p.y;" "2:54" "E0305.*unknown or inaccessible member 'y'")
check_rejection(anonymous-arguments "let p: class (export let x: int;) = new(1);" "2:37" "E0407.*expected 0 argument")
check_rejection(anonymous-new-structural-arguments "let p = new class (export let x: int = 1;)(2);" "2:9" "E0407")
check_rejection(anonymous-unexported-method-field [=[
let p: class (
    let x: int = 1;
    export let get = func() -> int => x;
) = new;
let y = p.x;
]=] "6:9" "E0305.*unknown or inaccessible member 'x'")
check_rejection(anonymous-hidden-init [=[
let p: class (
    export let x: int = 1;
    let init = func() { };
) = new;
]=] "5:5" "E0507.*constructor is not exported")
check_rejection(anonymous-not-a-base [=[
let Tools = module ( );
let p: class : Tools (export let x: int = 1;) = new;
]=] "3:16" "E0503.*base must have the same declaration kind")
check_rejection(anonymous-umbrella-construct "let value = new class();" "2:13" "class is an umbrella type and cannot be constructed")

# The umbrella type keeps its meaning where a class body could not begin, and
# a class body with a base or interfaces is accepted in a type position.
file(WRITE "${TEST_DIR}/umbrella.nra" [=[
let Point = class ( export let x: int = 0; );
let Named = interface ( let name = func() -> string; );
let thing: class = new Point();
let tested = thing is class;
let back = thing as class;
let pick = func(c: bool, a: Point, b: class) -> class => c ? a as class : b;
let empty: class () = new;
let derived: class : Point implements Named ( export let name = func() -> string => "d"; ) = new;
let named: class implements Named ( export let name = func() -> string => "n"; ) = new;
let generic = func<T: class>(value: T) -> T => value;
]=])
run_success(unused check "${TEST_DIR}/umbrella.nra")
