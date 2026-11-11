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

# Tags (docs/lang/Tags.md).
function(run_status output status_output)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    set("${output}" "${stdout}${stderr}" PARENT_SCOPE)
    set("${status_output}" "${status}" PARENT_SCOPE)
endfunction()

file(WRITE "${TEST_DIR}/tag-warnings.nra" [=[
let Api = module (
    #Deprecated(as_of: "0.2.0", use_instead: fresh)
    export let stale = func() -> int32 {
        return 1;
    };

    export let fresh = func() -> int32 {
        return 2;
    };

    #Experimental
    export let preview = func() -> int32 {
        return 3;
    };
);

let a: int32 = Api.stale();
let b: int32 = Api.preview();
]=])
# Warnings print on success; -Wno-<name> hides one; -Werror fails the run.
run_status(output status check --error-format=short tag-warnings.nra)
if(NOT status STREQUAL "0" OR NOT output MATCHES "tag-warnings[.]nra:17:16: warning\\[W0001\\]: function 'stale' is deprecated since 0[.]2[.]0"
   OR NOT output MATCHES "tag-warnings[.]nra:18:16: warning\\[W0002\\]")
    message(FATAL_ERROR "Expected W0001 and W0002 on a successful check (${status})\n${output}")
endif()
run_status(output status check --error-format=short -Wno-deprecated tag-warnings.nra)
if(NOT status STREQUAL "0" OR output MATCHES "W0001" OR NOT output MATCHES "W0002")
    message(FATAL_ERROR "Expected -Wno-deprecated to hide only W0001 (${status})\n${output}")
endif()
run_status(output status check --error-format=short -Wno-experimental -Wno-deprecated tag-warnings.nra)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "")
    message(FATAL_ERROR "Expected no output with both warnings disabled (${status})\n${output}")
endif()
run_status(output status check --error-format=short -Werror tag-warnings.nra)
if(NOT status STREQUAL "1" OR NOT output MATCHES "error\\[W0001\\]" OR NOT output MATCHES "error\\[W0002\\]")
    message(FATAL_ERROR "Expected -Werror to fail with both warnings as errors (${status})\n${output}")
endif()
# --fix applies the machine-applicable replacement and the tag spelling.
file(WRITE "${TEST_DIR}/tag-fix.nra" [=[
let Api = module (
    #Depricated(use_instead: fresh)
    export let stale = func() -> int32 {
        return 1;
    };

    export let fresh = func() -> int32 {
        return 2;
    };
);

let a: int32 = Api.stale();
]=])
run_success(unused check --fix tag-fix.nra)
file(READ "${TEST_DIR}/tag-fix.nra" fixed)
if(NOT fixed MATCHES "#Deprecated[(]use_instead: fresh[)]" OR NOT fixed MATCHES "let a: int32 = Api[.]fresh[(][)];")
    message(FATAL_ERROR "check --fix did not fix the tag and the deprecated use\n${fixed}")
endif()

# Tag validation: each rule's code at its span.
check_rejection(tag-unknown "#Depricated\nlet f = func() -> int32 {\n    return 1;\n};" "2:2" "unknown tag #Depricated")
check_rejection(tag-target "#Inline\nlet Shape = class ();" "2:2" "#Inline does not apply to a class")
check_rejection(tag-variant-target "let Color = enum (\n    #Final Red\n);" "3:6" "#Final does not apply to an enum variant")
check_rejection(tag-parameter-target "let f = func(#Deprecated value: int32) -> int32 {\n    return value;\n};" "2:15" "#Deprecated does not apply to a parameter")
check_rejection(tag-local-target "let f = func() -> int32 {\n    #Stable\n    let value: int32 = 1;\n    return value;\n};" "3:6" "#Stable does not apply to a local binding")
check_rejection(tag-lambda-parameter "let f = func() -> int32 {\n    let g = func(#Unit(1) value: int32) -> int32 {\n        return value;\n    };\n    return g(1);\n};" "3:24" "#Unit expects a string")
check_rejection(tag-too-many "#Since(\"0.1.0\", \"0.2.0\")\nlet x: int32 = 1;" "2:17" "#Since takes 1 argument")
check_rejection(tag-no-arguments "#Stable(true)\nlet x: int32 = 1;" "2:9" "#Stable takes no arguments")
check_rejection(tag-list "#Since([\"0.1.0\"])\nlet x: int32 = 1;" "2:8" "found a list")
check_rejection(tag-integer-range "#Unit(\"ms\")\n#Range(min: 0, max: 99999999999999999999)\nlet x: int32 = 1;" "3:21" "does not fit in 64 bits")
check_rejection(tag-reserved-memoize "#Memoize\nlet f = func() -> int32 {\n    return 1;\n};" "2:1" "#Memoize is reserved")
check_rejection(tag-reserved-checked "let C = class (\n    #Immutable\n    let x: int32 = 0;\n);" "3:5" "#Immutable is reserved")
check_rejection(tag-final-sealed "#[Final, Sealed]\nlet C = class ();" "2:10" "#Sealed conflicts with #Final")
check_rejection(tag-hot-cold "#Hot\n#Cold\nlet f = func() -> int32 {\n    return 1;\n};" "3:1" "#Cold conflicts with #Hot")
check_rejection(tag-discard-name "#Stable\nlet _ = 3;" "2:1" "tags cannot apply to '_'")
check_rejection(tag-end-of-file "let x: int32 = 1;\n#Stable" "3:8" "expected a declaration after tags")
check_rejection(tag-deny-code "let Api = module (\n    #Deprecated\n    export let old = func() -> int32 {\n        return 1;\n    };\n);\n#Deny(W0001)\nlet x: int32 = Api.old();" "9:16" "'old' is deprecated")
# A #Sealed type at the top of a file may be extended anywhere in the file.
file(WRITE "${TEST_DIR}/tag-sealed-file.nra" "#Sealed\nlet Base = class ();\nlet Holder = module (\n    export let Inner = class:Base ();\n);\n")
run_success(unused check tag-sealed-file.nra)
# A tag on an or-func group applies to every branch.
file(WRITE "${TEST_DIR}/tag-overloads.nra" [=[
let Convert = module (
    #Deprecated
    export let show = func(value: int32) -> string {
        return value.to_string();
    } or func(value: bool) -> string {
        return value.to_string();
    };
);
let a: string = Convert.show(1);
let b: string = Convert.show(true);
]=])
run_status(output status check --error-format=short tag-overloads.nra)
string(REGEX MATCHALL "warning\\[W0001\\]" overload_warnings "${output}")
list(LENGTH overload_warnings overload_count)
if(NOT status STREQUAL "0" OR NOT overload_count EQUAL 2)
    message(FATAL_ERROR "Expected a W0001 for each call of a deprecated overload group (${status})\n${output}")
endif()

# #Internal: usable from the declaring file's directory and below, not from
# a parent directory.
file(MAKE_DIRECTORY "${TEST_DIR}/tag-library/acme")
file(WRITE "${TEST_DIR}/tag-library/acme/core.nra" [=[
let Core = module (
    #Internal
    export let raw = func() -> int32 {
        return 1;
    };
    export let cooked = func() -> int32 {
        return raw() + 1;
    };
);
]=])
file(WRITE "${TEST_DIR}/tag-library/acme/report.nra" [=[
let core = import:Acme.Core;
let Report = module (
    export let total = func() -> int32 {
        return core.raw() + core.cooked();
    };
);
]=])
file(WRITE "${TEST_DIR}/tag-library/app.nra" [=[
let report = import:Acme.Report;
let core = import:Acme.Core;
let total: int32 = report.total() + core.cooked();
]=])
run_success(unused check -I tag-library tag-library/acme/report.nra)
run_success(unused check -I tag-library tag-library/app.nra)
file(APPEND "${TEST_DIR}/tag-library/app.nra" "let raw: int32 = core.raw();\n")
run_status(output status check --error-format=short -I tag-library tag-library/app.nra)
if(NOT status STREQUAL "1" OR NOT output MATCHES "app[.]nra:4:18: error\\[E0911\\]: function 'raw' is internal to its library")
    message(FATAL_ERROR "Expected E0911 for a use from outside the library (${status})\n${output}")
endif()

# The catalogue and the explain pages of warnings.
run_success(catalogue tags)
if(NOT catalogue MATCHES "#Deprecated[(]as_of: string = \"\", use_instead: name = \"\", reason: string = \"\"[)]"
   OR NOT catalogue MATCHES "status:  reserved" OR NOT catalogue MATCHES "[0-9]+ tags: [0-9]+ implemented")
    message(FATAL_ERROR "neura tags did not print the catalogue\n${catalogue}")
endif()
run_success(one_tag tags Final)
if(NOT one_tag MATCHES "^#Final\n    status:  implemented" OR NOT one_tag MATCHES "applies to classes and methods")
    message(FATAL_ERROR "neura tags Final did not print the entry\n${one_tag}")
endif()
run_status(output status tags Depricated)
if(NOT status STREQUAL "1" OR NOT output MATCHES "did you mean Deprecated")
    message(FATAL_ERROR "Expected neura tags to reject an unknown tag with a suggestion (${status})\n${output}")
endif()
run_success(page explain deprecated)
run_success(code_page explain W0001)
if(NOT page MATCHES "^# W0001: use of a deprecated declaration" OR NOT page STREQUAL code_page)
    message(FATAL_ERROR "neura explain did not print the W0001 page by name and by code\n${page}")
endif()

# A tag persists in the NeuraIR as the `tag` attribute and survives a .nir
# round trip; inline.directive follows #Inline(always: true) and #NoInline.
file(WRITE "${TEST_DIR}/tag-ir.nra" [=[
let Lib = module (
    #[Deprecated(as_of: "0.2.0"), Since("0.1.0")]
    export let old = func() -> int32 {
        return 1;
    };
    #Inline(always: true)
    export let small = func(value: int32) -> bool {
        return value > 0;
    };
    #NoInline
    export let kept = func(value: int32) -> bool {
        return value < 0;
    };
);
]=])
run_success(listing emit-ir --no-optimize tag-ir.nra)
if(NOT listing MATCHES "function \"old\"[^\n]*@tag[(]\\[1u, \"Deprecated\", \\[\\[\"as_of\", \"0[.]2[.]0\"\\]\\]"
   OR NOT listing MATCHES "@tag[(]\\[5u, \"Since\""
   OR NOT listing MATCHES "function \"small\"[^\n]*@inline[.]directive[(]1u[)]"
   OR NOT listing MATCHES "function \"kept\"[^\n]*@inline[.]directive[(]2u[)]")
    message(FATAL_ERROR "Expected tag and inline.directive attributes in the listing\n${listing}")
endif()
run_success(unused emit-ir tag-ir.nra -o tag-ir.nir)
run_success(round_trip emit-ir --no-optimize tag-ir.nir)
string(REGEX MATCHALL "@tag[(][^\n]*" source_tags "${listing}")
string(REGEX MATCHALL "@tag[(][^\n]*" artifact_tags "${round_trip}")
if(NOT source_tags OR NOT source_tags STREQUAL artifact_tags)
    message(FATAL_ERROR "The tags differ after a .nir round trip\n${listing}\n---\n${round_trip}")
endif()
