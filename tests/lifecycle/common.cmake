# Shared helpers of the lifecycle-* tests. Include it from a `cmake -P`
# script after setting BINARY_DIR, SOURCE_DIR, TEST_DIR, NEURA, and CONFIG.
#
# These tests need no network, no root, and no terminal: every child process
# reads its standard input from /dev/null, and updates use `--source <dir>`.

find_program(LC_SH sh REQUIRED)
find_program(LC_TAR tar REQUIRED)

# lc_exec(<prefix> [ENV NAME=value...] [WORKING_DIRECTORY dir] COMMAND args...)
# Runs the command and sets <prefix>_STATUS, <prefix>_OUT, and <prefix>_ERR.
function(lc_exec prefix)
    cmake_parse_arguments(ARG "" "WORKING_DIRECTORY" "ENV;COMMAND" ${ARGN})
    set(directory)
    if(ARG_WORKING_DIRECTORY)
        set(directory WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${ARG_ENV} ${ARG_COMMAND}
        ${directory}
        INPUT_FILE /dev/null
        RESULT_VARIABLE status
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(${prefix}_STATUS "${status}" PARENT_SCOPE)
    set(${prefix}_OUT "${out}" PARENT_SCOPE)
    set(${prefix}_ERR "${err}" PARENT_SCOPE)
endfunction()

function(lc_expect_status what prefix expected)
    if(NOT "${${prefix}_STATUS}" STREQUAL "${expected}")
        message(FATAL_ERROR "${what}: expected exit status ${expected}, got ${${prefix}_STATUS}\n"
            "stdout:\n${${prefix}_OUT}\nstderr:\n${${prefix}_ERR}")
    endif()
endfunction()

function(lc_expect_contains what text needle)
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${what}: expected the text to contain\n  ${needle}\nbut it is\n${text}")
    endif()
endfunction()

function(lc_expect_equal what actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "${what}: expected\n[${expected}]\nbut got\n[${actual}]")
    endif()
endfunction()

# The tool built in the build tree reports its version, triple, and commit.
function(lc_tool_version)
    lc_exec(version COMMAND "${NEURA}" --version)
    lc_expect_status("neura --version" version 0)
    if(NOT version_OUT MATCHES "^neura ([^ ]+) \\(([^,]+), commit ([^)]+)\\)\n$")
        message(FATAL_ERROR "unexpected neura --version output: [${version_OUT}]")
    endif()
    set(LC_VERSION "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set(LC_TRIPLE "${CMAKE_MATCH_2}" PARENT_SCOPE)
    set(LC_VERSION_LINE "${version_OUT}" PARENT_SCOPE)
endfunction()

# lc_snapshot(<var> <root>): every path below <root> with the SHA-256 of each
# file and the target of each symbolic link, so two snapshots are equal only
# when nothing changed, hidden files included.
function(lc_snapshot var root)
    file(GLOB_RECURSE entries LIST_DIRECTORIES true RELATIVE "${root}" "${root}/*")
    list(SORT entries)
    set(text "")
    foreach(entry IN LISTS entries)
        if(IS_SYMLINK "${root}/${entry}")
            file(READ_SYMLINK "${root}/${entry}" target)
            string(APPEND text "${entry} -> ${target}\n")
        elseif(IS_DIRECTORY "${root}/${entry}")
            string(APPEND text "${entry}/\n")
        else()
            file(SHA256 "${root}/${entry}" digest)
            string(APPEND text "${entry} ${digest}\n")
        endif()
    endforeach()
    set(${var} "${text}" PARENT_SCOPE)
endfunction()

# lc_make_install(<dir>): installs the build into <dir>/prefix, packs it with
# packaging/make-archive.sh, unpacks the archive, and runs its install.sh with
# HOME, NEURA_INSTALL_ROOT, and NEURA_BIN_DIR below <dir>. Sets LC_PREFIX,
# LC_ARCHIVES, LC_UNPACKED, LC_ROOT, LC_BIN_DIR, LC_HOME, LC_LINK (the
# `neura` in the bin directory), LC_VERSION, and LC_TRIPLE.
function(lc_make_install dir)
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}/home")
    set(config_args)
    if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
        set(config_args --config "${CONFIG}")
    endif()
    lc_exec(install COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --prefix "${dir}/prefix" ${config_args})
    lc_expect_status("cmake --install" install 0)
    lc_tool_version()
    lc_exec(archive COMMAND "${LC_SH}" "${SOURCE_DIR}/packaging/make-archive.sh"
        "${dir}/prefix" "${dir}/archives" "${LC_VERSION}" "${LC_TRIPLE}")
    lc_expect_status("make-archive.sh" archive 0)
    file(MAKE_DIRECTORY "${dir}/unpacked")
    lc_exec(unpack COMMAND "${LC_TAR}" -xzf "${dir}/archives/neura-${LC_VERSION}-${LC_TRIPLE}.tar.gz" -C "${dir}/unpacked")
    lc_expect_status("unpacking the archive" unpack 0)
    set(unpacked "${dir}/unpacked/neura-${LC_VERSION}-${LC_TRIPLE}")
    lc_exec(setup ENV "HOME=${dir}/home" "NEURA_INSTALL_ROOT=${dir}/root" "NEURA_BIN_DIR=${dir}/bin"
        WORKING_DIRECTORY "${unpacked}" COMMAND "${LC_SH}" ./install.sh)
    lc_expect_status("install.sh" setup 0)
    file(REAL_PATH "${dir}/root" root)
    file(REAL_PATH "${dir}/bin" bin_dir)
    set(LC_PREFIX "${dir}/prefix" PARENT_SCOPE)
    set(LC_ARCHIVES "${dir}/archives" PARENT_SCOPE)
    set(LC_UNPACKED "${unpacked}" PARENT_SCOPE)
    set(LC_ROOT "${root}" PARENT_SCOPE)
    set(LC_BIN_DIR "${bin_dir}" PARENT_SCOPE)
    set(LC_HOME "${dir}/home" PARENT_SCOPE)
    set(LC_LINK "${bin_dir}/neura" PARENT_SCOPE)
    set(LC_VERSION "${LC_VERSION}" PARENT_SCOPE)
    set(LC_TRIPLE "${LC_TRIPLE}" PARENT_SCOPE)
    set(LC_VERSION_LINE "${LC_VERSION_LINE}" PARENT_SCOPE)
endfunction()

# lc_expect_no_leftovers(<what> <root>): fails if an operation left temporary
# files in the install root.
function(lc_expect_no_leftovers what root)
    file(GLOB found LIST_DIRECTORIES true "${root}/.staging-*" "${root}/.update-lock" "${root}/.meta-*"
        "${root}/.marker-*" "${root}/.current-*" "${root}/.install-*")
    if(found)
        message(FATAL_ERROR "${what} left temporary files behind: ${found}")
    endif()
endfunction()
