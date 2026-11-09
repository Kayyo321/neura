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
        COMMAND "${NEURA}" check "${file}"
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "Expected ${name} to fail with a diagnostic, got ${status}\n${stdout}\n${stderr}")
    endif()
    if(NOT stderr MATCHES "${name}[.]nra:${location}: error:" OR NOT stderr MATCHES "${message_pattern}")
        message(FATAL_ERROR "Missing diagnostic for ${name}\n${stderr}")
    endif()
endfunction()

file(GLOB_RECURSE examples "${SOURCE_DIR}/examples/*.nra")
if(NOT examples)
    message(FATAL_ERROR "No Neura examples were found")
endif()
list(SORT examples)
run_success(unused check ${examples})

set(multi_file "${SOURCE_DIR}/examples/modules/multi-file")
run_success(unused check -I "${multi_file}" "${multi_file}/main.nra")
run_success(unused check -I "${SOURCE_DIR}/examples/guide" "${SOURCE_DIR}/examples/testing/math-tests.nra")

set(core "${SOURCE_DIR}/examples/guide/core.nra")
set(types "${SOURCE_DIR}/examples/types")
set(predicate "${TEST_DIR}/predicate.nra")
file(WRITE "${predicate}" [=[
let both = func(bool left, bool right) -> bool {
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
check_rejection(syntax "let count: int32 = ;" "2:20" "expected")
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
    }
);
]=] "6:[1-9][0-9]*" "super.init must be the first statement")
