# Helpers of the CMake-script lifecycle tests (version.cmake). The scenario
# suites of the install lifecycle are in scenarios.py.
#
# Every child process reads its standard input from the null device:
# /dev/null, or NUL on Windows.
if(CMAKE_HOST_WIN32)
    set(lc_null_device NUL)
else()
    set(lc_null_device /dev/null)
endif()

# lc_exec(<prefix> [ENV NAME=value...] [WORKING_DIRECTORY dir] COMMAND args...)
# Runs the command and sets <prefix>_STATUS, <prefix>_OUT, and <prefix>_ERR.
function(lc_exec prefix)
    cmake_parse_arguments(ARG "" "WORKING_DIRECTORY" "ENV;COMMAND" ${ARGN})
    set(directory)
    if(ARG_WORKING_DIRECTORY)
        set(directory WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${ARG_ENV} ${ARG_COMMAND}
        ${directory}
        INPUT_FILE "${lc_null_device}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(${prefix}_STATUS "${status}" PARENT_SCOPE)
    set(${prefix}_OUT "${out}" PARENT_SCOPE)
    set(${prefix}_ERR "${err}" PARENT_SCOPE)
endfunction()

function(lc_expect_status what prefix expected)
    if(NOT "${${prefix}_STATUS}" STREQUAL "${expected}")
        message(FATAL_ERROR "${what}: expected exit status ${expected}, got ${${prefix}_STATUS}\n"
            "stdout:\n${${prefix}_OUT}\nstderr:\n${${prefix}_ERR}")
    endif()
endfunction()

function(lc_expect_contains what text needle)
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${what}: expected the text to contain\n  ${needle}\nbut it is\n${text}")
    endif()
endfunction()

function(lc_expect_equal what actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "${what}: expected\n[${expected}]\nbut got\n[${actual}]")
    endif()
endfunction()
