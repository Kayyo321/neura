# Installs neura and the standard library into a fresh prefix that the
# per-area library tests share, and checks that the installed tool resolves
# Std imports from its own share/neura/stdlib without any -I root.
if(NOT DEFINED BINARY_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide BINARY_DIR, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
set(config_args)
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    set(config_args --config "${CONFIG}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --prefix "${TEST_DIR}/prefix" ${config_args}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "cmake --install failed\n${output}")
endif()
set(neura "${TEST_DIR}/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}")

# The installed tree holds exactly the library's source modules.
file(GLOB_RECURSE sources RELATIVE "${SOURCE_DIR}/stdlib" "${SOURCE_DIR}/stdlib/*.nra")
file(GLOB_RECURSE installed RELATIVE "${TEST_DIR}/prefix/share/neura/stdlib" "${TEST_DIR}/prefix/share/neura/stdlib/*")
list(SORT sources)
list(SORT installed)
if(NOT sources STREQUAL installed)
    message(FATAL_ERROR "installed library differs from stdlib/:\nsource: ${sources}\ninstalled: ${installed}")
endif()

# A program in a directory with no library nearby builds and runs with the
# installed tool alone.
file(MAKE_DIRECTORY "${TEST_DIR}/away")
file(WRITE "${TEST_DIR}/away/hello.nra" [=[
let console = import:Std.Io.Console;
let program = import:Std.Program;

export let Hello = module:program.Entry (
    export let.override main = func(string[] args) -> program.ExitCode {
        console.print_line("installed library");
        return program.ExitCode.Ok;
    };
);
]=])
execute_process(COMMAND "${neura}" build hello.nra -o hello.nvr WORKING_DIRECTORY "${TEST_DIR}/away"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "the installed compiler could not resolve Std imports\n${errors}")
endif()
execute_process(COMMAND "${neura}" run hello.nvr WORKING_DIRECTORY "${TEST_DIR}/away"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status STREQUAL "0" OR NOT stdout STREQUAL "installed library\n" OR NOT stderr STREQUAL "")
    message(FATAL_ERROR "installed hello printed '${stdout}' with status ${status}\n${stderr}")
endif()

# The image labels library blocks by their library path, never by where the
# library was installed.
file(STRINGS "${TEST_DIR}/away/hello.nvr" image_strings)
foreach(string IN LISTS image_strings)
    if(string MATCHES "${TEST_DIR}")
        message(FATAL_ERROR "the image contains an installation path: ${string}")
    endif()
endforeach()
