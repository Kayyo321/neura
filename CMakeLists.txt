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
    src/nvm/ffi_provider.cpp
    src/nvm/fleet.cpp
    src/nvm/fs_provider.cpp
    src/nvm/graph_device.cpp
    src/nvm/host.cpp
    src/nvm/layered.cpp
    src/nvm/machine.cpp
    src/nvm/net_provider.cpp
    src/nvm/placement.cpp
    src/nvm/providers.cpp
    src/nvm/scalar_text.cpp
    src/nvm/trace.cpp
    src/nvm/trace_file.cpp
)
target_link_libraries(neura_nvm PUBLIC neura_nvr)
# Std.DynamicForeign calls native functions through libffi and loads
# libraries with the platform loader.
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFFI REQUIRED IMPORTED_TARGET libffi)
    target_link_libraries(neura_nvm PRIVATE PkgConfig::LIBFFI ${CMAKE_DL_LIBS})
endif()
neura_target_defaults(neura_nvm)

add_library(neura_runner STATIC src/runner/runner.cpp src/launcher/container.cpp)
target_link_libraries(neura_runner PUBLIC neura_nvm)
neura_target_defaults(neura_runner)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" neura_launcher_arch)
    if(neura_launcher_arch MATCHES "^(x86_64|amd64)$")
        set(neura_launcher_arch x86_64)
    elseif(neura_launcher_arch MATCHES "^(aarch64|arm64)$")
        set(neura_launcher_arch aarch64)
    elseif(neura_launcher_arch MATCHES "^i[3-6]86$")
        set(neura_launcher_arch i386)
    elseif(neura_launcher_arch MATCHES "^arm")
        set(neura_launcher_arch arm)
    elseif(NOT neura_launcher_arch STREQUAL "riscv64")
        set(neura_launcher_arch unknown)
    endif()
    set(neura_launcher_triple "${neura_launcher_arch}-linux-gnu")
    set(neura_launcher_directory "lib/neura/launchers/${neura_launcher_triple}")
    find_library(NEURA_STATIC_LIBFFI NAMES libffi.a HINTS ${LIBFFI_LIBRARY_DIRS} REQUIRED)
    add_executable(neura_launcher src/launcher/main.cpp)
    target_compile_definitions(neura_launcher PRIVATE NEURA_LAUNCHER_TRIPLE="${neura_launcher_triple}")
    target_link_libraries(neura_launcher PRIVATE neura_runner neura_nvm "${NEURA_STATIC_LIBFFI}")
    target_link_options(neura_launcher PRIVATE -static-libstdc++ -static-libgcc -s)
    set_target_properties(neura_launcher PROPERTIES OUTPUT_NAME neura-launcher
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${neura_launcher_directory}")
    neura_target_defaults(neura_launcher)
endif()

set(NEURA_GPU "AUTO" CACHE STRING "GPU graph execution: AUTO, ON, or OFF")
set_property(CACHE NEURA_GPU PROPERTY STRINGS AUTO ON OFF)
if(NOT NEURA_GPU MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "NEURA_GPU must be AUTO, ON, or OFF")
endif()
set(NEURA_HAS_GPU OFF)
if(NEURA_GPU STREQUAL "AUTO" AND APPLE)
    message(STATUS "GPU graph execution disabled: Apple platforms are outside the Vulkan backend")
elseif(NOT NEURA_GPU STREQUAL "OFF")
    find_path(NEURA_VULKAN_INCLUDE_DIR vulkan/vulkan.h)
    find_program(NEURA_GLSLANG glslangValidator)
    if(NEURA_VULKAN_INCLUDE_DIR AND NEURA_GLSLANG)
        set(NEURA_HAS_GPU ON)
    else()
        set(missing)
        if(NOT NEURA_VULKAN_INCLUDE_DIR)
            list(APPEND missing "Vulkan headers")
        endif()
        if(NOT NEURA_GLSLANG)
            list(APPEND missing "glslangValidator")
        endif()
        list(JOIN missing ", " missing)
        if(NEURA_GPU STREQUAL "ON")
            message(FATAL_ERROR "GPU graph execution needs ${missing}")
        endif()
        message(STATUS "GPU graph execution disabled: missing ${missing}")
    endif()
endif()

if(NEURA_HAS_GPU)
    set(graph_shader "${CMAKE_CURRENT_BINARY_DIR}/generated/graph_eval.spv.inc")
    add_custom_command(OUTPUT "${graph_shader}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND "${NEURA_GLSLANG}" -V --target-env vulkan1.0 -x -o "${graph_shader}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/graph_eval.comp"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/graph_eval.comp"
        VERBATIM)
    add_library(neura_gpu STATIC
        src/gpu/calibrate.cpp
        src/gpu/context.cpp
        src/gpu/device.cpp
        src/gpu/vulkan_api.cpp
        "${graph_shader}"
    )
    target_link_libraries(neura_gpu PUBLIC neura_nvm PRIVATE ${CMAKE_DL_LIBS})
    target_include_directories(neura_gpu PRIVATE "${NEURA_VULKAN_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(neura_gpu PRIVATE VK_NO_PROTOTYPES)
    neura_target_defaults(neura_gpu)
endif()

add_library(neura_compiler STATIC
    src/c-import/c_import.cpp
    src/cli/source_loader.cpp
    src/cli/source_docs.cpp
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
    src/neura-ir/linker.cpp
    src/neura-ir/reference_initialization.cpp
    src/neura-ir/reference_storage.cpp
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
target_link_libraries(neura_compiler PUBLIC neura_nvr PRIVATE neura_libclang)
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

add_executable(neura src/cli/main.cpp src/cli/argument_lists.cpp src/cli/gpu_run.cpp src/launcher/descriptor.cpp)
target_link_libraries(neura PRIVATE neura_compiler neura_runner neura_decompiler)
if(TARGET neura_launcher)
    add_dependencies(neura neura_launcher)
endif()
if(NEURA_HAS_GPU)
    target_link_libraries(neura PRIVATE neura_gpu)
    target_compile_definitions(neura PRIVATE NEURA_HAS_GPU=1)
endif()
neura_target_defaults(neura)

# The compiler and the runner ship as one executable: `neura run` loads
# images through the same NVM library as any other embedder.
install(TARGETS neura RUNTIME DESTINATION bin)
install(DIRECTORY stdlib/ DESTINATION share/neura/stdlib OPTIONAL
    FILES_MATCHING PATTERN "*.nra")

include(CTest)

# The libclang C API headers and a loader that opens the system libclang at
# run time, for C header imports. Building Neura never links libclang.
set(NEURA_LIBCLANG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/neura-libclang")
if(NOT EXISTS "${NEURA_LIBCLANG_DIR}/include/clang-c/Index.h")
    message(FATAL_ERROR
        "The neura-libclang submodule is missing: "
        "${NEURA_LIBCLANG_DIR}/include/clang-c/Index.h does not exist.\n"
        "Fetch it from the repository root with:\n"
        "  git submodule sync --recursive\n"
        "  git submodule update --init --recursive\n"
        "Add --depth 1 to the update for a shallow checkout.")
endif()
set(NEURA_LIBCLANG_TESTS ${BUILD_TESTING})
add_subdirectory(third_party/neura-libclang)

if(BUILD_TESTING)
    if(TARGET neura_launcher)
        add_executable(neura_native_container_tests tests/native/container_cases.cpp)
        neura_target_defaults(neura_native_container_tests)
        foreach(suite IN ITEMS build run container)
            add_test(NAME native-${suite} COMMAND ${CMAKE_COMMAND}
                "-DNEURA=$<TARGET_FILE:neura>"
                "-DLAUNCHER=$<TARGET_FILE:neura_launcher>"
                "-DCONTAINER_TESTS=$<TARGET_FILE:neura_native_container_tests>"
                "-DSTRIP=${CMAKE_STRIP}"
                "-DSUITE=${suite}"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-executable-${suite}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/executable.cmake")
        endforeach()
    endif()

    add_test(NAME frontend COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/frontend-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend.cmake"
    )

    add_test(NAME source-docs COMMAND "${CMAKE_COMMAND}"
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/source-docs-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source-docs.cmake"
    )
    add_test(NAME nir-link COMMAND "${CMAKE_COMMAND}"
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/nir-link-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/nir-link.cmake"
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
        tests/backend/fleet_tests.cpp
        tests/backend/fleet_benchmark.cpp
        tests/backend/fixtures.cpp
    tests/backend/float_tests.cpp
        tests/backend/format_tests.cpp
    tests/backend/generics_tests.cpp
        tests/backend/host_tests.cpp
        tests/backend/layered_tests.cpp
        tests/backend/nir_tests.cpp
        tests/backend/placement_tests.cpp
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

    if(NEURA_HAS_GPU)
        target_sources(neura_backend_tests PRIVATE tests/backend/gpu_tests.cpp)
        target_link_libraries(neura_backend_tests PRIVATE neura_gpu)
        target_compile_definitions(neura_backend_tests PRIVATE NEURA_HAS_GPU=1)
        add_executable(neura_gpu_cli_test tests/backend/gpu_cli_main.cpp)
        target_link_libraries(neura_gpu_cli_test PRIVATE neura_gpu)
        neura_target_defaults(neura_gpu_cli_test)
        set(gpu_cli_test_options)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_library(neura_test_vulkan_loader SHARED tests/backend/vulkan_fault_loader.cpp)
            set_target_properties(neura_test_vulkan_loader PROPERTIES
                OUTPUT_NAME vulkan SOVERSION 1
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gpu-fault-loader")
            target_include_directories(neura_test_vulkan_loader PRIVATE "${NEURA_VULKAN_INCLUDE_DIR}")
            target_compile_definitions(neura_test_vulkan_loader PRIVATE VK_NO_PROTOTYPES=1)
            target_link_libraries(neura_test_vulkan_loader PRIVATE ${CMAKE_DL_LIBS})
            neura_target_defaults(neura_test_vulkan_loader)
            add_dependencies(neura_gpu_cli_test neura_test_vulkan_loader)
            target_compile_definitions(neura_gpu_cli_test PRIVATE NEURA_TEST_VULKAN_FAULTS=1)
            list(APPEND gpu_cli_test_options
                "-DFAULT_LOADER_DIR=$<TARGET_FILE_DIR:neura_test_vulkan_loader>"
                "-DBACKEND_TESTS=$<TARGET_FILE:neura_backend_tests>")
        endif()
        add_test(NAME gpu-device COMMAND neura_backend_tests gpu_device "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME gpu-cli COMMAND neura_gpu_cli_test "${CMAKE_COMMAND}"
            "-DNEURA=$<TARGET_FILE:neura>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/gpu-cli-tests"
            ${gpu_cli_test_options}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/gpu-cli.cmake")
        set_tests_properties(gpu-device gpu-cli PROPERTIES SKIP_RETURN_CODE 77 LABELS gpu TIMEOUT 1800)
    endif()

    foreach(suite IN ITEMS circuits floats format vm host nir conversion generics layered fleet placement)
        add_test(NAME backend-${suite}
            COMMAND neura_backend_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()

    add_test(NAME backend-each COMMAND "${CMAKE_COMMAND}"
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DNEURA_HAS_GPU=${NEURA_HAS_GPU}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/backend-each-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/each.cmake"
    )

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

    # A small C library for the foreign-interop tests, built as a shared
    # library for Std.DynamicForeign and as a position-independent static
    # archive for C imports.
    enable_language(C)
    foreach(kind IN ITEMS SHARED STATIC)
        string(TOLOWER "${kind}" suffix)
        add_library(neura_test_mathops_${suffix} ${kind} tests/native/mathops.c)
        set_target_properties(neura_test_mathops_${suffix} PROPERTIES
            OUTPUT_NAME mathops
            POSITION_INDEPENDENT_CODE ON
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-tests"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-tests"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-tests")
    endforeach()
    set(neura_native_test_dir "${CMAKE_CURRENT_BINARY_DIR}/native-tests")

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
            "-DNATIVE_TEST_DIR=${neura_native_test_dir}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/area.cmake"
        )
        set_tests_properties(stdlib-${area} PROPERTIES FIXTURES_REQUIRED stdlib)
    endforeach()

    add_test(NAME c-import COMMAND ${CMAKE_COMMAND}
        "-DNEURA=${CMAKE_CURRENT_BINARY_DIR}/stdlib-install/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/c-import-tests"
        "-DNATIVE_DIR=${neura_native_test_dir}"
        "-DC_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_AR=${CMAKE_AR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/c-import/c-import.cmake"
    )
    set_tests_properties(c-import PROPERTIES FIXTURES_REQUIRED stdlib)

    add_test(NAME native-replay COMMAND ${CMAKE_COMMAND}
        "-DNEURA=${CMAKE_CURRENT_BINARY_DIR}/stdlib-install/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-replay"
        "-DNATIVE_DIR=${neura_native_test_dir}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/replay.cmake"
    )
    set_tests_properties(native-replay PROPERTIES FIXTURES_REQUIRED stdlib)

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

    get_property(neura_tests DIRECTORY PROPERTY TESTS)
    list(REMOVE_ITEM neura_tests gpu-device gpu-cli)
    foreach(test IN LISTS neura_tests)
        set_property(TEST "${test}" APPEND PROPERTY ENVIRONMENT "NEURA_GPU=off")
    endforeach()
    # The submodule tests never invoke Neura. CMake 3.28 also allows
    # applying the same environment to tests declared in its directory.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
        get_property(libclang_tests DIRECTORY "${NEURA_LIBCLANG_DIR}" PROPERTY TESTS)
        foreach(test IN LISTS libclang_tests)
            set_property(TEST "${test}" DIRECTORY "${NEURA_LIBCLANG_DIR}"
                APPEND PROPERTY ENVIRONMENT "NEURA_GPU=off")
        endforeach()
    endif()
endif()
