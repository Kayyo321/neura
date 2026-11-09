cmake_minimum_required(VERSION 3.20)
project(Neura LANGUAGES CXX)

add_executable(neura
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/lowering.cpp
    src/ir_print.cpp
)
target_compile_features(neura PRIVATE cxx_std_20)
set_target_properties(neura PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(neura PRIVATE /W4)
else()
    target_compile_options(neura PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME frontend COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/frontend-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend.cmake"
    )
endif()
