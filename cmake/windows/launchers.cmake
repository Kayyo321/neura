# The SDK's standalone PE launcher (docs/design/Windows030.md, W30).
macro(neura_windows_launchers)
    set(neura_launcher_triple "${NEURA_HOST_TRIPLE}")
    set(neura_launcher_directory "lib/neura/launchers/${neura_launcher_triple}")
    add_subdirectory(src/launcher)
endmacro()

macro(neura_windows_launcher_target)
    # clang-cl defaults to lld-link in CMake. Older CMake selects link.exe
    # in this directory so the other SDK targets keep their configured linker.
    if(CMAKE_VERSION VERSION_LESS "3.29")
        get_filename_component(neura_launcher_linker_name "${CMAKE_LINKER}" NAME)
        string(TOLOWER "${neura_launcher_linker_name}" neura_launcher_linker_name)
        if(NOT neura_launcher_linker_name MATCHES "^link(\\.exe)?$")
            find_program(NEURA_WINDOWS_LAUNCHER_LINKER NAMES link.exe
                HINTS "$ENV{VCToolsInstallDir}/bin/Hostx64/x64" REQUIRED)
            set(CMAKE_LINKER "${NEURA_WINDOWS_LAUNCHER_LINKER}")
        endif()
    endif()
    add_executable(neura_launcher main.cpp)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.29")
        set_property(TARGET neura_launcher PROPERTY LINKER_TYPE MSVC)
    endif()
    target_compile_definitions(neura_launcher PRIVATE
        NEURA_LAUNCHER_TRIPLE="${neura_launcher_triple}" NEURA_LAUNCHER_FLAVOR=0)
    target_include_directories(neura_launcher PRIVATE "${PROJECT_BINARY_DIR}/generated")
    # The SDK reads the descriptor from the file, so keep it even with /OPT:REF.
    target_link_options(neura_launcher PRIVATE /INCLUDE:neura_launcher_descriptor)
    # The runner links the NVM and its libffi archive without the product DLLs.
    target_link_libraries(neura_launcher PRIVATE neura_runner)
    set_target_properties(neura_launcher PROPERTIES OUTPUT_NAME neura-launcher
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${neura_launcher_directory}")
    neura_target_defaults(neura_launcher)
    install(TARGETS neura_launcher RUNTIME DESTINATION "${neura_launcher_directory}" COMPONENT sdk)
endmacro()

macro(neura_windows_launcher_tests)
    get_filename_component(neura_launcher_linker_dir "${CMAKE_LINKER}" DIRECTORY)
    find_program(NEURA_DUMPBIN dumpbin HINTS "${neura_launcher_linker_dir}" REQUIRED)
    set(neura_launcher_test_prefix "${CMAKE_CURRENT_BINARY_DIR}/stdlib-install/prefix")
    foreach(neura_launcher_suite IN ITEMS deploy unicode inspect-replay policy corrupt dependencies identity no-toolchain)
        set(neura_launcher_test_args)
        if(neura_launcher_suite STREQUAL "dependencies")
            list(APPEND neura_launcher_test_args --dumpbin "${NEURA_DUMPBIN}")
        endif()
        add_test(NAME native-pe-${neura_launcher_suite} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/windows-executable.py" ${neura_launcher_suite}
            "${neura_launcher_test_prefix}/bin/neura${CMAKE_EXECUTABLE_SUFFIX}"
            "${neura_launcher_test_prefix}/${neura_launcher_directory}/$<TARGET_FILE_NAME:neura_launcher>"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/native-pe-${neura_launcher_suite}"
            ${neura_launcher_test_args})
        set_tests_properties(native-pe-${neura_launcher_suite} PROPERTIES FIXTURES_REQUIRED stdlib)
    endforeach()
    unset(neura_launcher_linker_dir)
    unset(neura_launcher_test_prefix)
    unset(neura_launcher_test_args)
endmacro()
