# The native Windows build (x86_64-windows-msvc; docs/design/Windows030.md).
# CMakeLists.txt includes this file right after project() when WIN32, so the
# settings below apply to every target, the libclang submodule's included.
# It holds everything that is Windows-wide: the toolchain check, the C runtime,
# compiler and linker settings, resources, libffi, the product layout (W04) and
# the hook points of the gated areas. Each such area is a cmake/windows/<area>.cmake
# file owned by one package, which defines only neura_windows_<hook> macros
# and NEURA_WINDOWS_<AREA> variables; without the file the area is absent.

if(CMAKE_VERSION VERSION_LESS "3.24")
    message(FATAL_ERROR "A Windows build of Neura needs CMake 3.24 or later, not ${CMAKE_VERSION}")
endif()
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "A Windows build of Neura needs cl or clang-cl for x86-64 (MinGW and Cygwin are not "
        "supported); run CMake from a Developer PowerShell for VS 2022 or use a windows-* preset")
endif()

set(NEURA_WINDOWS_DIR "${CMAKE_CURRENT_LIST_DIR}")

# The version and manifest resources of the executables and DLLs.
enable_language(RC)

# The static C runtime for every binary (W05): an installation needs no
# Visual C++ redistributable, and the C boundary of the DLLs (W04) lets each
# module carry its own copy.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# UNICODE is not defined: Windows code calls the W functions by name.
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
# Source files are UTF-8 (W06); without /utf-8, cl reads them in the ANSI
# code page and silently changes non-ASCII literals. The resource compiler
# takes none of these options.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>"
    "$<$<COMPILE_LANGUAGE:CXX>:/bigobj;/Zc:__cplusplus;/permissive->"
    # clang-cl's preprocessor conforms already and warns about the option.
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/Zc:preprocessor>")

# The warnings of Neura's C++ targets (neura_target_defaults): /W4 without the
# warnings that the GCC and Clang set (-Wall -Wextra -Wpedantic) does not
# enable either, conversions and shadowing among them.
set(NEURA_WINDOWS_WARNINGS /W4 /wd4244 /wd4267 /wd4456 /wd4457 /wd4458 /wd4459 /wd4127 /wd4702 /wd4324)

# An 8 MiB main stack, the default of std::thread too (W12): with Windows'
# 1 MiB the recursive parser, lowering and the reference evaluator overflow
# where Linux succeeds.
string(APPEND CMAKE_EXE_LINKER_FLAGS " /STACK:8388608")

# A release build is reproducible (/Brepro); its PDBs are CI artifacts and
# never installed, and their RSDS record doubles as the build identity.
# /DEBUG turns /OPT:REF and /OPT:ICF off unless they are named.
if(NEURA_PORTABLE_RELEASE)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/Brepro;/Z7>")
    add_link_options(/Brepro /DEBUG:FULL /OPT:REF /OPT:ICF)
endif()

# neura_windows_link_libffi(<target>): links the libffi of vcpkg (or one that
# find_path and find_library see) to <target>. A release archive must not
# need a libffi DLL, so NEURA_PORTABLE_RELEASE refuses an import library.
function(neura_windows_link_libffi target)
    find_package(unofficial-libffi CONFIG QUIET)
    if(TARGET unofficial::libffi::libffi)
        set(libffi unofficial::libffi::libffi)
        get_target_property(libffi_file unofficial::libffi::libffi IMPORTED_LOCATION_RELEASE)
        if(NOT libffi_file)
            get_target_property(libffi_file unofficial::libffi::libffi IMPORTED_LOCATION)
        endif()
    else()
        find_path(NEURA_LIBFFI_INCLUDE_DIR ffi.h)
        find_library(NEURA_LIBFFI_LIBRARY NAMES ffi libffi)
        if(NOT NEURA_LIBFFI_INCLUDE_DIR OR NOT NEURA_LIBFFI_LIBRARY)
            message(FATAL_ERROR "Std.DynamicForeign needs libffi: configure with the vcpkg toolchain "
                "($env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake, triplet x64-windows-static) or set "
                "NEURA_LIBFFI_INCLUDE_DIR and NEURA_LIBFFI_LIBRARY")
        endif()
        target_include_directories(${target} PRIVATE "${NEURA_LIBFFI_INCLUDE_DIR}")
        set(libffi "${NEURA_LIBFFI_LIBRARY}")
        set(libffi_file "${NEURA_LIBFFI_LIBRARY}")
    endif()
    if(NEURA_PORTABLE_RELEASE)
        # The members of an import library name their DLL; a static archive's
        # objects do not.
        get_filename_component(linker_dir "${CMAKE_LINKER}" DIRECTORY)
        find_program(NEURA_DUMPBIN dumpbin HINTS "${linker_dir}" REQUIRED)
        execute_process(COMMAND "${NEURA_DUMPBIN}" /nologo /headers "${libffi_file}"
            OUTPUT_VARIABLE libffi_headers RESULT_VARIABLE libffi_status)
        if(NOT libffi_status EQUAL 0)
            message(FATAL_ERROR "NEURA_PORTABLE_RELEASE: cannot inspect ${libffi_file} with dumpbin")
        endif()
        if(libffi_headers MATCHES "\\.idata\\$" OR libffi_headers MATCHES "DLL name")
            message(FATAL_ERROR "NEURA_PORTABLE_RELEASE needs a static libffi, but ${libffi_file} is the import "
                "library of a DLL; use the x64-windows-static triplet")
        endif()
    endif()
    target_link_libraries(${target} PRIVATE ${libffi})
endfunction()

# The resources every executable and DLL embeds: the version (VERSIONINFO, from
# NEURA_VERSION_FULL) and, for executables, the application manifest, which
# makes the process code page UTF-8 (argv, getenv and fopen take UTF-8, W07),
# allows long paths and declares Windows 10 and 11.
set(NEURA_RC_VERSION_NUMBERS "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
foreach(kind IN ITEMS app dll)
    if(kind STREQUAL "app")
        set(NEURA_RC_FILE_TYPE VFT_APP)
    else()
        set(NEURA_RC_FILE_TYPE VFT_DLL)
    endif()
    configure_file("${NEURA_WINDOWS_DIR}/neura-version.rc.in"
        "${CMAKE_BINARY_DIR}/generated/windows/neura-version-${kind}.rc" @ONLY)
endforeach()
unset(NEURA_RC_FILE_TYPE)

# neura_windows_embed_resources(<target>): adds the resources to an executable
# or a DLL; other targets are left alone.
function(neura_windows_embed_resources target)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
        target_sources(${target} PRIVATE "${NEURA_WINDOWS_DIR}/neura.manifest"
            "${CMAKE_BINARY_DIR}/generated/windows/neura-version-app.rc")
    elseif(type STREQUAL "SHARED_LIBRARY")
        target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated/windows/neura-version-dll.rc")
    endif()
endfunction()

# --- Product layout (W04) -----------------------------------------------------
# Two self-contained DLLs beside the executables, neura-core-<version>.dll and
# neura-sdk-<version>.dll. Each tool is a stub (src/cli/tool_stub.cpp) whose
# main calls the one function its DLL exports for it, neura_tool_<id>, so a
# DLL exports nothing else, no C++ object crosses a module boundary, and a
# process loads one Neura DLL, keeping one copy of every inline static.

# neura_windows_add_shared(<target> <name> <component> <static libraries>...):
# the DLL <name>-<full version>.dll with every object of the libraries. Its
# import library is for the tool stubs of this build only and is not installed.
function(neura_windows_add_shared target name component)
    add_library(${target} SHARED "${PROJECT_SOURCE_DIR}/src/cli/shared_library.cpp")
    foreach(library IN LISTS ARGN)
        target_link_options(${target} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:${library}>")
    endforeach()
    # The libraries again, for their own dependencies (libffi, system
    # libraries) and so that they are built first.
    target_link_libraries(${target} PRIVATE ${ARGN})
    # The tool entries (neura_windows_add_tool) include the tools' sources.
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${name}-${NEURA_VERSION_FULL}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    neura_target_defaults(${target})
    install(FILES "$<TARGET_FILE:${target}>" DESTINATION bin COMPONENT ${component})
endfunction()

# neura_windows_add_tool(<name> <source> <libraries>...): the tool <name>. Its
# source goes into the product DLL among <libraries> (neura_sdk_library if
# named, else neura_core_library) through a generated entry, which renames its
# main and exports it as neura_tool_<id>; the other libraries are linked into
# that DLL. The executable <name> is the stub that calls the entry.
function(neura_windows_add_tool name source)
    set(library neura_core_library)
    if(neura_sdk_library IN_LIST ARGN)
        set(library neura_sdk_library)
    endif()
    set(extra ${ARGN})
    list(REMOVE_ITEM extra neura_core_library neura_sdk_library)

    string(MAKE_C_IDENTIFIER "${name}" NEURA_TOOL_ID)
    set(NEURA_TOOL_NAME "${name}")
    get_filename_component(NEURA_TOOL_SOURCE "${source}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    set(entry "${CMAKE_BINARY_DIR}/generated/tool-entries/${name}.cpp")
    configure_file("${NEURA_WINDOWS_DIR}/tool_entry.cpp.in" "${entry}" @ONLY)
    target_sources(${library} PRIVATE "${entry}")
    if(extra)
        target_link_libraries(${library} PRIVATE ${extra})
    endif()
    if(NEURA_HAS_GPU)
        set_property(SOURCE "${entry}" APPEND PROPERTY COMPILE_DEFINITIONS NEURA_HAS_GPU=1)
    endif()

    add_executable(${name} "${PROJECT_SOURCE_DIR}/src/cli/tool_stub.cpp")
    target_compile_definitions(${name} PRIVATE NEURA_TOOL_ENTRY=neura_tool_${NEURA_TOOL_ID})
    target_link_libraries(${name} PRIVATE ${library})
    set_target_properties(${name} PROPERTIES NEURA_TOOL_LIBRARY ${library} NEURA_TOOL_ENTRY_SOURCE "${entry}")
    neura_target_defaults(${name})
endfunction()

# --- Gated areas ----------------------------------------------------------------
# neura_windows_hook(<name>): runs the macro neura_windows_<name> when an area
# file defines it. A macro runs in the scope of the hook point, as the POSIX
# text there does.
macro(neura_windows_hook neura_hook_name)
    if(COMMAND neura_windows_${neura_hook_name})
        cmake_language(CALL neura_windows_${neura_hook_name})
    endif()
endmacro()

# neura_windows_required_hook(<name> <area file>): the same for an area that a
# build option asked for, which is an error when no area file provides it.
macro(neura_windows_required_hook neura_hook_name neura_hook_file)
    if(NOT COMMAND neura_windows_${neura_hook_name})
        message(FATAL_ERROR "This option is not available on Windows yet: it needs the hook "
            "neura_windows_${neura_hook_name} of cmake/windows/${neura_hook_file}")
    endif()
    cmake_language(CALL neura_windows_${neura_hook_name})
endmacro()

file(GLOB neura_windows_area_files CONFIGURE_DEPENDS "${NEURA_WINDOWS_DIR}/windows/*.cmake")
list(SORT neura_windows_area_files)
foreach(neura_windows_area_file IN LISTS neura_windows_area_files)
    include("${neura_windows_area_file}")
endforeach()
unset(neura_windows_area_file)
unset(neura_windows_area_files)
