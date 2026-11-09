# Installs neura into a fresh prefix, builds Hello World with the installed
# compiler, and runs the image with the installed runner from a directory
# that contains only the image.
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
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "cmake --install failed\n${output}")
endif()
set(neura "${TEST_DIR}/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}")
if(NOT EXISTS "${neura}")
    message(FATAL_ERROR "the installation has no ${neura}")
endif()

file(MAKE_DIRECTORY "${TEST_DIR}/image")
execute_process(
    COMMAND "${neura}" build "${SOURCE_DIR}/examples/hello-world/hello-world.nra" -o "${TEST_DIR}/image/hello.nvr"
    RESULT_VARIABLE status
    ERROR_VARIABLE errors
)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "the installed compiler failed to build Hello World\n${errors}")
endif()
execute_process(
    COMMAND "${neura}" run hello.nvr
    WORKING_DIRECTORY "${TEST_DIR}/image"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status STREQUAL "0" OR NOT stdout STREQUAL "Hello, World!\n" OR NOT stderr STREQUAL "")
    message(FATAL_ERROR "the installed runner printed '${stdout}' with status ${status}\n${stderr}")
endif()
