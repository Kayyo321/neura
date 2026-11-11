if(NOT DEFINED NEURA OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA and TEST_DIR")
endif()

file(MAKE_DIRECTORY "${TEST_DIR}")
set(source "${TEST_DIR}/documented.nra")
set(windows "${TEST_DIR}/windows.nra")
set(output "${TEST_DIR}/index.json")

file(WRITE "${source}" [=[
let dependency = import:"missing.nir";
let prior = 1; /// A trailing comment.
let trailing = 2;
/// This block is separated by a blank line.

let separated = 3;
/// An ordinary comment interrupts this block.
// Ordinary comment.
let interrupted = 4;
/// @brief Public module.
export let Example = module (
    /// Returns "é" unchanged.
    /// @param[in] value A UTF-8 string.
    ///   May include a backslash.
    /// \return The same string.
    /// @code
    /// @param inside_code literal text
    /// @endcode
    export let echo = func(value: string) -> string {
        /// A local constant.
        const result = value;
        /// Applies to both named bindings.
        let [first, second, _] = [result, result, 0];
        return first + second;
    };
    /// First overload.
    export let convert = func(value: int32) -> int32 => value
        /// Second overload.
        or
        func(value: string) -> string => value;
    /// A status enumeration.
    export let Status = enum (
        /// The operation succeeded.
        Ready,
        Failed
    );
);
/// A class with cast overloads.
let Wrapped = class (
    /// The stored value.
    let value: int16 = 0;
    /// Reads the stored value.
    export let.as int16 = func -> int16 => value
        /// Formats the stored value.
        or as string = func -> string => value.to_string();
);
/// Increments the caller's integer.
let increment = func(value: &int) {
    value += 1;
};
]=])
string(ASCII 239 187 191 bom)
file(WRITE "${windows}" "${bom}/// First line\r\n///\r\n/// Third line\r\nlet value = 1;\r\n")

function(run_docs output_var)
    execute_process(COMMAND "${NEURA}" docs ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE json ERROR_VARIABLE error)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Documentation export failed (${status})\n${error}")
    endif()
    set("${output_var}" "${json}" PARENT_SCOPE)
endfunction()

run_docs(index "${source}" "${windows}")
run_docs(unused "${source}" "${windows}" -o "${output}")
file(READ "${output}" written)
if(NOT index STREQUAL written OR NOT unused STREQUAL "")
    message(FATAL_ERROR "File export differs from standard output")
endif()

function(expect_json expected)
    string(JSON actual GET "${index}" ${ARGN})
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "Expected ${ARGN} to be '${expected}', got '${actual}'")
    endif()
endfunction()

expect_json(1 version)
string(JSON count LENGTH "${index}" files 0 symbols)
if(NOT count EQUAL 14)
    message(FATAL_ERROR "Expected 14 documented declarations, got ${count}\n${index}")
endif()
expect_json("Example" files 0 symbols 0 name)
expect_json("module" files 0 symbols 0 kind)
expect_json("brief" files 0 symbols 0 documentation tags 0 command)
expect_json(11 files 0 symbols 0 declaration line)
expect_json(12 files 0 symbols 0 selection column)
expect_json("echo = func(value: string) -> string" files 0 symbols 1 signature)
expect_json("Example" files 0 symbols 1 scope 0)
expect_json(12 files 0 symbols 1 documentation range line)
expect_json(5 files 0 symbols 1 documentation range column)
expect_json(19 files 0 symbols 1 declaration line)
expect_json("param" files 0 symbols 1 documentation tags 0 command)
expect_json("value" files 0 symbols 1 documentation tags 0 subject)
expect_json("in" files 0 symbols 1 documentation tags 0 direction)
expect_json("A UTF-8 string.\n  May include a backslash." files 0 symbols 1 documentation tags 0 text)
expect_json("return" files 0 symbols 1 documentation tags 1 command)
expect_json("code" files 0 symbols 1 documentation tags 2 command)
expect_json("\n@param inside_code literal text" files 0 symbols 1 documentation tags 2 text)
expect_json("endcode" files 0 symbols 1 documentation tags 3 command)
expect_json("constant" files 0 symbols 2 kind)
expect_json("echo" files 0 symbols 3 scope 1)
expect_json("second" files 0 symbols 4 name)
expect_json("convert = func(value: int32) -> int32" files 0 symbols 5 signature)
expect_json("convert = func(value: string) -> string" files 0 symbols 6 signature)
expect_json("Second overload." files 0 symbols 6 documentation text)
expect_json("variant" files 0 symbols 8 kind)
expect_json("Status" files 0 symbols 8 scope 1)
expect_json("Ready" files 0 symbols 8 name)
expect_json("Wrapped" files 0 symbols 9 name)
expect_json("value" files 0 symbols 10 name)
expect_json("let.as int16 = func() -> int16" files 0 symbols 11 signature)
expect_json("let.as string = func() -> string" files 0 symbols 12 signature)
expect_json("increment = func(value: &int) -> void" files 0 symbols 13 signature)
expect_json("First line\n\nThird line" files 1 symbols 0 documentation text)
expect_json(3 files 1 symbols 0 documentation range offset)
expect_json(4 files 1 symbols 0 declaration line)

run_docs(stubs --doxygen "${source}")
file(READ "${source}" original)
string(REGEX MATCHALL "\n" original_newlines "${original}")
string(REGEX MATCHALL "\n" stub_newlines "${stubs}")
list(LENGTH original_newlines original_count)
list(LENGTH stub_newlines stub_count)
if(NOT original_count EQUAL stub_count)
    message(FATAL_ERROR "Doxygen filter changed the number of source lines")
endif()
foreach(pattern IN ITEMS
        "/// @brief Public module[.]\nnamespace Example"
        "/// @endcode\nstring echo[(]string value[)];"
        "/// First overload[.]\nint32 convert[(]int32 value[)];"
        "/// Second overload[.]\nstring convert[(]string value[)];"
        "/// The operation succeeded[.]\nstatic const Status Ready;"
        "/// Formats the stored value[.]\noperator string[(][)];"
        "/// Increments the caller's integer[.]\nvoid increment[(]int& value[)];")
    if(NOT stubs MATCHES "${pattern}")
        message(FATAL_ERROR "Doxygen filter lost a comment, name, or signature\n${stubs}")
    endif()
endforeach()
if(stubs MATCHES "trailing comment|ordinary comment interrupts|local constant")
    message(FATAL_ERROR "Doxygen filter exposed an unattached or local comment")
endif()

# `last` declarations print with their keyword; `last` stays a name elsewhere.
set(last_source "${TEST_DIR}/last.nra")
file(WRITE "${last_source}" [=[
/// A class with assign-once fields.
let Account = class (
    /// Assigned by init.
    export last owner: string;
    /// Fixed at declaration.
    export last currency: string = "EUR";
    /// A method named last.
    export let last = func -> string => owner;
    export let init = func(name: string) {
        owner = name;
    };
);
]=])
run_docs(index "${last_source}")
expect_json("last owner: string" files 0 symbols 1 signature)
expect_json("constant" files 0 symbols 1 kind)
expect_json("last currency: string" files 0 symbols 2 signature)
expect_json("last = func() -> string" files 0 symbols 3 signature)
