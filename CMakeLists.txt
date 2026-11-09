cmake_minimum_required(VERSION 3.20)
project(Neura LANGUAGES CXX)

function(neura_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(neura_compiler STATIC
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/neura-ir/lowering.cpp
    src/neura-ir/ir_print.cpp
)
target_include_directories(neura_compiler PUBLIC src)
neura_target_defaults(neura_compiler)

add_executable(neura src/cli/main.cpp)
target_link_libraries(neura PRIVATE neura_compiler)
neura_target_defaults(neura)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME frontend COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/frontend-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend.cmake"
    )
endif()
