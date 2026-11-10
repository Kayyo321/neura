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

# The NVR image format: the model shared by the writer and the loader.
add_library(neura_nvr STATIC
    src/nvr/loader.cpp
    src/nvr/names.cpp
    src/nvr/writer.cpp
)
target_include_directories(neura_nvr PUBLIC src)
neura_target_defaults(neura_nvr)

# The Neura Virtual Machine. It depends only on the image format, so a
# runtime-only distribution needs no compiler code.
add_library(neura_nvm STATIC
    src/nvm/fs_provider.cpp
    src/nvm/host.cpp
    src/nvm/machine.cpp
    src/nvm/net_provider.cpp
    src/nvm/providers.cpp
    src/nvm/scalar_text.cpp
    src/nvm/trace.cpp
    src/nvm/trace_file.cpp
)
target_link_libraries(neura_nvm PUBLIC neura_nvr)
neura_target_defaults(neura_nvm)

add_library(neura_compiler STATIC
    src/cli/source_loader.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/neura-ir/lowering.cpp
    src/neura-ir/ir_print.cpp
    src/neura-ir/integer.cpp
    src/neura-ir/float.cpp
    src/neura-ir/control_flow.cpp
    src/neura-ir/specialize.cpp
    src/neura-ir/static_initialization.cpp
    src/neura-ir/verifier.cpp
    src/neura-ir/nir_format.cpp
    src/ir-optimization/analysis.cpp
    src/ir-optimization/integer_folding.cpp
    src/ir-optimization/optimizer.cpp
    src/ir-optimization/passes.cpp
    src/backend/block_synth.cpp
    src/backend/circuit.cpp
    src/backend/float_circuit.cpp
    src/backend/converter.cpp
)
target_include_directories(neura_compiler PUBLIC src)
target_link_libraries(neura_compiler PUBLIC neura_nvr)
neura_target_defaults(neura_compiler)

# The decompiler reads images through the shared loader and lifts their
# graphs by regenerating them with the converter's circuit library.
add_library(neura_decompiler STATIC
    src/decompiler/assembly.cpp
    src/decompiler/decompile.cpp
    src/decompiler/lifter.cpp
)
target_link_libraries(neura_decompiler PUBLIC neura_compiler neura_nvm)
neura_target_defaults(neura_decompiler)

add_executable(neura src/cli/main.cpp)
target_link_libraries(neura PRIVATE neura_compiler neura_nvm neura_decompiler)
neura_target_defaults(neura)

# The compiler and the runner ship as one executable: `neura run` loads
# images through the same NVM library as any other embedder.
install(TARGETS neura RUNTIME DESTINATION bin)
install(DIRECTORY stdlib/ DESTINATION share/neura/stdlib OPTIONAL
    FILES_MATCHING PATTERN "*.nra")

include(CTest)
if(BUILD_TESTING)
    add_test(NAME frontend COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/frontend-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend.cmake"
    )

    add_executable(neura_ir_tests
        tests/ir-optimization/behavior_tests.cpp
        tests/ir-optimization/evaluator.cpp
        tests/ir-optimization/float_reference.cpp
        tests/ir-optimization/main.cpp
        tests/ir-optimization/structure_tests.cpp
        tests/ir-optimization/support.cpp
        tests/ir-optimization/verifier_tests.cpp
    )
    target_link_libraries(neura_ir_tests PRIVATE neura_compiler)
    target_include_directories(neura_ir_tests PRIVATE tests)
    neura_target_defaults(neura_ir_tests)

    # Each suite is a separate test, so a failure names the kind of evidence
    # that broke.
    foreach(suite IN ITEMS structure verifier behavior)
        add_test(NAME ir-optimization-${suite}
            COMMAND neura_ir_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()

    add_executable(neura_backend_tests
        tests/backend/assembler.cpp
        tests/backend/circuit_tests.cpp
        tests/backend/conversion_tests.cpp
    tests/backend/decompiler_tests.cpp
        tests/backend/fixtures.cpp
    tests/backend/float_tests.cpp
        tests/backend/format_tests.cpp
    tests/backend/generics_tests.cpp
        tests/backend/host_tests.cpp
        tests/backend/nir_tests.cpp
        tests/backend/reference_tests.cpp
        tests/backend/vm_tests.cpp
        tests/ir-optimization/evaluator.cpp
        tests/ir-optimization/float_reference.cpp
        tests/ir-optimization/main.cpp
        tests/ir-optimization/support.cpp
    )
    target_link_libraries(neura_backend_tests PRIVATE neura_compiler neura_nvm neura_decompiler)
    target_include_directories(neura_backend_tests PRIVATE tests)
    neura_target_defaults(neura_backend_tests)

    foreach(suite IN ITEMS circuits floats format vm host nir conversion generics)
        add_test(NAME backend-${suite}
            COMMAND neura_backend_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()

    # Standard library tests under the reference evaluator and NVM.
    add_test(NAME backend-reference COMMAND neura_backend_tests reference "${CMAKE_CURRENT_SOURCE_DIR}")

    foreach(suite IN ITEMS lossless lift behavior)
        add_test(NAME decompiler-${suite}
            COMMAND neura_backend_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()

    add_test(NAME backend-cli COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/backend-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/cli.cmake"
    )

    add_test(NAME backend-stdlib COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/backend-stdlib-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/stdlib.cmake"
    )

    add_test(NAME backend-install COMMAND ${CMAKE_COMMAND}
        "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-tests"
        "-DCONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/install.cmake"
    )

    # The standard library: one fixture installs the tool and the library
    # into a fresh prefix, and each area of tests/stdlib runs against that
    # installation (tests/stdlib/area.cmake describes the layout).
    add_test(NAME stdlib-install COMMAND ${CMAKE_COMMAND}
        "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/stdlib-install"
        "-DCONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/install.cmake"
    )
    set_tests_properties(stdlib-install PROPERTIES FIXTURES_SETUP stdlib)
    file(GLOB stdlib_areas LIST_DIRECTORIES true CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/*")
    foreach(area_dir IN LISTS stdlib_areas)
        if(NOT IS_DIRECTORY "${area_dir}")
            continue()
        endif()
        get_filename_component(area "${area_dir}" NAME)
        add_test(NAME stdlib-${area} COMMAND ${CMAKE_COMMAND}
            "-DNEURA=${CMAKE_CURRENT_BINARY_DIR}/stdlib-install/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/stdlib-tests/${area}"
            "-DAREA=${area}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/area.cmake"
        )
        set_tests_properties(stdlib-${area} PROPERTIES FIXTURES_REQUIRED stdlib)
    endforeach()

    # Cost baseline of representative library workloads (tests/benchmarks).
    add_test(NAME benchmarks COMMAND ${CMAKE_COMMAND}
        "-DNEURA=${CMAKE_CURRENT_BINARY_DIR}/stdlib-install/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/benchmarks"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmarks/run.cmake"
    )
    set_tests_properties(benchmarks PROPERTIES FIXTURES_REQUIRED stdlib)

    add_test(NAME ir-optimization-cli COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/ir-optimization-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ir-optimization/cli.cmake"
    )
endif()
