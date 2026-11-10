cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS BINARY_DIR SOURCE_DIR TEST_DIR TRIPLE HAS_STATIC)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "Provide BINARY_DIR, SOURCE_DIR, TEST_DIR, TRIPLE, and HAS_STATIC")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_DIR}")
set(prefix "${TEST_DIR}/prefix")
set(build_dir "${TEST_DIR}/build")
set(deployed "${TEST_DIR}/deployed")
set(root "${TEST_DIR}/empty-root")
set(proc_root "${TEST_DIR}/proc-root")
file(MAKE_DIRECTORY "${build_dir}" "${deployed}")

function(invoke directory)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    foreach(field IN ITEMS status stdout stderr)
        set(result_${field} "${${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

function(expect_ok directory)
    invoke("${directory}" ${ARGN})
    if(NOT result_status STREQUAL "0")
        message(FATAL_ERROR "${ARGN} returned ${result_status}\n${result_stdout}${result_stderr}")
    endif()
    set(command_stdout "${result_stdout}" PARENT_SCOPE)
endfunction()

function(expect_hello directory)
    invoke("${directory}" "${CMAKE_COMMAND}" -E env --unset=NEURA_RUN_OPTIONS --unset=LD_LIBRARY_PATH
        --unset=LD_PRELOAD "PATH=${TEST_DIR}/no-toolchain" ${ARGN})
    if(NOT result_status STREQUAL "0" OR NOT result_stdout STREQUAL "Hello, World!\n" OR
       NOT result_stderr STREQUAL "")
        message(FATAL_ERROR "${ARGN} returned ${result_status}\n${result_stdout}${result_stderr}")
    endif()
endfunction()

set(config_args)
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    set(config_args --config "${CONFIG}")
endif()
expect_ok("${build_dir}" "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --prefix "${prefix}" ${config_args})

set(neura "${prefix}/bin/neura")
set(launchers "${prefix}/lib/neura/launchers/${TRIPLE}")
set(hello "${SOURCE_DIR}/examples/hello-world/hello-world.nra")
find_program(readelf NAMES readelf llvm-readelf REQUIRED)

function(expect_cpu_only launcher)
    expect_ok("${build_dir}" "${readelf}" -d "${launcher}")
    if(command_stdout MATCHES "[Vv]ulkan")
        message(FATAL_ERROR "${launcher} depends on a Vulkan library\n${command_stdout}")
    endif()
    file(READ "${launcher}" launcher_hex HEX)
    foreach(spelling IN ITEMS "76756c6b616e" "56756c6b616e")
        string(FIND "${launcher_hex}" "${spelling}" vulkan_offset)
        if(NOT vulkan_offset EQUAL -1)
            message(FATAL_ERROR "${launcher} contains the text 'ulkan'")
        endif()
    endforeach()
endfunction()

expect_cpu_only("${launchers}/neura-launcher")
expect_ok("${build_dir}" "${readelf}" -d "${launchers}/neura-launcher")
string(REGEX MATCHALL "NEEDED[^\n]*" dependencies "${command_stdout}")
if(NOT dependencies)
    message(FATAL_ERROR "the default launcher should depend on the platform C library")
endif()
foreach(dependency IN LISTS dependencies)
    if(NOT dependency MATCHES "\\[(lib(c|m|dl|pthread|rt|util)\\.so(\\.[0-9]+)*|ld-linux[^]]*)\\]$")
        message(FATAL_ERROR "unexpected dynamic dependency in default launcher: ${dependency}")
    endif()
endforeach()
expect_ok("${build_dir}" "${neura}" build --native "${hello}" -o hello)
file(COPY "${build_dir}/hello" DESTINATION "${deployed}")

if(HAS_STATIC)
    find_program(unshare NAMES unshare REQUIRED)
    expect_cpu_only("${launchers}/neura-launcher-static")
    expect_ok("${build_dir}" "${readelf}" -l -d "${launchers}/neura-launcher-static")
    if(command_stdout MATCHES "NEEDED|INTERP")
        message(FATAL_ERROR "the static launcher has a dynamic dependency or interpreter\n${command_stdout}")
    endif()
    expect_ok("${build_dir}" "${neura}" build --native --static "${hello}" -o hello-static)
    # One root holds only the executable. The other adds an empty /proc
    # directory, on which a procfs is mounted when the test runs.
    file(MAKE_DIRECTORY "${root}" "${proc_root}/proc")
    file(COPY "${build_dir}/hello-static" DESTINATION "${root}")
    file(COPY "${build_dir}/hello-static" DESTINATION "${proc_root}")
elseif(EXISTS "${launchers}/neura-launcher-static")
    message(FATAL_ERROR "the installation unexpectedly contains a static launcher")
endif()

# The copied executables cannot rely on the installed toolchain or a
# separate NVR image. The default copy can still use the system C library.
file(REMOVE_RECURSE "${prefix}" "${build_dir}")
expect_hello("${deployed}" "${deployed}/hello")
if(HAS_STATIC)
    # An empty root file system: the executable is the only entry, so there
    # is no /proc, no interpreter, no library, and no tool. The launcher finds
    # its own file from the kernel's record of the executed path.
    file(GLOB root_entries RELATIVE "${root}" "${root}/*" "${root}/.*")
    if(NOT root_entries STREQUAL "hello-static")
        message(FATAL_ERROR "the empty root should hold only the executable, found: ${root_entries}")
    endif()
    expect_hello("${root}" "${unshare}" --user --map-root-user --root "${root}" --wd / /hello-static)
    message(STATUS "ran the installed static executable in a root holding only the executable, without procfs")
    # With procfs mounted the launcher takes /proc/self/exe.
    expect_hello("${proc_root}" "${unshare}" --user --map-root-user --pid --fork --mount-proc=/proc
        --root "${proc_root}" --wd / /hello-static)
    message(STATUS "ran the installed static executable in a root with procfs")
else()
    message(WARNING "static launcher: NOT BUILT, static checks not run (NEURA_STATIC_LAUNCHER has no static C library to link)")
endif()
message(STATUS "ran the relocated default executable without the installed toolchain")
