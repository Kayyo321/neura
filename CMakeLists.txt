cmake_minimum_required(VERSION 3.20)

# Neura's static libraries name each other, so one can reach a link line more
# than once. Apple's linker warns about each repeat ("ignoring duplicate
# libraries"), and the release build fails on a warning. From CMake 3.29 this
# policy removes the repeats on the platforms whose linkers do not need them.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()

# The version of the tool and the standard library has one source: VERSION.
# stdlib/std/core/version.nra must agree, which the version-consistency test
# checks.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" NEURA_VERSION_CORE LIMIT_COUNT 1)
if(NOT NEURA_VERSION_CORE MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must hold MAJOR.MINOR.PATCH, not '${NEURA_VERSION_CORE}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

# A local build reports a "dev" prerelease. Only a release build passes an
# empty prerelease or a release candidate such as rc.1.
set(NEURA_VERSION_PRERELEASE "dev" CACHE STRING "Prerelease label of the version: empty for a release, else dev, rc.N, ...")
set(NEURA_BUILD_COMMIT "unknown" CACHE STRING "Commit that `neura --version` reports")
if(NOT NEURA_VERSION_PRERELEASE MATCHES "^[0-9A-Za-z.-]*$")
    message(FATAL_ERROR "NEURA_VERSION_PRERELEASE may hold only letters, digits, '.' and '-'")
endif()
if(NOT NEURA_BUILD_COMMIT MATCHES "^[0-9A-Za-z._-]+$")
    message(FATAL_ERROR "NEURA_BUILD_COMMIT may hold only letters, digits, '.', '_' and '-'")
endif()
if(NEURA_VERSION_PRERELEASE STREQUAL "")
    set(NEURA_VERSION_FULL "${NEURA_VERSION_CORE}")
else()
    set(NEURA_VERSION_FULL "${NEURA_VERSION_CORE}-${NEURA_VERSION_PRERELEASE}")
endif()

project(Neura VERSION ${NEURA_VERSION_CORE} LANGUAGES CXX C)

# The host target triple names launchers and release archives.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" neura_host_arch)
if(neura_host_arch MATCHES "^(x86_64|amd64)$")
    set(neura_host_arch x86_64)
elseif(neura_host_arch MATCHES "^(aarch64|arm64)$")
    set(neura_host_arch aarch64)
elseif(neura_host_arch MATCHES "^i[3-6]86$")
    set(neura_host_arch i386)
elseif(neura_host_arch MATCHES "^arm")
    set(neura_host_arch arm)
elseif(NOT neura_host_arch STREQUAL "riscv64")
    set(neura_host_arch unknown)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NEURA_HOST_TRIPLE "${neura_host_arch}-linux-gnu")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(NEURA_HOST_TRIPLE "${neura_host_arch}-apple-darwin")
elseif(WIN32)
    set(NEURA_HOST_TRIPLE "${neura_host_arch}-windows-msvc")
else()
    set(NEURA_HOST_TRIPLE "${neura_host_arch}-unknown")
endif()
configure_file(src/cli/version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/neura/version.hpp" @ONLY)

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
set(neura_nvm_sources
    src/nvm/driver.cpp
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
    src/nvm/schedule.cpp
    src/nvm/tier1.cpp
    src/nvm/trace.cpp
    src/nvm/trace_file.cpp
)
add_library(neura_nvm STATIC ${neura_nvm_sources})
target_link_libraries(neura_nvm PUBLIC neura_nvr neura_lift)
# Std.DynamicForeign calls native functions through libffi and loads
# libraries with the platform loader.
#
# A release archive must run on machines that have neither a C++ runtime as
# new as the build's nor libffi installed. With NEURA_PORTABLE_RELEASE the
# programs that link the NVM carry libffi, and neura carries the C++ runtime,
# as the default launcher always does. They then need only the C library and
# the loader from their host.
option(NEURA_PORTABLE_RELEASE "Link libffi and the C++ runtime into neura statically, for release archives" OFF)
if(NEURA_PORTABLE_RELEASE AND WIN32)
    message(FATAL_ERROR "NEURA_PORTABLE_RELEASE is not supported on Windows")
endif()
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFFI REQUIRED IMPORTED_TARGET libffi)
    if(NEURA_PORTABLE_RELEASE)
        find_library(NEURA_PORTABLE_LIBFFI NAMES libffi.a HINTS ${LIBFFI_LIBRARY_DIRS} REQUIRED)
        target_include_directories(neura_nvm PRIVATE ${LIBFFI_INCLUDE_DIRS})
        target_link_libraries(neura_nvm PRIVATE "${NEURA_PORTABLE_LIBFFI}" ${CMAKE_DL_LIBS})
    else()
        target_link_libraries(neura_nvm PRIVATE PkgConfig::LIBFFI ${CMAKE_DL_LIBS})
    endif()
endif()
neura_target_defaults(neura_nvm)

set(neura_runner_sources src/runner/runner.cpp src/launcher/container.cpp)
add_library(neura_runner STATIC ${neura_runner_sources})
target_link_libraries(neura_runner PUBLIC neura_nvm)
neura_target_defaults(neura_runner)

set(NEURA_STATIC_LAUNCHER "AUTO" CACHE STRING "Fully static Neura launcher: AUTO, ON, or OFF")
set_property(CACHE NEURA_STATIC_LAUNCHER PROPERTY STRINGS AUTO ON OFF)
if(NOT NEURA_STATIC_LAUNCHER MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "NEURA_STATIC_LAUNCHER must be AUTO, ON, or OFF")
endif()
set(NEURA_HAS_STATIC_LAUNCHER OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(neura_launcher_triple "${NEURA_HOST_TRIPLE}")
    set(neura_launcher_directory "lib/neura/launchers/${neura_launcher_triple}")
    find_library(NEURA_STATIC_LIBFFI NAMES libffi.a HINTS ${LIBFFI_LIBRARY_DIRS} REQUIRED)
    function(neura_add_launcher target name flavor)
        add_executable(${target} src/launcher/main.cpp)
        target_compile_definitions(${target} PRIVATE
            NEURA_LAUNCHER_TRIPLE="${neura_launcher_triple}" NEURA_LAUNCHER_FLAVOR=${flavor})
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_link_options(${target} PRIVATE -s)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${name}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${neura_launcher_directory}")
        neura_target_defaults(${target})
        install(TARGETS ${target} RUNTIME DESTINATION "${neura_launcher_directory}")
    endfunction()
    neura_add_launcher(neura_launcher neura-launcher 0)
    target_link_libraries(neura_launcher PRIVATE neura_runner neura_nvm "${NEURA_STATIC_LIBFFI}")
    target_link_options(neura_launcher PRIVATE -static-libstdc++ -static-libgcc)

    if(NOT NEURA_STATIC_LAUNCHER STREQUAL "OFF")
        find_library(NEURA_STATIC_LIBC NAMES libc.a)
        if(NEURA_STATIC_LIBC)
            set(NEURA_HAS_STATIC_LAUNCHER ON)
            add_library(neura_nvm_static STATIC ${neura_nvm_sources})
            target_link_libraries(neura_nvm_static PUBLIC neura_nvr neura_lift)
            # A static program cannot use the C library's resolver portably,
            # so this launcher takes numeric addresses and "localhost" only.
            target_compile_definitions(neura_nvm_static PRIVATE NEURA_NO_NATIVE_INTEROP NEURA_NUMERIC_ADDRESSES_ONLY)
            neura_target_defaults(neura_nvm_static)

            add_library(neura_runner_static STATIC ${neura_runner_sources})
            target_link_libraries(neura_runner_static PUBLIC neura_nvm_static)
            neura_target_defaults(neura_runner_static)

            neura_add_launcher(neura_launcher_static neura-launcher-static 1)
            target_link_libraries(neura_launcher_static PRIVATE neura_runner_static)
            target_link_options(neura_launcher_static PRIVATE -static)
        elseif(NEURA_STATIC_LAUNCHER STREQUAL "ON")
            message(FATAL_ERROR "NEURA_STATIC_LAUNCHER needs a static C library")
        endif()
    endif()
elseif(NEURA_STATIC_LAUNCHER STREQUAL "ON")
    message(FATAL_ERROR "NEURA_STATIC_LAUNCHER requires Linux")
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

# The data side of the install lifecycle (docs/Install.md): SHA-256, version
# precedence, and the marker and release metadata formats. It has no
# dependency on the rest of the tool, so the unit tests link only this.
add_library(neura_lifecycle STATIC
    src/cli/install_layout.cpp
    src/util/sha256.cpp
)
target_include_directories(neura_lifecycle PUBLIC src)
neura_target_defaults(neura_lifecycle)

# The neuron circuit library: gadgets for bits, integers, and IEEE floats,
# and block assembly. The converter emits graphs with it and the lift
# library regenerates them with it, so it depends only on the image format.
add_library(neura_circuit STATIC
    src/circuit/block_synth.cpp
    src/circuit/circuit.cpp
    src/circuit/float_circuit.cpp
)
target_link_libraries(neura_circuit PUBLIC neura_nvr)
neura_target_defaults(neura_circuit)

# Lifted blocks (docs/runtime/NVM.md, "Execution tiers"): the typed
# expressions behind a block's graph, the structural and overflow proofs that
# make them trustworthy, and their lowering to native 64-bit operations. It
# depends on the circuit library and the image format only, so the runtime
# can use it without compiler code. Float operations are lowered only where
# hardware arithmetic is bit-exact, which rules out contracted multiply-adds.
add_library(neura_lift STATIC
    src/lift/expression.cpp
    src/lift/hints.cpp
    src/lift/lift.cpp
    src/lift/lowered_block.cpp
    src/lift/overflow.cpp
    src/lift/proof.cpp
    src/lift/recognize.cpp
)
target_link_libraries(neura_lift PUBLIC neura_circuit)
if(NOT MSVC)
    target_compile_options(neura_lift PRIVATE -ffp-contract=off)
endif()
neura_target_defaults(neura_lift)

add_library(neura_compiler STATIC
    src/c-import/c_import.cpp
    src/cli/diagnostic_render.cpp
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
    src/neura-ir/task_analysis.cpp
    src/neura-ir/purity.cpp
    src/neura-ir/dispatch.cpp
    src/neura-ir/monitors.cpp
    src/neura-ir/kernel_signature.cpp
    src/neura-ir/verifier.cpp
    src/neura-ir/nir_format.cpp
    src/neura-ir/attributes.cpp
    src/neura-ir/tags.cpp
    src/neura-ir/linker.cpp
    src/neura-ir/reference_initialization.cpp
    src/neura-ir/definite_assignment.cpp
    src/neura-ir/reference_storage.cpp
    src/ir-optimization/analysis.cpp
    src/ir-optimization/effects.cpp
    src/ir-optimization/inlining.cpp
    src/ir-optimization/integer_folding.cpp
    src/ir-optimization/locals.cpp
    src/ir-optimization/optimizer.cpp
    src/ir-optimization/passes.cpp
    src/ir-optimization/value_passes.cpp
    src/backend/converter.cpp
    src/util/source_map.cpp
)
target_include_directories(neura_compiler PUBLIC src)
target_link_libraries(neura_compiler PUBLIC neura_circuit neura_lift neura_nvr PRIVATE neura_libclang)
neura_target_defaults(neura_compiler)

# The decompiler reads images through the shared loader and lifts their
# graphs with the lift library, which regenerates them with the converter's
# circuit library.
add_library(neura_decompiler STATIC
    src/decompiler/assembly.cpp
    src/decompiler/decompile.cpp
    src/decompiler/lifter.cpp
)
target_link_libraries(neura_decompiler PUBLIC neura_lift neura_compiler neura_nvm)
neura_target_defaults(neura_decompiler)

# `neura explain` prints the pages in docs/diagnostics, embedded here so that
# the binary needs no documentation files at run time.
file(GLOB neura_diagnostic_pages CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/docs/diagnostics/E*.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/diagnostics/W*.md")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${neura_diagnostic_pages})
set(neura_diagnostic_pages_inc "")
foreach(page IN LISTS neura_diagnostic_pages)
    get_filename_component(code "${page}" NAME_WE)
    file(READ "${page}" text)
    string(APPEND neura_diagnostic_pages_inc "{\"${code}\", R\"neura_page(${text})neura_page\"},\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/neura/diagnostic_pages.inc"
    CONTENT "${neura_diagnostic_pages_inc}" @ONLY)

add_executable(neura src/cli/main.cpp src/cli/argument_lists.cpp src/cli/explain.cpp src/cli/gpu_run.cpp src/cli/lifecycle.cpp
    src/launcher/descriptor.cpp)
target_link_libraries(neura PRIVATE neura_compiler neura_runner neura_decompiler neura_lifecycle)
if(NEURA_PORTABLE_RELEASE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(neura PRIVATE -static-libstdc++ -static-libgcc)
endif()
target_include_directories(neura PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(TARGET neura_launcher)
    add_dependencies(neura neura_launcher)
endif()
if(TARGET neura_launcher_static)
    add_dependencies(neura neura_launcher_static)
endif()
if(NEURA_HAS_GPU)
    target_link_libraries(neura PRIVATE neura_gpu)
    target_compile_definitions(neura PRIVATE NEURA_HAS_GPU=1)
endif()
neura_target_defaults(neura)

# The compiler and the runner ship as one executable: `neura run` loads
# images through the same NVM library as any other embedder.
install(TARGETS neura RUNTIME DESTINATION bin)
install(FILES LICENSE DESTINATION share/doc/neura)
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

# --- Kernels (docs/runtime/Kernels.md) ---------------------------------------
# Deterministic, pure C functions that the runtime will call in place of
# standard library code. They call no libm function (scripts/check-kernels.sh
# checks the archive), so they add nothing to the release binaries' libc,
# libm, and loader dependencies. Floating point keeps strict binary64
# semantics: no contraction into fused multiply-add, no fast-math, and no
# host-specific instruction selection. The image format links it, so the
# loader checks an image's KERN table against the kernel table and the NVM
# (neura_nvm and neura_nvm_static, hence every launcher) runs kernel exits;
# the compiler checks kernel bindings against the same table.
add_library(neura_kernels STATIC
    src/kernels/neura_digest.c
    src/kernels/neura_encoding.c
    src/kernels/neura_hash.c
    src/kernels/neura_integer_text.c
    src/kernels/neura_kernel.c
    src/kernels/neura_math.c
    src/kernels/neura_sort.c
    src/kernels/neura_text.c
)
target_include_directories(neura_kernels PUBLIC src/kernels)
set_target_properties(neura_kernels PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(neura_kernels PRIVATE /W4 /WX /fp:strict)
else()
    target_compile_options(neura_kernels PRIVATE
        -ffp-contract=off -fno-math-errno
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
target_link_libraries(neura_nvr PUBLIC neura_kernels)
if(BUILD_TESTING)
    # Published vectors and boundary cases for each kernel family, plus the
    # math vectors of tests/stdlib/math/support/math-vectors.nra.
    add_executable(neura_kernel_tests tests/kernels/kernel_tests.cpp)
    target_link_libraries(neura_kernel_tests PRIVATE neura_kernels)
    neura_target_defaults(neura_kernel_tests)
    foreach(suite IN ITEMS abi digest encoding text integer hash sort math table)
        add_test(NAME kernels-${suite}
            COMMAND neura_kernel_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
endif()
# --- End of kernels -----------------------------------------------------------

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

    # Scoped tasks (docs/lang/Concurrency.md): conformance of the forms and
    # their checks, then compiled programs through every build route.
    add_test(NAME concurrency-frontend COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/concurrency-frontend-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/concurrency/frontend.cmake"
    )
    add_test(NAME concurrency-programs COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/concurrency-programs-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/concurrency/programs.cmake"
    )
    # Overlapping host I/O on the loopback interface and the real clock, with
    # concurrent record and replay (Concurrency.md sections 10.4 and 11).
    add_test(NAME concurrency-io COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/concurrency-io-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/concurrency/io.cmake"
    )

    # Virtual time, schedule exploration, and saved schedules (Concurrency.md section 14.4).
    add_test(NAME concurrency-schedules COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/concurrency-schedules-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/concurrency/schedules.cmake"
    )

    # Monitors: #Synchronized methods on shared instances (Concurrency.md section 8.1).
    add_test(NAME concurrency-monitors COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/concurrency-monitors-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/concurrency/monitors.cmake"
    )

    add_test(NAME concurrency-parallel COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/concurrency-parallel-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/concurrency/parallel.cmake"
    )

    add_test(NAME source-docs COMMAND "${CMAKE_COMMAND}"
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/source-docs-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source-docs.cmake"
    )
    # Every Neura example in the documentation is compiled or run.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME doc-examples COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-examples.py"
        --neura "$<TARGET_FILE:neura>" --root "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME nir-link COMMAND "${CMAKE_COMMAND}"
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/nir-link-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/nir-link.cmake"
    )

    # Diagnostics: the core's unit tests, and one UI test per area under
    # tests/ui that compares rendered errors and applied fixes.
    add_executable(neura_diagnostic_tests tests/diagnostics/unit_tests.cpp)
    target_link_libraries(neura_diagnostic_tests PRIVATE neura_compiler)
    neura_target_defaults(neura_diagnostic_tests)
    foreach(suite IN ITEMS sink source-map render width edits)
        add_test(NAME diagnostics-unit-${suite} COMMAND neura_diagnostic_tests ${suite})
    endforeach()
    file(GLOB neura_ui_areas CONFIGURE_DEPENDS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/*")
    foreach(area_dir IN LISTS neura_ui_areas)
        if(IS_DIRECTORY "${area_dir}")
            get_filename_component(area "${area_dir}" NAME)
            add_test(NAME ui-${area} COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ui-test.py" --neura "$<TARGET_FILE:neura>" "${area_dir}")
        endif()
    endforeach()

    add_executable(neura_ir_tests
        tests/ir-optimization/behavior_tests.cpp
        tests/ir-optimization/concurrency_support.cpp
        tests/ir-optimization/concurrency_tests.cpp
        tests/ir-optimization/evaluator.cpp
        tests/ir-optimization/float_reference.cpp
        tests/ir-optimization/main.cpp
        tests/ir-optimization/pass_tests.cpp
        tests/ir-optimization/structure_tests.cpp
        tests/ir-optimization/support.cpp
        tests/ir-optimization/verifier_tests.cpp
    )
    target_link_libraries(neura_ir_tests PRIVATE neura_compiler)
    target_include_directories(neura_ir_tests PRIVATE tests)
    neura_target_defaults(neura_ir_tests)

    # Each suite is a separate test, so a failure names the kind of evidence
    # that broke.
    foreach(suite IN ITEMS structure verifier behavior concurrency)
        add_test(NAME ir-optimization-${suite}
            COMMAND neura_ir_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()

    add_executable(neura_backend_tests
        tests/backend/allocation_fault.cpp
        tests/backend/assembler.cpp
        tests/backend/circuit_tests.cpp
        tests/backend/concurrency_evidence_tests.cpp
        tests/backend/concurrency_io_tests.cpp
        tests/backend/concurrency_tests.cpp
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
        tests/backend/tier_tests.cpp
        tests/backend/vm_tests.cpp
        tests/ir-optimization/concurrency_support.cpp
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

    foreach(suite IN ITEMS circuits floats format vm host nir conversion generics layered fleet placement concurrency tier)
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

    # Lifted blocks (src/lift): the structural and overflow proofs, and the
    # lowered blocks against the reference evaluator over a program corpus.
    add_executable(neura_lift_tests
        tests/lift/lift_tests.cpp
        tests/ir-optimization/main.cpp
        tests/ir-optimization/support.cpp
    )
    target_link_libraries(neura_lift_tests PRIVATE neura_lift neura_compiler neura_nvm)
    target_include_directories(neura_lift_tests PRIVATE tests)
    neura_target_defaults(neura_lift_tests)
    foreach(suite IN ITEMS proof overflow execute corpus)
        add_test(NAME lift-${suite} COMMAND neura_lift_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
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
    # The tests name the shared library libmathops.so. dlopen ignores a file's
    # suffix, so on macOS the Mach-O dynamic library keeps that name instead of
    # CMake's libmathops.dylib.
    set_target_properties(neura_test_mathops_shared PROPERTIES SUFFIX ".so")
    set(neura_native_test_dir "${CMAKE_CURRENT_BINARY_DIR}/native-tests")
    add_test(NAME backend-capabilities COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/backend-capabilities-tests"
        "-DMATHOPS=$<TARGET_FILE:neura_test_mathops_shared>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/capabilities.cmake")
    if(TARGET neura_launcher)
        add_test(NAME native-policy COMMAND ${CMAKE_COMMAND}
            "-DNEURA=$<TARGET_FILE:neura>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-executable-policy"
            "-DMATHOPS=$<TARGET_FILE:neura_test_mathops_shared>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/policy.cmake")
        add_test(NAME native-install COMMAND ${CMAKE_COMMAND}
            "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-executable-install"
            "-DCONFIG=$<CONFIG>" "-DTRIPLE=${neura_launcher_triple}"
            "-DHAS_STATIC=${NEURA_HAS_STATIC_LAUNCHER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/install-executable.cmake")
        add_test(NAME native-c-import COMMAND ${CMAKE_COMMAND}
            "-DNEURA=$<TARGET_FILE:neura>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-executable-c-import"
            "-DMATHOPS=$<TARGET_FILE:neura_test_mathops_static>" "-DTRIPLE=${neura_launcher_triple}"
            "-DHAS_STATIC=${NEURA_HAS_STATIC_LAUNCHER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/c-import-executable.cmake")
    else()
        message(STATUS "No Neura launcher for ${CMAKE_SYSTEM_NAME}: native executable tests are not registered "
            "(native-build, native-run, native-container, native-policy, native-install, native-c-import); "
            "native-replay skips its launcher comparisons")
    endif()

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

    set(native_replay_options)
    if(TARGET neura_launcher)
        list(APPEND native_replay_options "-DNATIVE_TOOLCHAIN=$<TARGET_FILE:neura>")
    endif()
    add_test(NAME native-replay COMMAND ${CMAKE_COMMAND}
        "-DNEURA=${CMAKE_CURRENT_BINARY_DIR}/stdlib-install/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-replay"
        "-DNATIVE_DIR=${neura_native_test_dir}"
        ${native_replay_options}
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

    # The install lifecycle (docs/Install.md): version source, the data
    # formats, and the commands against an install made from a release
    # archive. None needs the network, root, or a terminal.
    add_executable(neura_lifecycle_tests tests/lifecycle/unit_tests.cpp)
    target_link_libraries(neura_lifecycle_tests PRIVATE neura_lifecycle)
    neura_target_defaults(neura_lifecycle_tests)
    foreach(suite IN ITEMS sha256 semver release-metadata marker)
        add_test(NAME lifecycle-unit-${suite} COMMAND neura_lifecycle_tests ${suite})
    endforeach()
    add_test(NAME version-consistency COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle/version.cmake"
    )
    foreach(suite IN ITEMS install update uninstall bootstrap)
        add_test(NAME lifecycle-${suite} COMMAND ${CMAKE_COMMAND}
            "-DNEURA=$<TARGET_FILE:neura>"
            "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/lifecycle-${suite}-tests"
            "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle/${suite}.cmake"
        )
        set_tests_properties(lifecycle-${suite} PROPERTIES TIMEOUT 600)
    endforeach()

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
