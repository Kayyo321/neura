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
if(WIN32)
    # The native Windows build: toolchain, settings, product layout and the
    # hook points of the gated areas (docs/design/Windows030.md).
    include(cmake/NeuraWindows.cmake)
endif()

option(NEURA_WASM_RUNTIME "Build only the browser's wasm32 runtime" OFF)
if(NEURA_WASM_RUNTIME)
    if(NOT EMSCRIPTEN OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "NEURA_WASM_RUNTIME requires the pinned wasm32 Emscripten toolchain")
    endif()
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    get_filename_component(neura_emscripten_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(neura_emsdk_dir "${neura_emscripten_dir}/../.." ABSOLUTE)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-emsdk.py" "${neura_emsdk_dir}"
        COMMAND_ERROR_IS_FATAL ANY)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions> -ffp-contract=off)
endif()

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

# Under AddressSanitizer the NVM heap (src/nvm/heap.hpp) takes every entry
# from operator new, so that the sanitizer checks each one. This option
# keeps its size-class pool instead, to check the pool's own code; it has no
# effect in a build without the sanitizer. Every target that includes the
# heap must see the same setting, so it applies to the whole build.
option(NEURA_HEAP_POOL_UNDER_SANITIZERS "Keep the NVM heap's size-class pool under AddressSanitizer" OFF)
if(NEURA_HEAP_POOL_UNDER_SANITIZERS)
    add_compile_definitions(NEURA_HEAP_POOL_UNDER_SANITIZERS=1)
endif()

# The SDK: the compiler, the decompiler, the editor tools, the standard
# library and the native launchers (docs/design/Products030.md). Without it
# the build defines only the execution core, the `core` install component,
# so nothing in the core can depend on compiler code, and libclang's loader
# is not built.
option(NEURA_SDK "Build the SDK tools as well as the execution core" ON)

# Every library is position independent, so that its objects can go into the
# shared libraries of the tools. Calls within a translation unit stay direct
# and inlinable (no semantic interposition), and thread-local variables use
# the initial-exec model, valid because the shared libraries are always
# loaded at program start, never with dlopen. The browser runtime (Emscripten)
# links statically and takes neither.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT EMSCRIPTEN AND NOT MSVC AND (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR (CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT APPLE)))
    add_compile_options(-fno-semantic-interposition -ftls-model=initial-exec)
endif()
# The server host and its tests use std::jthread and std::stop_token, which
# the libc++ of Xcode 16 still marks experimental. -fexperimental-library
# enables them there and links libc++'s static libc++experimental.a, so the
# tools need no other system library.
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-std=c++20")
    check_cxx_source_compiles("#include <thread>\nint main() { std::jthread t([] {}); return 0; }" NEURA_HAS_JTHREAD)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT NEURA_HAS_JTHREAD)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>)
        add_link_options($<$<LINK_LANGUAGE:CXX>:-fexperimental-library>)
    endif()
endif()

function(neura_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        # Not for the resource compiler, which takes no such options.
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${NEURA_WINDOWS_WARNINGS}>")
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(WIN32)
        neura_windows_embed_resources(${target})
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
else()
    # cl does not contract under /fp:precise; clang-cl does unless told not to.
    target_compile_options(neura_lift PRIVATE /fp:precise $<$<CXX_COMPILER_ID:Clang>:/clang:-ffp-contract=off>)
endif()
neura_target_defaults(neura_lift)

if(NEURA_WASM_RUNTIME)
    add_library(neura_nvm STATIC
        src/nvm/host.cpp
        src/nvm/layered.cpp
        src/nvm/machine.cpp
        src/nvm/scalar_text.cpp
        src/nvm/tier1.cpp
        src/nvm/tier2.cpp)
    target_compile_definitions(neura_nvm PUBLIC NEURA_WASM_RUNTIME)
    target_link_libraries(neura_nvm PUBLIC neura_nvr neura_lift)
    target_link_options(neura_nvm INTERFACE -fwasm-exceptions -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0)
    neura_target_defaults(neura_nvm)

    option(NEURA_WASM_HOST "Link the portable browser NVM and its ES module loader" OFF)
    if(NEURA_WASM_HOST)
        add_executable(neura_wasm src/wasm/entry.cpp)
        set_target_properties(neura_wasm PROPERTIES OUTPUT_NAME nvm SUFFIX .mjs)
        target_link_libraries(neura_wasm PRIVATE neura_nvm)
        set(neura_wasm_exports
            _malloc _free _neura_wasm_interface _neura_wasm_open _neura_wasm_close _neura_wasm_error
            _neura_wasm_advance _neura_wasm_respond _neura_wasm_deliver _neura_wasm_clock
            _neura_wasm_has_proof _neura_wasm_prove_idle _neura_wasm_last_proof _neura_wasm_tier_stats)
        list(JOIN neura_wasm_exports "','" neura_wasm_exports)
        target_link_options(neura_wasm PRIVATE
            --no-entry -sENVIRONMENT=web,node -sSTACK_SIZE=1048576 -sDYNAMIC_EXECUTION=0
            -sMODULARIZE=1 -sEXPORT_ES6=1
            "-sEXPORTED_FUNCTIONS=['${neura_wasm_exports}']"
            "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','UTF8ToString']")
        neura_target_defaults(neura_wasm)
    endif()

    option(NEURA_WASM_SMOKE "Link the portable runtime into a Node smoke fixture" OFF)
    if(NEURA_WASM_SMOKE)
        add_executable(neura_wasm_smoke tests/wasm/smoke.cpp)
        target_link_libraries(neura_wasm_smoke PRIVATE neura_nvm)
        target_link_options(neura_wasm_smoke PRIVATE
            --no-entry -sENVIRONMENT=node -sSTACK_SIZE=1048576 -sMODULARIZE=1
            "-sEXPORTED_FUNCTIONS=['_malloc','_free','_neura_smoke']"
            "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']")
        neura_target_defaults(neura_wasm_smoke)
    endif()
    return()
endif()

# The platform layer (src/platform, namespace neura::platform): text and path
# conversion, processes, files, consoles, sockets, dynamic libraries, the
# environment and well-known directories, with a POSIX and a Windows
# implementation behind one interface. The runtime, the runner, every tool's
# start and the dispatcher use it.
file(GLOB neura_platform_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp")
add_library(neura_platform STATIC ${neura_platform_sources})
target_include_directories(neura_platform PUBLIC src)
if(WIN32)
    target_link_libraries(neura_platform PUBLIC ws2_32 mswsock bcrypt advapi32 shell32 ole32 wer psapi)
else()
    target_link_libraries(neura_platform PUBLIC ${CMAKE_DL_LIBS})
endif()
neura_target_defaults(neura_platform)

# The Neura Virtual Machine. It depends only on the image format, so a
# runtime-only distribution needs no compiler code.
set(neura_nvm_sources
    src/nvm/driver.cpp
    src/nvm/environment.cpp
    src/nvm/app_provider.cpp
    src/nvm/sql_services.cpp
    src/nvm/secret_services.cpp
    src/nvm/ffi_provider.cpp
    src/nvm/fleet.cpp
    src/nvm/fs_provider.cpp
    src/nvm/graph_device.cpp
    src/nvm/host.cpp
    src/nvm/layered.cpp
    src/nvm/machine.cpp
    src/nvm/net_provider.cpp
    src/nvm/pending_requests.cpp
    src/nvm/placement.cpp
    src/nvm/providers.cpp
    src/nvm/scalar_text.cpp
    src/nvm/socket_provider.cpp
    src/nvm/doq_provider.cpp
    src/nvm/tls_provider.cpp
    src/nvm/schedule.cpp
    src/nvm/tier1.cpp
    src/nvm/tier2.cpp
    src/nvm/tier3_build.cpp
    src/nvm/tier3_float.cpp
    src/nvm/tier3_inline.cpp
    src/nvm/tier3_interp.cpp
    src/nvm/tier3_licm.cpp
    src/nvm/tier3_passes.cpp
    src/nvm/tier3_range.cpp
    src/nvm/tier3_verify.cpp
    src/nvm/trace.cpp
    src/nvm/trace_file.cpp
    # The native code generator of tier 2 (docs/runtime/NVM.md, "Native
    # compilation"): part of the runtime, with no dependency of its own.
    src/jit/aarch64.cpp
    src/jit/compiler.cpp
    src/jit/jit.cpp
    src/jit/mir_check.cpp
    src/jit/regalloc.cpp
    src/jit/tier3_emit.cpp
    src/jit/tier3_lower.cpp
    src/jit/x64.cpp
    src/jit/x86_64.cpp
)
add_library(neura_nvm STATIC ${neura_nvm_sources})
target_link_libraries(neura_nvm PUBLIC neura_nvr neura_lift neura_platform)
if(MSVC)
    # Float semantics stated, as for neura_lift: tier 3 folds and evaluates
    # floats, and its results must match the other tiers bit for bit.
    target_compile_options(neura_nvm PRIVATE /fp:precise $<$<CXX_COMPILER_ID:Clang>:/clang:-ffp-contract=off>)
endif()
# Std.DynamicForeign calls native functions through libffi and loads
# libraries with the platform loader.
#
# A release archive must run on machines that have neither a C++ runtime as
# new as the build's nor libffi installed. With NEURA_PORTABLE_RELEASE the
# programs that link the NVM carry libffi, and neura carries the C++ runtime,
# as the default launcher always does. They then need only the C library and
# the loader from their host.
option(NEURA_PORTABLE_RELEASE "Link libffi and the C++ runtime into neura statically, for release archives" OFF)
# On Windows every binary has the static C runtime in any build (W05); a
# release build also refuses a libffi DLL and is reproducible
# (cmake/NeuraWindows.cmake).
if(WIN32)
    neura_windows_link_libffi(neura_nvm)
else()
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
if(WIN32)
    # neura.entropy.fill uses the system generator, BCryptGenRandom.
    target_link_libraries(neura_nvm PRIVATE bcrypt)
endif()

set(neura_runner_sources src/runner/runner.cpp src/launcher/container.cpp)
add_library(neura_runner STATIC ${neura_runner_sources})
target_link_libraries(neura_runner PUBLIC neura_nvm neura_platform)
neura_target_defaults(neura_runner)

set(NEURA_STATIC_LAUNCHER "AUTO" CACHE STRING "Fully static Neura launcher: AUTO, ON, or OFF")
set_property(CACHE NEURA_STATIC_LAUNCHER PROPERTY STRINGS AUTO ON OFF)
if(NOT NEURA_STATIC_LAUNCHER MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "NEURA_STATIC_LAUNCHER must be AUTO, ON, or OFF")
endif()
set(NEURA_HAS_STATIC_LAUNCHER OFF)
if(NOT NEURA_SDK)
    # The launchers belong to the SDK's native packaging.
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
        install(TARGETS ${target} RUNTIME DESTINATION "${neura_launcher_directory}" COMPONENT sdk)
    endfunction()
    neura_add_launcher(neura_launcher neura-launcher 0)
    target_link_libraries(neura_launcher PRIVATE neura_runner neura_nvm "${NEURA_STATIC_LIBFFI}")
    target_link_options(neura_launcher PRIVATE -static-libstdc++ -static-libgcc)

    if(NOT NEURA_STATIC_LAUNCHER STREQUAL "OFF")
        find_library(NEURA_STATIC_LIBC NAMES libc.a)
        if(NEURA_STATIC_LIBC)
            set(NEURA_HAS_STATIC_LAUNCHER ON)
            add_library(neura_nvm_static STATIC ${neura_nvm_sources})
            target_link_libraries(neura_nvm_static PUBLIC neura_nvr neura_lift neura_platform)
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
elseif(WIN32 AND COMMAND neura_windows_launchers)
    # The PE launcher (cmake/windows/launchers.cmake).
    neura_windows_launchers()
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
    if(WIN32)
        # The Vulkan SDK's installer sets VULKAN_SDK; vcpkg's vulkan-headers
        # and glslang (the gpu feature of vcpkg.json) are on its search paths.
        find_path(NEURA_VULKAN_INCLUDE_DIR vulkan/vulkan.h HINTS "$ENV{VULKAN_SDK}/Include")
        find_program(NEURA_GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin")
    else()
        find_path(NEURA_VULKAN_INCLUDE_DIR vulkan/vulkan.h)
        find_program(NEURA_GLSLANG glslangValidator)
    endif()
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
add_library(neura_hash STATIC src/util/sha256.cpp)
neura_target_defaults(neura_hash)

add_library(neura_lifecycle STATIC src/cli/install_layout.cpp)
target_link_libraries(neura_lifecycle PUBLIC neura_hash)
target_include_directories(neura_lifecycle PUBLIC src)
neura_target_defaults(neura_lifecycle)

if(NEURA_SDK)
add_library(neura_compiler STATIC
    src/c-import/c_import.cpp
    src/cli/diagnostic_render.cpp
    src/cli/source_loader.cpp
    src/cli/source_docs.cpp
    src/lexer/island.cpp
    src/lexer/lexer.cpp
    src/parser/css_stylesheet.cpp
    src/parser/html_template.cpp
    src/parser/parser.cpp
    src/neura-ir/lowering.cpp
    src/neura-ir/derive_json.cpp
    src/neura-ir/ir_print.cpp
    src/neura-ir/integer.cpp
    src/neura-ir/float.cpp
    src/neura-ir/control_flow.cpp
    src/neura-ir/lifetime.cpp
    src/neura-ir/scoped_resources.cpp
    src/neura-ir/event_guard.cpp
    src/neura-ir/specialize.cpp
    src/neura-ir/type_of.cpp
    src/neura-ir/reflect.cpp
    src/neura-ir/containment.cpp
    src/neura-ir/inline_limit.cpp
    src/neura-ir/hook_arguments.cpp
    src/neura-ir/class_hooks.cpp
    src/neura-ir/value_layout.cpp
    src/neura-ir/storage.cpp
    src/neura-ir/static_initialization.cpp
    src/neura-ir/initialization_order.cpp
    src/neura-ir/task_analysis.cpp
    src/neura-ir/module_dispatch.cpp
    src/neura-ir/purity.cpp
    src/neura-ir/dispatch.cpp
    src/neura-ir/frozen.cpp
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
    src/ir-optimization/analysis_cache.cpp
    src/ir-optimization/inlining.cpp
    src/ir-optimization/integer_folding.cpp
    src/ir-optimization/locals.cpp
    src/ir-optimization/optimizer.cpp
    src/ir-optimization/passes.cpp
    src/ir-optimization/value_passes.cpp
    src/backend/converter.cpp
    src/util/source_map.cpp
    src/tooling/analysis.cpp
    src/tooling/lsp.cpp
    src/tooling/formatter.cpp
    src/tooling/test_protocol.cpp
    src/tooling/refactor.cpp
    src/tooling/coverage.cpp
)
target_include_directories(neura_compiler PUBLIC src)
target_include_directories(neura_compiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(neura_compiler PUBLIC neura_circuit neura_lift neura_nvr PRIVATE neura_libclang neura_hash)
if(WIN32)
    target_link_libraries(neura_compiler PUBLIC neura_platform)
endif()
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
endif()

add_library(neura_server_worker_wire STATIC EXCLUDE_FROM_ALL src/server/worker_wire.cpp)
target_include_directories(neura_server_worker_wire PUBLIC src)
neura_target_defaults(neura_server_worker_wire)

if(UNIX AND NOT EMSCRIPTEN)
    add_library(neura_server_http STATIC EXCLUDE_FROM_ALL
        src/server/http_wire.cpp
        src/server/http_connection.cpp
        src/server/http_tcp.cpp)
    target_include_directories(neura_server_http PUBLIC src)
    neura_target_defaults(neura_server_http)

    add_library(neura_server_listeners STATIC EXCLUDE_FROM_ALL src/server/http_listener.cpp)
    target_include_directories(neura_server_listeners PUBLIC src)
    target_link_libraries(neura_server_listeners PRIVATE neura_server_http)
    neura_target_defaults(neura_server_listeners)
elseif(WIN32)
    neura_windows_hook(server_libraries)
endif()

option(NEURA_ENABLE_SERVER_TLS "Build the OpenSSL 3.5 TLS transports" OFF)
if(NEURA_ENABLE_SERVER_TLS AND WIN32)
    # Std.Net.Tls and DoQ use the Winsock provider; the separate HTTP
    # listener has its own platform feature gate.
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    find_package(OpenSSL 3.5 REQUIRED COMPONENTS SSL Crypto)
    if(NOT OPENSSL_VERSION MATCHES "^3\\.5\\.")
        message(FATAL_ERROR "The Neura TLS provider requires OpenSSL 3.5")
    endif()
    target_compile_definitions(neura_nvm PRIVATE NEURA_NVM_TLS)
    target_link_libraries(neura_nvm PRIVATE OpenSSL::SSL OpenSSL::Crypto)
elseif(NEURA_ENABLE_SERVER_TLS)
    if(NOT UNIX OR EMSCRIPTEN)
        message(FATAL_ERROR "The Neura server TLS transport requires POSIX sockets")
    endif()

    function(neura_configure_server_tls)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
        find_package(OpenSSL 3.5 REQUIRED COMPONENTS SSL Crypto)
        if(NOT OPENSSL_VERSION MATCHES "^3\\.5\\." OR NOT OPENSSL_SSL_LIBRARY MATCHES "\\.a$"
           OR NOT OPENSSL_CRYPTO_LIBRARY MATCHES "\\.a$")
            message(FATAL_ERROR "The Neura server TLS transport requires OpenSSL 3.5 static archives")
        endif()
        # The OpenSSL found above, with its headers as an ordinary include
        # directory (-I): the imported OpenSSL::SSL would give -isystem, which
        # CPATH and the compiler's own directories can precede, so another
        # OpenSSL on the search path (Homebrew's, in /usr/local/include on
        # Intel macOS) was compiled against while the pinned one was linked.
        find_package(Threads REQUIRED)
        add_library(neura_openssl INTERFACE)
        target_include_directories(neura_openssl INTERFACE "${OPENSSL_INCLUDE_DIR}")
        target_link_libraries(neura_openssl INTERFACE "${OPENSSL_SSL_LIBRARY}" "${OPENSSL_CRYPTO_LIBRARY}"
            Threads::Threads ${CMAKE_DL_LIBS})

        add_library(neura_server_tls STATIC src/server/http_tls.cpp)
        target_include_directories(neura_server_tls PUBLIC src)
        target_link_libraries(neura_server_tls PRIVATE neura_server_http neura_openssl)
        neura_target_defaults(neura_server_tls)
        target_compile_definitions(neura_server_listeners PRIVATE NEURA_SERVER_TLS_ENABLED)
        # TLS client and server streams of the standard providers
        # (src/nvm/tls_provider.cpp); without this build option they answer
        # Unsupported.
        target_compile_definitions(neura_nvm PRIVATE NEURA_NVM_TLS)
        target_link_libraries(neura_nvm PRIVATE neura_openssl)
        target_link_libraries(neura_server_listeners PRIVATE neura_server_tls)
        # The test of this transport (server-http-tls) is registered after
        # include(CTest), with the other server tests: BUILD_TESTING is not
        # defined yet here.
    endfunction()

    neura_configure_server_tls()
endif()

# The SQL providers of Std.Data.Sql (docs/runtime/Sql.md): pools, sessions and
# migrations, used by neura-server-host and by `neura run`/`neura test`
# --sql-pool. SQLite is compiled in from the amalgamation that
# packaging/server-components.lock.toml pins: from NEURA_SQLITE_AMALGAMATION
# when it names the archive, otherwise downloaded at configure time; either
# way its SHA-256 must match the lock. PostgreSQL needs a static libpq 18.7
# and the static OpenSSL 3.5 of the TLS build.
option(NEURA_ENABLE_SQL "Build the SQL providers of Std.Data.Sql (SQLite)" ON)
set(NEURA_SQLITE_AMALGAMATION "" CACHE FILEPATH
    "sqlite-amalgamation-3540100.zip; when empty it is downloaded from the URL of the component lock")
option(NEURA_ENABLE_POSTGRES "Build the PostgreSQL provider of Std.Data.Sql over a static libpq 18.7" OFF)
set(NEURA_LIBPQ_SOURCE "" CACHE PATH "The postgresql-18.7 source tree, for the libpq headers")
set(NEURA_LIBPQ_BUILD "" CACHE PATH "The postgresql-18.7 build tree holding libpq.a, libpgcommon_shlib.a and libpgport_shlib.a")
if(NEURA_ENABLE_SQL AND NOT EMSCRIPTEN)
    set(neura_sqlite_sha256 "0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9")
    set(neura_sqlite_dir "${CMAKE_CURRENT_BINARY_DIR}/_deps/sqlite")
    set(neura_sqlite_source "${neura_sqlite_dir}/sqlite-amalgamation-3540100/sqlite3.c")
    if(NOT EXISTS "${neura_sqlite_source}")
        set(neura_sqlite_zip "${NEURA_SQLITE_AMALGAMATION}")
        if(neura_sqlite_zip STREQUAL "")
            set(neura_sqlite_zip "${neura_sqlite_dir}/sqlite-amalgamation-3540100.zip")
            message(STATUS "Downloading the SQLite 3.54.1 amalgamation")
            file(DOWNLOAD "https://sqlite.org/2026/sqlite-amalgamation-3540100.zip" "${neura_sqlite_zip}"
                EXPECTED_HASH SHA256=${neura_sqlite_sha256} STATUS neura_sqlite_status TLS_VERIFY ON)
            list(GET neura_sqlite_status 0 neura_sqlite_code)
            if(NOT neura_sqlite_code EQUAL 0)
                message(FATAL_ERROR "Cannot download the SQLite amalgamation (${neura_sqlite_status}). Pass "
                    "-DNEURA_SQLITE_AMALGAMATION=<sqlite-amalgamation-3540100.zip> or -DNEURA_ENABLE_SQL=OFF.")
            endif()
        endif()
        file(SHA256 "${neura_sqlite_zip}" neura_sqlite_actual)
        if(NOT neura_sqlite_actual STREQUAL neura_sqlite_sha256)
            message(FATAL_ERROR "${neura_sqlite_zip} has SHA-256 ${neura_sqlite_actual}; the lock pins ${neura_sqlite_sha256}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${neura_sqlite_zip}" DESTINATION "${neura_sqlite_dir}")
    endif()
    # Upstream C, compiled without the project's warnings.
    add_library(neura_sqlite3 STATIC "${neura_sqlite_source}")
    target_include_directories(neura_sqlite3 SYSTEM PUBLIC "${neura_sqlite_dir}/sqlite-amalgamation-3540100")
    target_compile_definitions(neura_sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0
        SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_LIKE_DOESNT_MATCH_BLOBS)
    set_target_properties(neura_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
    if(NOT MSVC)
        target_compile_options(neura_sqlite3 PRIVATE -w)
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(neura_sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

    add_library(neura_sql STATIC src/sql/sql_engine.cpp src/sql/sqlite_connection.cpp src/sql/postgres_connection.cpp
        src/sql/migrations.cpp)
    target_include_directories(neura_sql PUBLIC src)
    target_link_libraries(neura_sql PUBLIC neura_nvm neura_lifecycle PRIVATE neura_sqlite3)
    target_compile_definitions(neura_sql PUBLIC NEURA_HAS_SQL)
    neura_target_defaults(neura_sql)
    if(NEURA_ENABLE_POSTGRES AND WIN32)
        neura_windows_required_hook(postgres server-components.cmake)
    elseif(NEURA_ENABLE_POSTGRES)
        if(NOT NEURA_ENABLE_SERVER_TLS)
            message(FATAL_ERROR "NEURA_ENABLE_POSTGRES needs NEURA_ENABLE_SERVER_TLS for the static OpenSSL 3.5")
        endif()
        if(NOT EXISTS "${NEURA_LIBPQ_SOURCE}/src/interfaces/libpq/libpq-fe.h"
           OR NOT EXISTS "${NEURA_LIBPQ_BUILD}/src/interfaces/libpq/libpq.a")
            message(FATAL_ERROR "NEURA_ENABLE_POSTGRES needs NEURA_LIBPQ_SOURCE (postgresql-18.7) and "
                "NEURA_LIBPQ_BUILD (its build tree with libpq.a)")
        endif()
        # First, like the OpenSSL headers: a libpq elsewhere on the search
        # path is never compiled against.
        target_include_directories(neura_sql BEFORE PRIVATE "${NEURA_LIBPQ_SOURCE}/src/interfaces/libpq"
            "${NEURA_LIBPQ_SOURCE}/src/include")
        target_compile_definitions(neura_sql PRIVATE NEURA_SQL_POSTGRES)
        target_link_libraries(neura_sql PRIVATE "${NEURA_LIBPQ_BUILD}/src/interfaces/libpq/libpq.a"
            "${NEURA_LIBPQ_BUILD}/src/common/libpgcommon_shlib.a" "${NEURA_LIBPQ_BUILD}/src/port/libpgport_shlib.a"
            neura_openssl Threads::Threads)
    endif()
endif()

# HTTP/2 in the server host (docs/design/Backend030.md, "HTTP/2 and decision
# 9"): ALPN h2 on the TLS listeners and h2c with prior knowledge, framed by
# the static nghttp2 the server component lock pins. Off by default; the host
# then speaks HTTP/1.1 only. NEURA_NGHTTP2_ROOT is the install prefix of that
# build; the version must be the locked one, so a system libnghttp2 is never
# linked by accident (NEURA_NGHTTP2_ALLOW_UNPINNED is for comparison builds).
option(NEURA_ENABLE_SERVER_HTTP2 "Build HTTP/2 into the server host with the pinned nghttp2" OFF)
set(NEURA_NGHTTP2_ROOT "" CACHE PATH "Install prefix of the pinned static nghttp2")
option(NEURA_NGHTTP2_ALLOW_UNPINNED "Accept an nghttp2 other than the locked version (never for release)" OFF)
if(NEURA_ENABLE_SERVER_HTTP2 AND WIN32)
    neura_windows_required_hook(server_http2 server-components.cmake)
elseif(NEURA_ENABLE_SERVER_HTTP2)
    if(NOT UNIX OR EMSCRIPTEN)
        message(FATAL_ERROR "The Neura server HTTP/2 transport requires POSIX sockets")
    endif()
    if(NOT NEURA_NGHTTP2_ROOT)
        message(FATAL_ERROR "NEURA_ENABLE_SERVER_HTTP2 needs NEURA_NGHTTP2_ROOT, the install prefix of the "
            "nghttp2 build packaging/server-components.lock.toml pins")
    endif()
    find_path(NEURA_NGHTTP2_INCLUDE_DIR nghttp2/nghttp2.h PATHS "${NEURA_NGHTTP2_ROOT}/include" NO_DEFAULT_PATH)
    find_library(NEURA_NGHTTP2_LIBRARY NAMES libnghttp2.a
        PATHS "${NEURA_NGHTTP2_ROOT}/lib" "${NEURA_NGHTTP2_ROOT}/lib64" NO_DEFAULT_PATH)
    if(NOT NEURA_NGHTTP2_INCLUDE_DIR OR NOT NEURA_NGHTTP2_LIBRARY)
        message(FATAL_ERROR "No static nghttp2 (include/nghttp2/nghttp2.h and lib/libnghttp2.a) under "
            "${NEURA_NGHTTP2_ROOT}")
    endif()
    file(STRINGS "${NEURA_NGHTTP2_INCLUDE_DIR}/nghttp2/nghttp2ver.h" neura_nghttp2_version_line
        REGEX "^#define NGHTTP2_VERSION \"")
    string(REGEX REPLACE "^#define NGHTTP2_VERSION \"([^\"]*)\".*" "\\1" neura_nghttp2_version
        "${neura_nghttp2_version_line}")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/packaging/server-components.lock.toml" neura_component_lock)
    string(REGEX MATCH "\\[providers\\.nghttp2\\][^[]*version = \"([^\"]*)\"" neura_nghttp2_entry
        "${neura_component_lock}")
    set(neura_nghttp2_locked "${CMAKE_MATCH_1}")
    if(NOT neura_nghttp2_version STREQUAL neura_nghttp2_locked)
        if(NEURA_NGHTTP2_ALLOW_UNPINNED)
            message(WARNING "nghttp2 ${neura_nghttp2_version} is not the locked ${neura_nghttp2_locked}: "
                "this build is for comparison only")
        else()
            message(FATAL_ERROR "nghttp2 ${neura_nghttp2_version} under ${NEURA_NGHTTP2_ROOT} is not the "
                "locked ${neura_nghttp2_locked}")
        endif()
    endif()
    message(STATUS "Server HTTP/2: nghttp2 ${neura_nghttp2_version} (${NEURA_NGHTTP2_LIBRARY})")

    find_package(Threads REQUIRED)
    # An interface library, not an imported one, so the pinned headers are
    # an ordinary -I directory ahead of any other nghttp2 (see neura_openssl).
    add_library(neura_nghttp2 INTERFACE)
    target_include_directories(neura_nghttp2 INTERFACE "${NEURA_NGHTTP2_INCLUDE_DIR}")
    target_link_libraries(neura_nghttp2 INTERFACE "${NEURA_NGHTTP2_LIBRARY}")
    target_sources(neura_server_http PRIVATE src/server/http2_connection.cpp)
    target_link_libraries(neura_server_http PUBLIC neura_nghttp2 Threads::Threads)
    target_compile_definitions(neura_server_http PUBLIC NEURA_SERVER_HTTP2_ENABLED)
endif()

# --- The Neura tools (docs/design/Products030.md) ------------------------------
# `neura` is a dispatcher: each command runs a sibling tool from the same
# directory. The code the tools share is in two shared libraries in
# lib/neura, named with the full version: libneura-core (image format, NVM
# with its tiers and JIT, kernels, host services, runner, and the commands of
# the execution core) and libneura-sdk (compiler, decompiler and editor
# tools), which needs libneura-core. A tool is a small executable that finds
# them through its RPATH, relative to itself, so an installation can move.
# The core tools load libneura-core only. Tests keep linking the static
# libraries below.

# The registry: data, the version line and the usage text. The dispatcher
# links only this.
add_library(neura_cli_registry STATIC src/cli/tool_registry.cpp)
target_include_directories(neura_cli_registry PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
neura_target_defaults(neura_cli_registry)

# A tool's start: the dispatch token, --help, --version, the real path.
add_library(neura_cli_tool STATIC src/cli/tool.cpp)
target_link_libraries(neura_cli_tool PUBLIC neura_cli_registry neura_platform)
neura_target_defaults(neura_cli_tool)

# The protocol of the runtime service (docs/design/Service030.md): pure
# data handling, shared by the client in neura-run and by neurad.
add_library(neura_service_protocol STATIC src/service/protocol.cpp)
target_include_directories(neura_service_protocol PUBLIC src)
neura_target_defaults(neura_service_protocol)

# The option reader and the commands of the execution core: run, inspect,
# inspect-image, doctor, update and uninstall, and the update notice.
add_library(neura_cli_core STATIC
    src/cli/argument_lists.cpp
    src/cli/commands_inspect.cpp
    src/cli/commands_run.cpp
    src/cli/driver_core.cpp
    src/cli/gpu_run.cpp
    src/cli/lifecycle.cpp
    src/cli/options.cpp
    src/cli/serve.cpp
    src/launcher/descriptor.cpp
    src/tooling/inspect.cpp
)
if(NOT WIN32)
    # The runtime service (docs/design/Service030.md): the client of
    # `run --runtime`, the `runtime` commands, and neurad's supervisor and
    # worker, which link the execution core only.
    target_sources(neura_cli_core PRIVATE
        src/service/client.cpp
        src/service/connect.cpp
        src/service/control.cpp
        src/service/launch.cpp
        src/service/login.cpp
        src/service/supervisor.cpp
        src/service/system.cpp
        src/service/worker.cpp
    )
    target_link_libraries(neura_cli_core PUBLIC neura_service_protocol ${CMAKE_DL_LIBS})
    target_compile_definitions(neura_cli_core PUBLIC NEURA_HAS_SERVICE=1)
elseif(NEURA_WINDOWS_SERVICE)
    neura_windows_hook(service_sources)
endif()
target_include_directories(neura_cli_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(neura_cli_core PUBLIC neura_cli_tool neura_runner neura_lifecycle)
if(TARGET neura_sql)
    # neura run and neura test take --sql-pool (docs/runtime/Sql.md).
    target_link_libraries(neura_cli_core PUBLIC neura_sql)
endif()
if(NEURA_HAS_GPU)
    # The GPU options change the layout of the option types, so every user of
    # them is built with the same setting.
    target_link_libraries(neura_cli_core PUBLIC neura_gpu)
    target_compile_definitions(neura_cli_core PUBLIC NEURA_HAS_GPU=1)
endif()
neura_target_defaults(neura_cli_core)

if(NEURA_SDK)
# The commands of the SDK: the compiler's, test, the decompiler's, the
# reference pages, the debug adapter, and the driver that renders source
# diagnostics.
add_library(neura_cli_sdk STATIC
    src/cli/commands_compile.cpp
    src/cli/commands_test.cpp
    src/cli/driver_sdk.cpp
)
target_link_libraries(neura_cli_sdk PUBLIC neura_cli_core neura_compiler)
neura_target_defaults(neura_cli_sdk)

add_library(neura_cli_artifact STATIC src/cli/commands_artifact.cpp)
target_link_libraries(neura_cli_artifact PUBLIC neura_cli_sdk neura_decompiler)
neura_target_defaults(neura_cli_artifact)

add_library(neura_cli_explain STATIC src/cli/commands_explain.cpp src/cli/explain.cpp)
target_include_directories(neura_cli_explain PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(neura_cli_explain PUBLIC neura_cli_sdk)
neura_target_defaults(neura_cli_explain)

add_library(neura_cli_tags STATIC src/cli/commands_tags.cpp)
target_link_libraries(neura_cli_tags PUBLIC neura_cli_sdk)
neura_target_defaults(neura_cli_tags)

add_library(neura_cli_debug STATIC src/tooling/debug.cpp)
target_link_libraries(neura_cli_debug PUBLIC neura_cli_sdk)
neura_target_defaults(neura_cli_debug)
endif()

set(NEURA_LIBRARY_DIRECTORY "lib/neura")
# In the build tree the tools are in the build directory and the libraries
# in its lib/neura; installed, the tools are in bin/ and the libraries in
# lib/neura.
if(APPLE)
    set(neura_build_rpath "@loader_path/${NEURA_LIBRARY_DIRECTORY}")
    set(neura_install_rpath "@loader_path/../${NEURA_LIBRARY_DIRECTORY}")
else()
    set(neura_build_rpath "$ORIGIN/${NEURA_LIBRARY_DIRECTORY}")
    set(neura_install_rpath "$ORIGIN/../${NEURA_LIBRARY_DIRECTORY}")
endif()

# neura_add_shared(<target> <name> <component> <first> <static libraries>...):
# the shared library lib<name>-<full version> holding every object of the
# given static libraries. <first> (or "none") is a shared library linked
# ahead of them, so that the new library takes every symbol <first> defines
# from it instead of from the archives.
function(neura_add_shared target name component first)
    if(WIN32)
        # A self-contained DLL in bin/ (W04): no process loads two Neura
        # DLLs, so <first> is not linked.
        neura_windows_add_shared(${target} ${name} ${component} ${ARGN})
        return()
    endif()
    add_library(${target} SHARED src/cli/shared_library.cpp)
    if(first STREQUAL "none")
        set(first)
    endif()
    set(archives)
    foreach(library IN LISTS ARGN)
        if(APPLE)
            list(APPEND archives "-Wl,-force_load,$<TARGET_FILE:${library}>")
        else()
            list(APPEND archives "$<TARGET_FILE:${library}>")
        endif()
    endforeach()
    # <first> follows the objects that need it, so that a linker that drops
    # unneeded libraries (--as-needed) sees their references.
    if(APPLE)
        target_link_libraries(${target} PRIVATE ${archives} ${first})
    else()
        target_link_libraries(${target} PRIVATE -Wl,--whole-archive ${archives} -Wl,--no-whole-archive ${first})
        # Calls between the library's own functions bind inside it; data,
        # such as the static variables of inline functions that both
        # libraries and the tools share, stays unified across them.
        target_link_options(${target} PRIVATE -Wl,-Bsymbolic-functions -Wl,--no-undefined)
    endif()
    # The dependencies of the archives (libffi, the loader, other archives
    # whose symbols another shared library already defines).
    target_link_libraries(${target} PRIVATE ${ARGN})
    add_dependencies(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${name}-${NEURA_VERSION_FULL}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${NEURA_LIBRARY_DIRECTORY}"
        INSTALL_NAME_DIR "@rpath")
    # libneura-sdk finds libneura-core beside itself.
    if(APPLE)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    else()
        set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    endif()
    if(NEURA_PORTABLE_RELEASE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # No dependency on the host's C++ runtime: libneura-core holds it.
        target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    neura_target_defaults(${target})
    install(TARGETS ${target} LIBRARY DESTINATION "${NEURA_LIBRARY_DIRECTORY}" COMPONENT ${component})
endfunction()

set(neura_core_archives neura_cli_core neura_cli_tool neura_cli_registry neura_runner neura_nvm neura_lift neura_circuit
    neura_nvr neura_kernels neura_lifecycle neura_service_protocol neura_platform)
if(NEURA_HAS_GPU)
    list(APPEND neura_core_archives neura_gpu)
endif()
if(TARGET neura_sql)
    list(APPEND neura_core_archives neura_sql)
endif()
if(WIN32)
    # neura-run's project layer (defined below): every tool of the core is in
    # the core DLL.
    list(APPEND neura_core_archives neura_app neura_project)
endif()
neura_add_shared(neura_core_library neura-core core none ${neura_core_archives})
if(NEURA_ENABLE_SERVER_TLS AND NOT APPLE AND NOT WIN32)
    # The static OpenSSL of the TLS streams stays private to the library, so
    # the server host's own copy (src/server/http_tls.cpp) cannot interpose.
    target_link_options(neura_core_library PRIVATE "-Wl,--exclude-libs,libssl.a:libcrypto.a")
    if(NEURA_ENABLE_POSTGRES)
        # libpq and its support archives stay private too, so a program that
        # loads a system libpq through C import binds to its own copy.
        target_link_options(neura_core_library PRIVATE
            "-Wl,--exclude-libs,libpq.a:libpgcommon_shlib.a:libpgport_shlib.a")
    endif()
endif()
if(NEURA_PORTABLE_RELEASE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The C++ runtime lives in libneura-core, whole, and the SDK library and
    # the tools take it from there; each needs only the C library, libm and
    # the loader from the host.
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.a
        OUTPUT_VARIABLE NEURA_PORTABLE_LIBSTDCXX OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT IS_ABSOLUTE "${NEURA_PORTABLE_LIBSTDCXX}" OR NOT EXISTS "${NEURA_PORTABLE_LIBSTDCXX}")
        message(FATAL_ERROR "NEURA_PORTABLE_RELEASE needs the static C++ runtime (libstdc++.a)")
    endif()
    target_link_libraries(neura_core_library PRIVATE -Wl,--whole-archive "${NEURA_PORTABLE_LIBSTDCXX}" -Wl,--no-whole-archive)
    # libffi's static archive is often not position independent; its symbols
    # stay private to the library, which lets the linker resolve its
    # PC-relative references.
    target_link_options(neura_core_library PRIVATE "-Wl,--exclude-libs,libffi.a")
endif()
if(NEURA_SDK)
    # The project layer (neura_app, neura_project; defined below) is in the SDK library once, for neurac and
    # the project tools. neura-run, a core tool, keeps its own static copy, so that a runtime-only
    # installation runs bundles without libneura-sdk.
    neura_add_shared(neura_sdk_library neura-sdk sdk neura_core_library neura_cli_sdk neura_cli_artifact neura_cli_explain
        neura_cli_tags neura_cli_debug neura_compiler neura_decompiler neura_libclang neura_app neura_project)
endif()

set(NEURA_TOOL_TARGETS)
# neura_add_tool(<name> <component> <source> <shared libraries>...): one
# executable in bin/, named <name> in the build tree and the install.
function(neura_add_tool name component source)
    if(WIN32)
        # The tool's code goes into its product DLL and ${name}.exe is a stub.
        neura_windows_add_tool(${name} ${source} ${ARGN})
        if(NOT component STREQUAL "service")
            install(TARGETS ${name} RUNTIME DESTINATION bin COMPONENT ${component})
        endif()
        set(NEURA_TOOL_TARGETS ${NEURA_TOOL_TARGETS} ${name} PARENT_SCOPE)
        return()
    endif()
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${ARGN})
    target_include_directories(${name} PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    if(NEURA_HAS_GPU)
        target_compile_definitions(${name} PRIVATE NEURA_HAS_GPU=1)
    endif()
    set_target_properties(${name} PROPERTIES BUILD_RPATH "${neura_build_rpath}" INSTALL_RPATH "${neura_install_rpath}")
    if(NEURA_PORTABLE_RELEASE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${name} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    neura_target_defaults(${name})
    if(NOT component STREQUAL "service")
        install(TARGETS ${name} RUNTIME DESTINATION bin COMPONENT ${component})
    endif()
    set(NEURA_TOOL_TARGETS ${NEURA_TOOL_TARGETS} ${name} PARENT_SCOPE)
endfunction()

# What a tool's own code needs beyond neura_add_tool: on Windows that code is
# in its product DLL, elsewhere in the tool's executable.
# neura_tool_link_libraries(<tool> <items>...) links the items privately.
function(neura_tool_link_libraries tool)
    if(WIN32)
        get_target_property(tool ${tool} NEURA_TOOL_LIBRARY)
    endif()
    target_link_libraries(${tool} PRIVATE ${ARGN})
endfunction()
# neura_tool_add_dependencies(<tool> <targets>...): builds the targets before
# the tool's code is linked.
function(neura_tool_add_dependencies tool)
    if(WIN32)
        get_target_property(tool ${tool} NEURA_TOOL_LIBRARY)
    endif()
    add_dependencies(${tool} ${ARGN})
endfunction()
# neura_tool_compile_definitions(<tool> <definitions>...): private definitions
# of the tool's source only.
function(neura_tool_compile_definitions tool)
    if(WIN32)
        get_target_property(entry ${tool} NEURA_TOOL_ENTRY_SOURCE)
        set_property(SOURCE "${entry}" APPEND PROPERTY COMPILE_DEFINITIONS ${ARGN})
    else()
        target_compile_definitions(${tool} PRIVATE ${ARGN})
    endif()
endfunction()

neura_add_tool(neura-run core src/cli/tools/neura-run.cpp neura_core_library)
neura_add_tool(neura-inspect core src/cli/tools/neura-inspect.cpp neura_core_library)
neura_add_tool(neura-doctor core src/cli/tools/neura-doctor.cpp neura_core_library)
neura_add_tool(neura-update core src/cli/tools/neura-update.cpp neura_core_library)
neura_add_tool(neura-uninstall core src/cli/tools/neura-uninstall.cpp neura_core_library)
if(NOT WIN32 OR NEURA_WINDOWS_SERVICE)
    # The service component. `cmake --install` of every component leaves it
    # out, so the SDK's prefix is unchanged; `--component service` adds it.
    neura_add_tool(neurad service src/cli/tools/neurad.cpp neura_core_library)
    install(TARGETS neurad RUNTIME DESTINATION bin COMPONENT service EXCLUDE_FROM_ALL)
endif()
if(NEURA_SDK)
    foreach(tool IN ITEMS neurac neura-test neura-doc neura-cbind neura-decompile neura-asm neura-disasm neura-explain
            neura-tags neura-debug neura-lsp neura-fmt)
        neura_add_tool(${tool} sdk src/cli/tools/${tool}.cpp neura_core_library neura_sdk_library)
    endforeach()
    # The doctor reports libclang through the C import stage's probe, which
    # loads libclang only when it runs. It takes that one object from the
    # static compiler library, so it does not load libneura-sdk.
    neura_tool_link_libraries(neura-doctor "$<TARGET_FILE:neura_compiler>"
        "$<TARGET_FILE:neura_libclang>" ${CMAKE_DL_LIBS})
    neura_tool_add_dependencies(neura-doctor neura_compiler neura_libclang)
    neura_tool_compile_definitions(neura-doctor NEURA_DOCTOR_PROBES_LIBCLANG=1)
endif()

# The dispatcher links the registry only.
add_executable(neura src/cli/main.cpp)
target_link_libraries(neura PRIVATE neura_cli_registry neura_platform)
if(WIN32)
    target_link_libraries(neura PRIVATE neura_cli_tool)
endif()
# Every command starts neura and then its tool, so neura starts as fast as
# it can: on Linux it is a static executable, which the loader does not
# relocate, where the C library has a static form. A sanitizer build turns
# this off, since a static executable cannot carry the sanitizer runtime.
option(NEURA_STATIC_DISPATCHER "Link the neura dispatcher statically on Linux" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NEURA_STATIC_DISPATCHER)
    find_library(NEURA_DISPATCHER_STATIC_LIBC NAMES libc.a)
    if(NEURA_DISPATCHER_STATIC_LIBC)
        target_link_options(neura PRIVATE -static)
    else()
        target_link_options(neura PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()
target_include_directories(neura PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Building neura builds every tool it starts and the launchers, so a test or
# a workflow that builds the target neura gets a complete set.
add_dependencies(neura ${NEURA_TOOL_TARGETS})
if(TARGET neura_launcher)
    add_dependencies(neura neura_launcher)
endif()
if(TARGET neura_launcher_static)
    add_dependencies(neura neura_launcher_static)
endif()
neura_target_defaults(neura)

install(TARGETS neura RUNTIME DESTINATION bin COMPONENT core)
install(FILES LICENSE DESTINATION share/doc/neura COMPONENT core)
# The licence texts of the server components this build links statically,
# which packaging/build-server-components.sh copies out of the verified
# sources: they ship with the core component, beside Neura's own. A release
# build (NEURA_PORTABLE_RELEASE) that links one of them refuses to configure
# without its text.
set(NEURA_COMPONENT_LICENSES "" CACHE PATH
    "The licenses directory that packaging/build-server-components.sh writes")
set(neura_component_license_files)
if(NEURA_ENABLE_SERVER_TLS)
    list(APPEND neura_component_license_files openssl-LICENSE.txt)
endif()
if(NEURA_ENABLE_SERVER_HTTP2)
    list(APPEND neura_component_license_files nghttp2-COPYING)
endif()
if(NEURA_ENABLE_POSTGRES)
    list(APPEND neura_component_license_files postgresql-COPYRIGHT)
endif()
if(neura_component_license_files)
    if(NEURA_COMPONENT_LICENSES)
        set(neura_component_license_paths)
        foreach(file IN LISTS neura_component_license_files)
            if(NOT EXISTS "${NEURA_COMPONENT_LICENSES}/${file}")
                message(FATAL_ERROR "${NEURA_COMPONENT_LICENSES}/${file} is missing")
            endif()
            list(APPEND neura_component_license_paths "${NEURA_COMPONENT_LICENSES}/${file}")
        endforeach()
        install(FILES ${neura_component_license_paths} DESTINATION share/doc/neura/third-party COMPONENT core)
    elseif(NEURA_PORTABLE_RELEASE)
        message(FATAL_ERROR "A release build that links ${neura_component_license_files} needs their texts: "
            "pass -DNEURA_COMPONENT_LICENSES=<prefix>/licenses from packaging/build-server-components.sh")
    endif()
endif()
# The WHATWG HTML entity table that the template lexer embeds (third_party/whatwg-html).
install(FILES third_party/whatwg-html/LICENSE DESTINATION share/doc/neura
    RENAME LICENSE-WHATWG-HTML COMPONENT sdk)
# The third install component, service, holds neurad when its target exists
# (docs/design/Service030.md). Without it the component is empty, and
# packaging/make-archive.sh still writes its manifest with no tool or file
# (docs/design/Lifecycle030.md).
if(NEURA_SDK)
    install(DIRECTORY stdlib/ DESTINATION share/neura/stdlib OPTIONAL COMPONENT sdk
        FILES_MATCHING PATTERN "*.nra")
    # What `neura build --target` puts in a web bundle and `neura run` starts a desktop bundle with
    # (docs/design/Projects030.md): the page modules of the browser runtime, its nvm.wasm and nvm.mjs when
    # this build has them, and the desktop host's main.js and preload.js.
    set(NEURA_WEB_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/wasm-runtime" CACHE PATH
        "Directory holding nvm.wasm and nvm.mjs for the browser checks")
    install(FILES hosts/web/boot.mjs hosts/web/dom.mjs hosts/web/fetch.mjs hosts/web/nvm.js hosts/web/ui.mjs
        hosts/web/ui-wire.mjs DESTINATION share/neura/web COMPONENT sdk)
    if(EXISTS "${NEURA_WEB_RUNTIME_DIR}/nvm.wasm" AND EXISTS "${NEURA_WEB_RUNTIME_DIR}/nvm.mjs")
        install(FILES "${NEURA_WEB_RUNTIME_DIR}/nvm.wasm" "${NEURA_WEB_RUNTIME_DIR}/nvm.mjs"
            DESTINATION share/neura/web COMPONENT sdk)
    endif()
endif()

include(CTest)

if(NEURA_SDK)
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
endif()

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

    # The platform layer, one entry per module, on every host.
    add_executable(neura_platform_tests tests/platform/platform_tests.cpp)
    target_link_libraries(neura_platform_tests PRIVATE neura_platform)
    neura_target_defaults(neura_platform_tests)
    foreach(suite IN ITEMS text process file console socket dynlib environment dirs)
        add_test(NAME platform-${suite} COMMAND neura_platform_tests ${suite})
        set_tests_properties(platform-${suite} PROPERTIES TIMEOUT 120)
    endforeach()

    # The portable helpers in src/util. Int128 is a struct only with MSVC and
    # clang-cl; elsewhere the same expected values check __int128.
    add_executable(neura_util_tests tests/platform/util_tests.cpp)
    target_include_directories(neura_util_tests PRIVATE src)
    neura_target_defaults(neura_util_tests)
    foreach(suite IN ITEMS int128 line-endings)
        add_test(NAME util-${suite} COMMAND neura_util_tests ${suite})
    endforeach()
endif()

add_library(neura_project STATIC EXCLUDE_FROM_ALL
    src/project/toml.cpp
    src/project/manifest.cpp
    src/project/lock.cpp
    src/project/resolve.cpp
    src/project/bundle.cpp
)
target_include_directories(neura_project PUBLIC src)
target_link_libraries(neura_project PRIVATE neura_hash)
neura_target_defaults(neura_project)

# The project tools' shared layer (docs/design/Projects030.md): `neura build
# --project`, `neura run <bundle>`, and the neura-new, neura-dev and
# neura-package tools.
add_library(neura_app STATIC EXCLUDE_FROM_ALL src/app/project.cpp src/app/run_bundle.cpp src/app/build_command.cpp
    src/app/closure.cpp)
target_include_directories(neura_app PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(neura_app PUBLIC neura_project PRIVATE neura_hash)
if(NEURA_ENABLE_POSTGRES)
    target_compile_definitions(neura_app PRIVATE NEURA_HAS_POSTGRES_PROVIDER)
endif()
if(NEURA_ENABLE_SERVER_TLS)
    target_compile_definitions(neura_app PRIVATE NEURA_HAS_TLS_PROVIDER)
endif()
neura_target_defaults(neura_app)
# neura-run belongs to the core (docs/design/Products030.md) and links the layer statically; neurac and the
# project tools take it from libneura-sdk.
neura_tool_link_libraries(neura-run neura_app)
if(NEURA_SDK)
    # `neura new` (src/app/new.cpp) and the project templates it copies.
    neura_add_tool(neura-new sdk src/app/new.cpp neura_core_library neura_sdk_library)
    add_dependencies(neura neura-new)
    install(DIRECTORY templates/ DESTINATION share/neura/templates COMPONENT sdk)
    # `neura package` (src/app/package.cpp); a desktop package goes through the desktop packager, which finds
    # the desktop host in hosts/desktop beside its own parent directory, as in this repository.
    neura_add_tool(neura-package sdk src/app/package.cpp neura_core_library neura_sdk_library)
    add_dependencies(neura neura-package)
    # `neura dev` (src/app/dev.cpp): build, run and rebuild in the foreground.
    # On Windows it needs the job-based host control of cmake/windows/project-tools.cmake.
    if(NOT WIN32 OR NEURA_WINDOWS_DEV)
        neura_add_tool(neura-dev sdk src/app/dev.cpp neura_core_library neura_sdk_library)
        add_dependencies(neura neura-dev)
    endif()
    install(FILES scripts/package-desktop.py DESTINATION share/neura/scripts COMPONENT sdk)
    install(FILES hosts/desktop/main.js hosts/desktop/preload.js DESTINATION share/neura/hosts/desktop COMPONENT sdk)
    install(FILES LICENSE DESTINATION share/neura COMPONENT sdk)
endif()

# The server host (docs/design/Backend030.md): listeners, TLS when
# NEURA_ENABLE_SERVER_TLS is on, and one request-worker process per request.
# The same executable is the worker, in its --worker mode. It is a tool of
# the core component: its worker objects call the runner, the NVM and the SQL
# providers in libneura-core, which it loads like the other core tools.
if(UNIX AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    add_library(neura_server_worker STATIC src/server/worker_messages.cpp src/server/request_worker.cpp
        src/server/worker_zygote.cpp src/server/sql_channel.cpp src/server/sql_host.cpp)
    target_include_directories(neura_server_worker PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(neura_server_worker PUBLIC neura_server_worker_wire)
    if(TARGET neura_sql)
        target_compile_definitions(neura_server_worker PUBLIC NEURA_HAS_SQL)
    endif()
    neura_target_defaults(neura_server_worker)
    neura_add_tool(neura-server-host core src/server/server_host.cpp neura_server_worker neura_server_listeners
        neura_server_http neura_core_library Threads::Threads)
    add_dependencies(neura neura-server-host)
elseif(WIN32)
    neura_windows_hook(server_tools)
endif()

if(BUILD_TESTING AND NEURA_SDK)
    add_executable(neura_wait_source_tests
        src/nvm/pending_requests.cpp
        tests/backend/wait_source_tests.cpp)
    target_include_directories(neura_wait_source_tests PRIVATE src)
    if(NOT EMSCRIPTEN)
        find_package(Threads REQUIRED)
        target_link_libraries(neura_wait_source_tests PRIVATE Threads::Threads)
    endif()
    neura_target_defaults(neura_wait_source_tests)
    add_test(NAME nvm-wait-source COMMAND neura_wait_source_tests)
    set_tests_properties(nvm-wait-source PROPERTIES TIMEOUT 10)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # The same waits through the epoll(7) backend (src/nvm/pending_requests.cpp).
        add_test(NAME nvm-wait-source-epoll COMMAND neura_wait_source_tests)
        set_tests_properties(nvm-wait-source-epoll PROPERTIES TIMEOUT 10 ENVIRONMENT "NEURA_IO_BACKEND=epoll")
        add_test(NAME nvm-wait-source-io-uring COMMAND neura_wait_source_tests)
        set_tests_properties(nvm-wait-source-io-uring PROPERTIES TIMEOUT 10 ENVIRONMENT "NEURA_IO_BACKEND=io_uring")
    endif()

    add_executable(neura_worker_wire_tests tests/server/worker_wire_tests.cpp)
    target_link_libraries(neura_worker_wire_tests PRIVATE neura_server_worker_wire)
    neura_target_defaults(neura_worker_wire_tests)
    add_test(NAME server-worker-wire COMMAND neura_worker_wire_tests)

    if(TARGET neura_server_worker)
        add_executable(neura_worker_channel_tests tests/server/worker_channel_tests.cpp)
        target_link_libraries(neura_worker_channel_tests PRIVATE neura_server_worker Threads::Threads)
        neura_target_defaults(neura_worker_channel_tests)
        add_test(NAME server-worker-channel COMMAND neura_worker_channel_tests)
        set_tests_properties(server-worker-channel PROPERTIES TIMEOUT 60)
    endif()

    add_executable(neura_http_wire_tests
        src/server/http_wire.cpp
        tests/server/http_wire_tests.cpp)
    target_include_directories(neura_http_wire_tests PRIVATE src)
    neura_target_defaults(neura_http_wire_tests)
    add_test(NAME server-http-wire COMMAND neura_http_wire_tests)

    # The server libraries' tests, wherever their targets are defined (on
    # Windows by cmake/windows/server.cmake).
    if(TARGET neura_server_http)
        find_package(Threads REQUIRED)
        add_executable(neura_http_connection_tests tests/server/http_connection_tests.cpp)
        target_link_libraries(neura_http_connection_tests PRIVATE neura_server_http Threads::Threads)
        neura_target_defaults(neura_http_connection_tests)
        add_test(NAME server-http-connection COMMAND neura_http_connection_tests)
        set_tests_properties(server-http-connection PROPERTIES TIMEOUT 15)

        if(NEURA_ENABLE_SERVER_TLS)
            add_executable(neura_http_tls_tests tests/server/http_tls_tests.cpp)
            target_link_libraries(neura_http_tls_tests PRIVATE neura_server_tls neura_openssl Threads::Threads)
            neura_target_defaults(neura_http_tls_tests)
            add_test(NAME server-http-tls COMMAND neura_http_tls_tests)
            set_tests_properties(server-http-tls PROPERTIES TIMEOUT 15)
        endif()

        add_executable(neura_http_listener_foreground tests/server/http_listener_foreground.cpp)
        target_link_libraries(neura_http_listener_foreground PRIVATE neura_server_listeners Threads::Threads)
        neura_target_defaults(neura_http_listener_foreground)

        if(NEURA_ENABLE_SERVER_HTTP2)
            # The scripted HTTP/2 client of the server host tests.
            add_executable(neura_http2_test_client tests/server/http2_test_client.cpp)
            target_link_libraries(neura_http2_test_client PRIVATE neura_nghttp2)
            neura_target_defaults(neura_http2_test_client)
        endif()
    endif()

    add_executable(neura_project_toml_tests tests/project/toml_tests.cpp)
    target_link_libraries(neura_project_toml_tests PRIVATE neura_project)
    neura_target_defaults(neura_project_toml_tests)
    add_test(NAME project-toml COMMAND neura_project_toml_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/project")

    add_executable(neura_project_manifest_tests tests/project/manifest_tests.cpp)
    target_link_libraries(neura_project_manifest_tests PRIVATE neura_project)
    neura_target_defaults(neura_project_manifest_tests)
    add_test(NAME project-manifest
        COMMAND neura_project_manifest_tests "${CMAKE_CURRENT_SOURCE_DIR}/docs/design/Projects030.md")

    add_executable(neura_project_lock_tests tests/project/lock_tests.cpp)
    target_link_libraries(neura_project_lock_tests PRIVATE neura_project)
    neura_target_defaults(neura_project_lock_tests)
    add_test(NAME project-lock
        COMMAND neura_project_lock_tests "${CMAKE_CURRENT_SOURCE_DIR}/docs/design/Projects030.md")

    add_executable(neura_project_resolve_tests tests/project/resolve_tests.cpp)
    target_link_libraries(neura_project_resolve_tests PRIVATE neura_project)
    neura_target_defaults(neura_project_resolve_tests)
    add_test(NAME project-resolve COMMAND neura_project_resolve_tests)

    add_executable(neura_project_bundle_tests tests/project/bundle_tests.cpp)
    target_link_libraries(neura_project_bundle_tests PRIVATE neura_project)
    neura_target_defaults(neura_project_bundle_tests)
    add_test(NAME project-bundle COMMAND neura_project_bundle_tests)

    # Project builds end to end: every target kind, two paths compared, the refusals of
    # docs/design/Projects030.md and `neura run` of a bundle (tests/project/build.py).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME project-build COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/project/build.py"
        "$<TARGET_FILE:neura>" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/project-build-tests")
    set_tests_properties(project-build PROPERTIES TIMEOUT 900)
    # `neura new` and its templates (tests/project/new.py).
    add_test(NAME project-new COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/project/new.py"
        "$<TARGET_FILE:neura>" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/project-new-tests")
    set_tests_properties(project-new PROPERTIES TIMEOUT 900)
    # `neura dev` (tests/project/dev.py): reloads, stylesheet swaps, restarts and process-tree checks.
    # Registered only where neura-dev is built (see the neura-dev tool above).
    if(NOT WIN32 OR NEURA_WINDOWS_DEV)
        add_test(NAME project-dev COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/project/dev.py"
            "$<TARGET_FILE:neura>" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/project-dev-tests")
        set_tests_properties(project-dev PROPERTIES TIMEOUT 600)
    endif()
    # `neura package` (tests/project/package.py); the desktop package only with the pinned engine.
    add_test(NAME project-package COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/project/package.py"
        "$<TARGET_FILE:neura>" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/project-package-tests")
    set_tests_properties(project-package PROPERTIES TIMEOUT 900)
    if(NEURA_DESKTOP_ENGINE)
        set_tests_properties(project-package PROPERTIES ENVIRONMENT "NEURA_DESKTOP_ENGINE=${NEURA_DESKTOP_ENGINE}")
    endif()

    if(TARGET neura_launcher AND NOT WIN32)
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

    add_test(NAME parser-if-initializers COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/expressions/if_initializers.py" "$<TARGET_FILE:neura>")

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

    add_test(NAME parser-dot-tags COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tags/dot_tags.py" "$<TARGET_FILE:neura>")

    add_test(NAME source-tag-style COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-tag-style.py" --neura "$<TARGET_FILE:neura>")

    add_test(NAME source-docs COMMAND "${CMAKE_COMMAND}"
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/source-docs-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source-docs.cmake"
    )
    # Editor protocol contracts execute real compiler and runtime paths.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(TARGET neura_http_listener_foreground)
        set(NEURA_SERVER_LISTENER_TEST_OPTIONS)
        if(NEURA_ENABLE_SERVER_TLS)
            find_program(NEURA_SERVER_TEST_OPENSSL NAMES openssl REQUIRED)
            list(APPEND NEURA_SERVER_LISTENER_TEST_OPTIONS --openssl "${NEURA_SERVER_TEST_OPENSSL}")
        endif()
        add_test(NAME server-http-listeners COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/server/http_listener_foreground.py"
            "$<TARGET_FILE:neura_http_listener_foreground>" ${NEURA_SERVER_LISTENER_TEST_OPTIONS})
        set_tests_properties(server-http-listeners PROPERTIES TIMEOUT 20)
    endif()
    if(TARGET neura-server-host)
        # A Neura server image under the server host: HTTP, HTTPS when the
        # build has TLS, malformed and oversized input, limits, traps,
        # record and replay, the drain, and 1,000 requests of load.
        set(NEURA_SERVER_HOST_TEST_OPTIONS)
        if(NEURA_ENABLE_SERVER_TLS)
            find_program(NEURA_SERVER_TEST_OPENSSL NAMES openssl REQUIRED)
            list(APPEND NEURA_SERVER_HOST_TEST_OPTIONS --tls --openssl "${NEURA_SERVER_TEST_OPENSSL}")
        endif()
        find_program(NEURA_SERVER_TEST_CURL NAMES curl)
        if(NEURA_SERVER_TEST_CURL)
            list(APPEND NEURA_SERVER_HOST_TEST_OPTIONS --curl "${NEURA_SERVER_TEST_CURL}")
        endif()
        if(TARGET neura_http2_test_client)
            list(APPEND NEURA_SERVER_HOST_TEST_OPTIONS --http2-client "$<TARGET_FILE:neura_http2_test_client>")
        endif()
        add_test(NAME server-host COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/server/server_host_test.py"
            --neura "$<TARGET_FILE:neura>" --host "$<TARGET_FILE:neura-server-host>"
            --source "${CMAKE_CURRENT_SOURCE_DIR}" --work "${CMAKE_CURRENT_BINARY_DIR}/server-host-test"
            ${NEURA_SERVER_HOST_TEST_OPTIONS})
        set_tests_properties(server-host PROPERTIES TIMEOUT 300)
        if(TARGET neura_sql)
            # Std.Data.Sql under the server host (docs/runtime/Sql.md): the
            # CRUD example with curl, leases, deadlines, migrations and
            # replay on SQLite; the same against PostgreSQL 16, 17 and 18 in
            # Docker, skipped without Docker or the PostgreSQL provider.
            set(NEURA_SQL_TEST_OPTIONS)
            if(NEURA_SERVER_TEST_CURL)
                list(APPEND NEURA_SQL_TEST_OPTIONS --curl "${NEURA_SERVER_TEST_CURL}")
            endif()
            if(NEURA_ENABLE_POSTGRES)
                list(APPEND NEURA_SQL_TEST_OPTIONS --postgres-build --openssl "${NEURA_SERVER_TEST_OPENSSL}")
            endif()
            add_test(NAME server-sql COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/server/sql_host_test.py"
                --neura "$<TARGET_FILE:neura>" --host "$<TARGET_FILE:neura-server-host>"
                --source "${CMAKE_CURRENT_SOURCE_DIR}" --work "${CMAKE_CURRENT_BINARY_DIR}/server-sql-test"
                ${NEURA_SQL_TEST_OPTIONS})
            set_tests_properties(server-sql PROPERTIES TIMEOUT 300)
            # Registered only with the PostgreSQL provider: a build without it
            # would report the entry as skipped, which the CI report refuses.
            if(NEURA_ENABLE_POSTGRES)
                add_test(NAME server-sql-postgres COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/server/sql_host_test.py" --postgres
                    --neura "$<TARGET_FILE:neura>" --host "$<TARGET_FILE:neura-server-host>"
                    --source "${CMAKE_CURRENT_SOURCE_DIR}" --work "${CMAKE_CURRENT_BINARY_DIR}/server-sql-postgres-test"
                    ${NEURA_SQL_TEST_OPTIONS})
                set_tests_properties(server-sql-postgres PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77)
            endif()
        endif()
        if(TARGET neura_http2_test_client)
            # The same suite with request() over HTTP/2 (h2c, prior knowledge).
            add_test(NAME server-host-h2c COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/server/server_host_test.py"
                --neura "$<TARGET_FILE:neura>" --host "$<TARGET_FILE:neura-server-host>"
                --source "${CMAKE_CURRENT_SOURCE_DIR}" --work "${CMAKE_CURRENT_BINARY_DIR}/server-host-h2c-test"
                ${NEURA_SERVER_HOST_TEST_OPTIONS} --protocol h2c)
            set_tests_properties(server-host-h2c PROPERTIES TIMEOUT 300)
        endif()
    endif()
    foreach(suite IN ITEMS formatter_tests lsp_format_regressions test_protocol lsp_protocol lsp_editor_regressions lsp_parameters lsp_hooks lsp_scaling refactor_protocol debug_protocol coverage release_gate image_inspection)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/${suite}.py")
            add_test(NAME tooling-${suite} COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/${suite}.py" "$<TARGET_FILE:neura>")
            set_tests_properties(tooling-${suite} PROPERTIES TIMEOUT 180)
        endif()
    endforeach()
    add_executable(neura_coverage_tests tests/tooling/coverage_unit.cpp)
    target_link_libraries(neura_coverage_tests PRIVATE neura_compiler)
    neura_target_defaults(neura_coverage_tests)
    # The dispatcher and its tools (docs/design/Products030.md): every command
    # directly and through neura, missing and mixed components, and the
    # build tree against an install on PATH. The mixed dispatcher claims
    # another build in its dispatch token.
    # The Windows dispatcher starts tools through tool.cpp (tool::argv_of).
    add_executable(neura_test_mixed_dispatcher src/cli/main.cpp src/cli/tool_registry.cpp
        $<$<PLATFORM_ID:Windows>:src/cli/tool.cpp>)
    target_include_directories(neura_test_mixed_dispatcher PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(neura_test_mixed_dispatcher PRIVATE neura_platform)
    target_compile_definitions(neura_test_mixed_dispatcher PRIVATE
        "NEURA_DISPATCH_VERSION_OVERRIDE=\"neura 0.0.0-mixed (test, commit test)\"")
    set_target_properties(neura_test_mixed_dispatcher PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-tools")
    neura_target_defaults(neura_test_mixed_dispatcher)
    add_test(NAME tools-parity COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/dispatch.py" parity "$<TARGET_FILE_DIR:neura>")
    add_test(NAME tools-components COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/dispatch.py" components "$<TARGET_FILE_DIR:neura>"
        "$<TARGET_FILE:neura_test_mixed_dispatcher>")
    add_test(NAME tools-install COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/dispatch.py" install "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_COMMAND}")
    set_tests_properties(tools-parity tools-components tools-install PROPERTIES TIMEOUT 600)

    add_test(NAME tooling-coverage-unit COMMAND neura_coverage_tests)
    add_executable(neura_test_output_tests tests/tooling/test_output_unit.cpp)
    target_link_libraries(neura_test_output_tests PRIVATE neura_compiler)
    neura_target_defaults(neura_test_output_tests)
    add_test(NAME tooling-test-output COMMAND neura_test_output_tests)
    # Every Neura example in the documentation is compiled or run.
    add_test(NAME doc-examples COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-examples.py"
        --neura "$<TARGET_FILE:neura>" --root "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    # A few generated programs, each compared across every execution mode and under limit sweeps (scripts/program-fuzz.py).
    add_test(NAME program-fuzz COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/program-fuzz.py" run
        --neura "$<TARGET_FILE:neura>" --quick --jobs 3
    )
    # The quick fuzz run takes a few minutes in a Release build. Under the
    # address and undefined-behavior sanitizers every run is several times
    # slower, so that configuration sets a longer limit.
    set(NEURA_PROGRAM_FUZZ_TEST_TIMEOUT 600 CACHE STRING
        "CTest limit, in seconds, for the program-fuzz test")
    set_tests_properties(program-fuzz PROPERTIES TIMEOUT "${NEURA_PROGRAM_FUZZ_TEST_TIMEOUT}")
    add_test(NAME nir-link COMMAND "${CMAKE_COMMAND}"
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/nir-link-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/nir-link.cmake"
    )

    add_test(NAME typed-numeric-literals COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/expressions/typed-numeric-literals.py" "$<TARGET_FILE:neura>")

    add_test(NAME nil-conditions COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/expressions/nil_conditions.py" "$<TARGET_FILE:neura>")

    # Grouped parameters and the constructor shorthand build the image of
    # their written-out form (docs/design/Parameters030.md).
    add_test(NAME parameters-equivalence COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/parameters/equivalence.py" "$<TARGET_FILE:neura>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/parameters/equivalence")
    set_tests_properties(parameters-equivalence PROPERTIES TIMEOUT 180)
    # Programs of the matching milestone (docs/design/Matching030.md), each
    # run in every build route at the default tier and at tier 0.
    add_test(NAME matching-programs COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/matching/programs.py" "$<TARGET_FILE:neura>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/matching/programs")
    set_tests_properties(matching-programs PROPERTIES TIMEOUT 180)
    # Programs of the lifetime milestone (docs/design/Lifetime030.md), each
    # in every build and execution mode with the same output, steps and peak
    # cells, and their NeuraIR version.
    add_test(NAME lifetime-programs COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/lifetime/programs.py" "$<TARGET_FILE:neura>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/lifetime/programs")
    set_tests_properties(lifetime-programs PROPERTIES TIMEOUT 900)
    # E0475 and E0550 when a program is built from a .nir module whose call
    # was retargeted after lowering checked it.
    add_test(NAME lifetime-nir-checks COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/lifetime/nir_checks.py" "$<TARGET_FILE:neura>")
    add_test(NAME matching-nir COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/matching/nir_versions.py" "$<TARGET_FILE:neura>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/matching/programs")
    # Records, copies, property contracts, static requirements and derivation
    # (docs/design/Records030.md): generated members against their written-out
    # forms, behaviour in every build and tier, and the NeuraIR boundary.
    add_test(NAME records-equivalence COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/parameters/equivalence.py" "$<TARGET_FILE:neura>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/records/equivalence")
    set_tests_properties(records-equivalence PROPERTIES TIMEOUT 300)
    # The 0.3.0 expression forms (docs/design/Expressions030.md) against the
    # forms they are written out to, and programs that use them.
    add_test(NAME expressions-equivalence COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/parameters/equivalence.py" "$<TARGET_FILE:neura>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/expressions/equivalence")
    set_tests_properties(expressions-equivalence PROPERTIES TIMEOUT 300)
    # A range literal as a for-each source is counted without a region.
    add_test(NAME expressions-range-loop-stack COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/expressions/range-loop-stack.py" "$<TARGET_FILE:neura>")
    set_tests_properties(expressions-range-loop-stack PROPERTIES TIMEOUT 300)
    add_test(NAME records-boundary COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/records/records.py" "$<TARGET_FILE:neura>")
    set_tests_properties(records-boundary PROPERTIES TIMEOUT 300)
    add_test(NAME records-codecs COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/records/codecs.py" "$<TARGET_FILE:neura>")
    set_tests_properties(records-codecs PROPERTIES TIMEOUT 300)
    # Std.Reflect (docs/design/Reflect030.md) in every build, from .nir, without
    # names, deterministically, and across a .nir module.
    add_test(NAME reflect-boundary COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/reflect/reflect.py" "$<TARGET_FILE:neura>")
    set_tests_properties(reflect-boundary PROPERTIES TIMEOUT 900)
    # The compile-time check of interpolated strings agrees with the
    # formatting library on a corpus of specifications (docs/design/Calls030.md).
    add_test(NAME calls-format-corpus COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/calls/format_corpus.py" "$<TARGET_FILE:neura>")
    set_tests_properties(calls-format-corpus PROPERTIES TIMEOUT 300)

    # The NVR class hook operations in every execution mode, the loader's
    # rejections, and the assembler and decompiler (docs/design/Hooks030.md).
    add_test(NAME hook-operations COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/hooks/operations.py" "$<TARGET_FILE:neura>"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME hook-references COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/hooks/references.py" "$<TARGET_FILE:neura>")
    set_tests_properties(hook-operations hook-references PROPERTIES TIMEOUT 300)

    add_executable(neura_island_lexer_tests tests/lexer/html_island_tests.cpp)
    target_link_libraries(neura_island_lexer_tests PRIVATE neura_compiler)
    neura_target_defaults(neura_island_lexer_tests)
    add_test(NAME lexer-islands COMMAND neura_island_lexer_tests)

    add_executable(neura_html_template_tests tests/parser/html_template_tests.cpp)
    target_link_libraries(neura_html_template_tests PRIVATE neura_compiler)
    neura_target_defaults(neura_html_template_tests)
    add_test(NAME parser-html-templates COMMAND neura_html_template_tests)

    add_executable(neura_css_stylesheet_tests tests/parser/css_stylesheet_tests.cpp)
    target_link_libraries(neura_css_stylesheet_tests PRIVATE neura_compiler)
    neura_target_defaults(neura_css_stylesheet_tests)
    add_test(NAME parser-css-stylesheets COMMAND neura_css_stylesheet_tests)

    add_test(NAME app-html-typed COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/apps/html/typed.py" "$<TARGET_FILE:neura>")
    set_tests_properties(app-html-typed PROPERTIES TIMEOUT 180)
    add_test(NAME app-frozen-storage COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/apps/frozen/storage.py" "$<TARGET_FILE:neura>")
    set_tests_properties(app-frozen-storage PROPERTIES TIMEOUT 180)
    add_test(NAME app-ui-retained COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/apps/ui/retained.py" "$<TARGET_FILE:neura>")
    set_tests_properties(app-ui-retained PROPERTIES TIMEOUT 180)
    add_test(NAME app-html-styles COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/apps/html/styles.py" "$<TARGET_FILE:neura>")
    set_tests_properties(app-html-styles PROPERTIES TIMEOUT 180)
    add_test(NAME app-ui-reconcile COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/apps/ui-ops/reconcile.py" "$<TARGET_FILE:neura>")
    set_tests_properties(app-ui-reconcile PROPERTIES TIMEOUT 900)
    add_test(NAME app-ui-trace COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/apps/ui-ops/trace.py" "$<TARGET_FILE:neura>")
    set_tests_properties(app-ui-trace PROPERTIES TIMEOUT 180)
    # A Neura application in headless Chromium and Firefox through Std.Web.mount. It needs the browser
    # runtime of the wasm-runtime preset (configure with -DNEURA_WASM_HOST=ON, build the neura_wasm target),
    # Node, and the browsers. The two checks are registered only when that runtime exists when this build
    # is configured, so that a build without it does not report them as skipped (CI refuses skipped tests);
    # without Node or a browser they are still skipped.
    set(NEURA_WEB_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/wasm-runtime" CACHE PATH
        "Directory holding nvm.wasm and nvm.mjs for the browser checks")
    if(EXISTS "${NEURA_WEB_RUNTIME_DIR}/nvm.wasm")
        add_test(NAME app-web-browser COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/apps/web/app-check.py" --neura "$<TARGET_FILE:neura>"
            --wasm "${NEURA_WEB_RUNTIME_DIR}" --browser chromium --browser firefox)
        set_tests_properties(app-web-browser PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
        # The front end of examples/apps/web-backend calling the server image of examples/server through a
        # proxy; it also needs neura-server-host beside neura, and is skipped without it.
        add_test(NAME app-web-backend COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/apps/web/backend-check.py" --neura "$<TARGET_FILE:neura>"
            --wasm "${NEURA_WEB_RUNTIME_DIR}" --browser chromium --browser firefox)
        set_tests_properties(app-web-backend PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
        # The reference project's page from `neura build --target web` and from `neura dev`, at a deep path and
        # with a wrong media type for nvm.wasm (tests/project/browser-check.py).
        add_test(NAME project-web-browser COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/project/browser-check.py" --neura "$<TARGET_FILE:neura>"
            --wasm "${NEURA_WEB_RUNTIME_DIR}" --browser chromium --browser firefox)
        set_tests_properties(project-web-browser PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
    endif()

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
        tests/ir-optimization/containment_tests.cpp
        tests/ir-optimization/storage_tests.cpp
        tests/ir-optimization/summaries_tests.cpp
        tests/ir-optimization/evaluator.cpp
        tests/ir-optimization/float_reference.cpp
        tests/ir-optimization/hooks_tests.cpp
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
    foreach(suite IN ITEMS structure verifier behavior concurrency hooks)
        add_test(NAME ir-optimization-${suite}
            COMMAND neura_ir_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()

    # The x86-64 encoder of tier 3 (src/jit/x64.cpp): every instruction form
    # against an assembler's bytes, labels and relaxation, and small functions
    # run on x86-64 Linux. It links the runtime for jit::Program only.
    add_executable(neura_x64_tests tests/jit/x64_tests.cpp)
    target_link_libraries(neura_x64_tests PRIVATE neura_nvm)
    target_include_directories(neura_x64_tests PRIVATE tests)
    neura_target_defaults(neura_x64_tests)
    add_test(NAME backend-x64 COMMAND neura_x64_tests)

    # The register allocator of tier 3 (src/jit/regalloc.cpp) and its
    # independent checker (src/jit/mir_check.cpp): hand-built functions,
    # random functions checked by the checker and allocated twice for
    # determinism, the move sequencer, and allocations the checker must reject.
    add_executable(neura_regalloc_tests tests/jit/regalloc_tests.cpp)
    target_link_libraries(neura_regalloc_tests PRIVATE neura_nvm)
    target_include_directories(neura_regalloc_tests PRIVATE tests)
    neura_target_defaults(neura_regalloc_tests)
    add_test(NAME backend-regalloc COMMAND neura_regalloc_tests)

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
        tests/backend/heap_executor_tests.cpp
        tests/backend/heap_tests.cpp
        tests/backend/host_tests.cpp
        tests/backend/layered_tests.cpp
        tests/backend/nir_tests.cpp
        tests/backend/placement_tests.cpp
        tests/backend/reference_tests.cpp
        tests/backend/tier_tests.cpp
        tests/backend/tier2_tests.cpp
        tests/backend/tier_programs_tests.cpp
        tests/backend/tier_shadow_tests.cpp
        tests/backend/tier3_float_tests.cpp
        tests/backend/tier3_heap_tests.cpp
        tests/backend/tier3_inline_tests.cpp
        tests/backend/tier3_ir_tests.cpp
        tests/backend/tier3_native_tests.cpp
        tests/backend/tier3_pass_tests.cpp
        tests/backend/vm_tests.cpp
        tests/jit/jit_tests.cpp
        tests/jit/tier3_native_tests.cpp
        tests/ir-optimization/concurrency_support.cpp
        tests/ir-optimization/evaluator.cpp
        tests/ir-optimization/float_reference.cpp
        tests/ir-optimization/main.cpp
        tests/ir-optimization/support.cpp
    )
    target_link_libraries(neura_backend_tests PRIVATE neura_compiler neura_nvm neura_decompiler)
    target_include_directories(neura_backend_tests PRIVATE tests)
    neura_target_defaults(neura_backend_tests)
    if(NEURA_ENABLE_SERVER_TLS)
        target_compile_definitions(neura_backend_tests PRIVATE NEURA_NVM_TLS)
        if(WIN32)
            target_link_libraries(neura_backend_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        else()
            target_link_libraries(neura_backend_tests PRIVATE neura_openssl)
        endif()
    endif()

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

    set(backend_suites circuits floats format vm heap host nir conversion generics layered fleet placement concurrency tier tier3 jit)
    if(WIN32)
        # tests/backend/host_tests.cpp drives the host with POSIX sockets,
        # processes and files and holds no case on Windows; see "Tests that
        # run on fewer targets" in docs/ContinuousIntegration.md.
        list(REMOVE_ITEM backend_suites host)
    endif()
    foreach(suite IN LISTS backend_suites)
        add_test(NAME backend-${suite}
            COMMAND neura_backend_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
    # backend-tier compares tier 2 with tier 0 with the default native code
    # (docs/runtime/NVM.md, "Native compilation"); this runs it again with
    # every region on the region interpreter.
    add_test(NAME backend-tier-interpreter COMMAND neura_backend_tests tier "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(backend-tier-interpreter PROPERTIES ENVIRONMENT NEURA_JIT=off)
    # Native tier 3 is the default (docs/runtime/NVM.md, "Execution tiers"), so
    # the suites above exercise function programs; these run the tier 2
    # region suites with tier 3 off, so that a plain ctest covers regions too.
    foreach(suite IN ITEMS tier jit)
        add_test(NAME backend-${suite}-off COMMAND neura_backend_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(backend-${suite}-off PROPERTIES ENVIRONMENT NEURA_TIER3=off)
    endforeach()
    # Tier 3's checkable variant under the region check of NEURA_TIER_CHECK=1.
    add_test(NAME backend-tier3-check COMMAND neura_backend_tests tier3_check "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(backend-tier3-check PROPERTIES ENVIRONMENT "NEURA_TIER_CHECK=1;NEURA_TIER3=interp")
    # This fixture examines the result join in the native Tier 3 IR dump.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND neura_host_arch STREQUAL "x86_64")
        add_test(NAME backend-tier3-nil-result COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/bench-wall/check-tier-invariant.py"
            --candidate "$<TARGET_FILE:neura>" --fixture nil-result)
        set_tests_properties(backend-tier3-nil-result PROPERTIES TIMEOUT 180)
    endif()

    add_test(NAME backend-each COMMAND "${CMAKE_COMMAND}"
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DNEURA_HAS_GPU=${NEURA_HAS_GPU}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/backend-each-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/each.cmake"
    )

    # Standard library tests under the reference evaluator and NVM.
    add_test(NAME backend-reference COMMAND neura_backend_tests reference "${CMAKE_CURRENT_SOURCE_DIR}")
    # The suite takes about 1470 seconds on GitHub's Intel macOS runners, which
    # is too close to CTest's default limit of 1500 seconds. Under the address
    # and undefined-behavior sanitizers it takes more than an hour on a GitHub
    # runner, so that configuration sets a longer limit. A limit set here takes
    # precedence over ctest --timeout.
    set(NEURA_REFERENCE_TEST_TIMEOUT 3600 CACHE STRING
        "CTest limit, in seconds, for the backend-reference test")
    set_tests_properties(backend-reference PROPERTIES TIMEOUT "${NEURA_REFERENCE_TEST_TIMEOUT}")

    foreach(suite IN ITEMS lossless lift behavior)
        add_test(NAME decompiler-${suite}
            COMMAND neura_backend_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
    # decompiler-behavior takes a few minutes in a Release build and more than
    # an hour under the address and undefined-behavior sanitizers, so that
    # configuration sets a longer limit, as it does for backend-reference.
    set(NEURA_DECOMPILER_BEHAVIOR_TEST_TIMEOUT 3600 CACHE STRING
        "CTest limit, in seconds, for the decompiler-behavior test")
    set_tests_properties(decompiler-behavior PROPERTIES TIMEOUT "${NEURA_DECOMPILER_BEHAVIOR_TEST_TIMEOUT}")

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
    foreach(suite IN ITEMS proof overflow execute corpus floats)
        add_test(NAME lift-${suite} COMMAND neura_lift_tests ${suite} "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
    # Graphs and proofs that do not depend on the C++ compiler (tests/lift/portable.cmake).
    add_test(NAME lift-portable COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/lift-portable"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/lift/portable.cmake")

    add_test(NAME backend-cli COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/backend-tests"
        "-DTLS_ENABLED=${NEURA_ENABLE_SERVER_TLS}"
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
        # Script mode has no CMAKE_EXECUTABLE_SUFFIX of its own (neura.exe on Windows).
        "-DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
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
    if(WIN32)
        # The same names on Windows, which the tests and their '// args:'
        # lines use: libmathops.so is the DLL, which exports every function of
        # mathops.c, and libmathops.a the static COFF archive. The DLL's
        # import library takes another name, mathops-import.lib.
        set_target_properties(neura_test_mathops_shared PROPERTIES
            PREFIX "lib"
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            ARCHIVE_OUTPUT_NAME mathops-import)
        set_target_properties(neura_test_mathops_static PROPERTIES PREFIX "lib" SUFFIX ".a")
    endif()
    set(neura_native_test_dir "${CMAKE_CURRENT_BINARY_DIR}/native-tests")
    add_test(NAME backend-capabilities COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/backend-capabilities-tests"
        "-DMATHOPS=$<TARGET_FILE:neura_test_mathops_shared>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend/capabilities.cmake")
    if(TARGET neura_launcher AND NOT WIN32)
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
    elseif(TARGET neura_launcher)
        # The PE launcher's tests (cmake/windows/launchers.cmake).
        neura_windows_hook(launcher_tests)
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
        # Script mode has no CMAKE_EXECUTABLE_SUFFIX of its own (neura.exe on Windows).
        "-DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/install.cmake"
    )
    set_tests_properties(stdlib-install PROPERTIES FIXTURES_SETUP stdlib)
    if(NEURA_ENABLE_SERVER_TLS)
        find_program(NEURA_SERVER_TEST_OPENSSL NAMES openssl REQUIRED)
        add_test(NAME stdlib-net-tls-loopback COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/net-tls/loopback.py"
            --neura "$<TARGET_FILE:neura>" --openssl "${NEURA_SERVER_TEST_OPENSSL}")
    endif()
    file(GLOB stdlib_areas LIST_DIRECTORIES true CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/*")
    foreach(area_dir IN LISTS stdlib_areas)
        if(NOT IS_DIRECTORY "${area_dir}")
            continue()
        endif()
        get_filename_component(area "${area_dir}" NAME)
        # Std.Data.Sql's tests need the SQL providers (--sql-pool).
        if(area STREQUAL "sql" AND NOT TARGET neura_sql)
            continue()
        endif()
        # The test files that run as entries of their own, stdlib-<area>-<file>:
        # without the JIT, one entry of the whole area ran for hours, alone at
        # the end of the slower targets' suites.
        set(split_files)
        if(area STREQUAL "crypto-pqc")
            set(split_files slh-dsa-sign-fast-tests slh-dsa-sign-sha2-small-tests slh-dsa-sign-shake-small-tests)
        endif()
        string(REPLACE ";" "," skipped "${split_files}")
        add_test(NAME stdlib-${area} COMMAND ${CMAKE_COMMAND}
            "-DNEURA=${CMAKE_CURRENT_BINARY_DIR}/stdlib-install/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/stdlib-tests/${area}"
            "-DAREA=${area}"
            "-DNATIVE_TEST_DIR=${neura_native_test_dir}"
            "-DSKIP_FILES=${skipped}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/area.cmake"
        )
        set_tests_properties(stdlib-${area} PROPERTIES FIXTURES_REQUIRED stdlib)
        foreach(file IN LISTS split_files)
            string(REGEX REPLACE "-tests$" "" part "${file}")
            add_test(NAME stdlib-${area}-${part} COMMAND ${CMAKE_COMMAND}
                "-DNEURA=${CMAKE_CURRENT_BINARY_DIR}/stdlib-install/prefix/bin/neura${CMAKE_EXECUTABLE_SUFFIX}"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/stdlib-tests/${area}-${part}"
                "-DAREA=${area}"
                "-DNATIVE_TEST_DIR=${neura_native_test_dir}"
                "-DONLY_FILE=${file}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/area.cmake"
            )
            set_tests_properties(stdlib-${area}-${part} PROPERTIES FIXTURES_REQUIRED stdlib)
        endforeach()
    endforeach()

    # The desktop window host and its Neura native child (docs/design/Desktop030.md). These tests need the
    # pinned Electron 45.3.3 of hosts/desktop/engine.lock, Node and xvfb-run, so they are registered only when
    # NEURA_DESKTOP_ENGINE names that Electron executable. Each starts X servers and Electron processes, so they
    # run one at a time.
    set(NEURA_DESKTOP_ENGINE "" CACHE FILEPATH
        "The pinned Electron 45.3.3 executable; when set, the desktop tests are registered")
    if(NEURA_DESKTOP_ENGINE)
        if(NOT EXISTS "${NEURA_DESKTOP_ENGINE}")
            message(FATAL_ERROR "NEURA_DESKTOP_ENGINE names no file: ${NEURA_DESKTOP_ENGINE}")
        endif()
        find_program(NEURA_DESKTOP_NODE node REQUIRED)
        if(NOT WIN32)
            # Windows has a desktop session of its own; X servers are for POSIX.
            find_program(NEURA_DESKTOP_XVFB_RUN xvfb-run REQUIRED)
        endif()
        set(neura_desktop_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/desktop")
        add_test(NAME desktop-native-child COMMAND "${NEURA_DESKTOP_NODE}" --test
            "${neura_desktop_tests}/native_child_test.js")
        add_test(NAME desktop-shell COMMAND "${Python3_EXECUTABLE}" "${neura_desktop_tests}/run_shell_test.py"
            "${NEURA_DESKTOP_ENGINE}" --case shell --case child-mismatch --case child-close --case child-quit
            --case child-crash)
        add_test(NAME desktop-app COMMAND "${Python3_EXECUTABLE}" "${neura_desktop_tests}/run_app_test.py"
            "${NEURA_DESKTOP_ENGINE}" --neura "$<TARGET_FILE:neura>")
        add_test(NAME desktop-package COMMAND "${Python3_EXECUTABLE}" "${neura_desktop_tests}/run_package_test.py"
            "${NEURA_DESKTOP_ENGINE}" --neura "$<TARGET_FILE:neura>")
        set_tests_properties(desktop-native-child desktop-shell desktop-app desktop-package PROPERTIES
            RUN_SERIAL TRUE LABELS desktop WORKING_DIRECTORY "${neura_desktop_tests}")
        set_tests_properties(desktop-app desktop-package PROPERTIES TIMEOUT 1200)
    else()
        message(STATUS "NEURA_DESKTOP_ENGINE is not set: the desktop tests are not registered "
            "(desktop-native-child, desktop-shell, desktop-app, desktop-package)")
    endif()

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

    # The runtime service (docs/design/Service030.md): the protocol codec.
    add_executable(neura_service_protocol_tests tests/service/protocol_tests.cpp)
    target_link_libraries(neura_service_protocol_tests PRIVATE neura_service_protocol)
    neura_target_defaults(neura_service_protocol_tests)
    add_test(NAME service-unit-protocol COMMAND neura_service_protocol_tests)
    if(NOT WIN32 OR NEURA_WINDOWS_SERVICE)
        # The cached environment readers abort while the environment is held.
        add_executable(neura_service_environment_tests tests/service/environment_tests.cpp)
        target_link_libraries(neura_service_environment_tests PRIVATE neura_nvm)
        neura_target_defaults(neura_service_environment_tests)
        add_test(NAME service-unit-environment COMMAND neura_service_environment_tests)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # A test driver that plays the supervisor of one worker. The suites
        # read /proc and use memfd_create: Linux is the qualified platform.
        # On macOS the service builds but these suites do not run.
        add_executable(neura_service_worker_driver tests/service/worker_driver.cpp)
        target_link_libraries(neura_service_worker_driver PRIVATE neura_cli_core)
        target_include_directories(neura_service_worker_driver PRIVATE src)
        neura_target_defaults(neura_service_worker_driver)
        add_test(NAME service-worker COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/service/service.py" worker "$<TARGET_FILE_DIR:neura>"
            "$<TARGET_FILE:neura_service_worker_driver>")
        set_tests_properties(service-worker PROPERTIES TIMEOUT 600)
        # The service end to end, one suite per test (tests/service/service.py).
        foreach(suite IN ITEMS cli-options off-default protocol run parity control login isolation)
            add_test(NAME service-${suite} COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/service/service.py" ${suite} "$<TARGET_FILE_DIR:neura>")
            set_tests_properties(service-${suite} PROPERTIES TIMEOUT 600)
            # A GPU build's refusal of a graph device names --gpu=off.
            if(NEURA_HAS_GPU)
                set_tests_properties(service-${suite} PROPERTIES ENVIRONMENT "NEURA_TEST_GPU_BUILD=1")
            endif()
        endforeach()
    elseif(WIN32 AND NEURA_WINDOWS_SERVICE)
        neura_windows_hook(service_tests)
    endif()

    # The install lifecycle (docs/Install.md): version source, the data
    # formats, and the commands against an install made from a release
    # archive. None needs the network, root, or a terminal.
    add_executable(neura_lifecycle_tests tests/lifecycle/unit_tests.cpp)
    target_link_libraries(neura_lifecycle_tests PRIVATE neura_lifecycle neura_cli_registry)
    neura_target_defaults(neura_lifecycle_tests)
    foreach(suite IN ITEMS sha256 semver release-metadata marker manifest journal generation lease)
        add_test(NAME lifecycle-unit-${suite} COMMAND neura_lifecycle_tests ${suite})
    endforeach()
    add_test(NAME version-consistency COMMAND ${CMAKE_COMMAND}
        "-DNEURA=$<TARGET_FILE:neura>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle/version.cmake"
    )
    # The scenario suites of the 0.3 lifecycle (docs/design/Lifecycle030.md).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    foreach(suite IN ITEMS archives install bootstrap update migrate doctor components uninstall service neurad)
        add_test(NAME lifecycle-${suite} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle/scenarios.py" ${suite}
            "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/lifecycle-${suite}-tests" "${CMAKE_COMMAND}")
        set_tests_properties(lifecycle-${suite} PROPERTIES TIMEOUT 900)
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

# A slower target (the CI runners of docs/ContinuousIntegration.md, "Time
# limits") multiplies every test's time limit by one whole number instead of
# editing each limit. A test without a TIMEOUT property has CTest's default,
# DART_TESTING_TIMEOUT, which is multiplied too. The scale is also given to
# each test as NEURA_TEST_TIMEOUT_SCALE, for suites with limits of their own.
set(NEURA_TEST_TIMEOUT_SCALE 1 CACHE STRING
    "A whole number every test's time limit is multiplied by, for slower machines")
if(BUILD_TESTING)
    if(NOT NEURA_TEST_TIMEOUT_SCALE MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "NEURA_TEST_TIMEOUT_SCALE must be a whole number of 1 or more, not '${NEURA_TEST_TIMEOUT_SCALE}'")
    endif()
    set(neura_default_timeout 1500)
    if(DART_TESTING_TIMEOUT MATCHES "^[1-9][0-9]*$")
        set(neura_default_timeout "${DART_TESTING_TIMEOUT}")
    endif()
    # In the submodule's directory a test property is addressed with
    # DIRECTORY, which CMake 3.28 introduced.
    function(neura_scale_test_timeouts)
        get_property(tests DIRECTORY ${ARGN} PROPERTY TESTS)
        if(ARGN)
            set(where DIRECTORY ${ARGN})
        endif()
        foreach(test IN LISTS tests)
            get_property(limit TEST "${test}" ${where} PROPERTY TIMEOUT)
            if(NOT limit)
                set(limit "${neura_default_timeout}")
            endif()
            math(EXPR limit "${limit} * ${NEURA_TEST_TIMEOUT_SCALE}")
            set_property(TEST "${test}" ${where} PROPERTY TIMEOUT "${limit}")
            set_property(TEST "${test}" ${where}
                APPEND PROPERTY ENVIRONMENT "NEURA_TEST_TIMEOUT_SCALE=${NEURA_TEST_TIMEOUT_SCALE}")
        endforeach()
    endfunction()
    neura_scale_test_timeouts()
    # The longest tests start first under ctest --parallel, so they overlap
    # the many short ones instead of running alone at the end. The costs are
    # their seconds on the x86_64 Linux CI runner (release trial 16).
    set(neura_test_costs
        backend-reference 2640 lifetime-programs 900 stdlib-crypto-pqc 300 stdlib-crypto-pqc-slh-dsa-sign-shake-small 400
        stdlib-crypto-pqc-slh-dsa-sign-sha2-small 300 stdlib-crypto-pqc-slh-dsa-sign-fast 200 decompiler-behavior 540
        stdlib-crypto-asymmetric 480 app-ui-reconcile 450 stdlib-character 390 backend-tier3 380
        program-fuzz 370 lifecycle-update 360 decompiler-lift 350 stdlib-crypto-pki 330
        expressions-equivalence 300 stdlib-crypto-advanced 290 lift-corpus 260 stdlib-crypto-token 250
        backend-placement 240 ir-optimization-behavior 220 lift-floats 175 backend-fleet 150
        stdlib-crypto-enclave 150)
    while(neura_test_costs)
        list(POP_FRONT neura_test_costs test cost)
        if(TEST "${test}")
            set_property(TEST "${test}" PROPERTY COST "${cost}")
        endif()
    endwhile()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28" AND NEURA_SDK)
        neura_scale_test_timeouts("${NEURA_LIBCLANG_DIR}")
    endif()
endif()
