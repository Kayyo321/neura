# lifecycle-bootstrap: packaging/bootstrap.sh, the script a release publishes
# as install.sh, reading a release from a local directory. A download over
# HTTPS and the build provenance check are not tested here; the release
# workflow runs the script against each published release candidate.
if(NOT DEFINED BINARY_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR OR NOT DEFINED NEURA)
    message(FATAL_ERROR "Provide BINARY_DIR, SOURCE_DIR, TEST_DIR, and NEURA")
endif()
include("${SOURCE_DIR}/tests/lifecycle/common.cmake")

# A release directory: the archive of this build and its metadata.
lc_make_install("${TEST_DIR}/base")
set(release "${LC_ARCHIVES}")
lc_exec(metadata COMMAND "${LC_SH}" "${SOURCE_DIR}/packaging/make-release-metadata.sh" "${release}" "${LC_VERSION}")
lc_expect_status("make-release-metadata.sh" metadata 0)
set(archive "neura-${LC_VERSION}-${LC_TRIPLE}.tar.gz")
set(bootstrap "${SOURCE_DIR}/packaging/bootstrap.sh")

# bootstrap(<prefix> <home> args...): runs the script with a new home directory.
function(bootstrap prefix home)
    file(MAKE_DIRECTORY "${home}")
    lc_exec(${prefix} ENV "HOME=${home}" "NEURA_INSTALL_ROOT=${home}/root" "NEURA_BIN_DIR=${home}/bin"
        COMMAND "${LC_SH}" "${bootstrap}" ${ARGN})
    set(${prefix}_STATUS "${${prefix}_STATUS}" PARENT_SCOPE)
    set(${prefix}_OUT "${${prefix}_OUT}" PARENT_SCOPE)
    set(${prefix}_ERR "${${prefix}_ERR}" PARENT_SCOPE)
endfunction()

function(expect_nothing_installed what home)
    if(EXISTS "${home}/root" OR EXISTS "${home}/bin")
        message(FATAL_ERROR "${what}: the failed install left files under ${home}")
    endif()
endfunction()

# --- a successful install ------------------------------------------------------

set(home "${TEST_DIR}/ok")
file(REMOVE_RECURSE "${home}")
bootstrap(first "${home}" --source "${release}")
lc_expect_status("install from a local release" first 0)
lc_expect_contains("install from a local release" "${first_OUT}" "Verified: size and SHA-256 of ${archive}")
lc_expect_contains("install from a local release" "${first_OUT}" "Not verified: build provenance")
lc_exec(version ENV "HOME=${home}" COMMAND "${home}/bin/neura" --version)
lc_expect_status("the installed neura --version" version 0)
lc_expect_equal("the installed neura --version" "${version_OUT}" "${LC_VERSION_LINE}")
lc_exec(doctor ENV "HOME=${home}" COMMAND "${home}/bin/neura" doctor)
lc_expect_status("neura doctor after the install" doctor 0)

# The named version installs the same release; another version is refused.
set(home "${TEST_DIR}/named")
file(REMOVE_RECURSE "${home}")
bootstrap(named "${home}" --source "${release}" --version "${LC_VERSION}")
lc_expect_status("install with --version" named 0)
set(home "${TEST_DIR}/other-version")
file(REMOVE_RECURSE "${home}")
bootstrap(other "${home}" --source "${release}" --version 9.9.9)
lc_expect_status("install of a version the source does not hold" other 1)
lc_expect_contains("install of a version the source does not hold" "${other_ERR}" "asked for version 9.9.9")
expect_nothing_installed("install of a version the source does not hold" "${home}")

# Installing the same version again needs --force.
set(home "${TEST_DIR}/ok")
lc_snapshot(before "${home}/root")
bootstrap(again "${home}" --source "${release}")
if(again_STATUS STREQUAL "0")
    message(FATAL_ERROR "a second install of the same version succeeded without --force")
endif()
lc_snapshot(after "${home}/root")
lc_expect_equal("a refused second install changes nothing" "${after}" "${before}")
bootstrap(forced "${home}" --source "${release}" --force)
lc_expect_status("a second install with --force" forced 0)

# --- releases that must be refused ----------------------------------------------

# broken(<name> <error text>): expects the script to fail on ${TEST_DIR}/<name>/release.
function(broken name text)
    set(home "${TEST_DIR}/${name}/home")
    file(REMOVE_RECURSE "${home}")
    bootstrap(result "${home}" --source "${TEST_DIR}/${name}/release")
    lc_expect_status("${name}" result 1)
    lc_expect_contains("${name}" "${result_ERR}" "${text}")
    expect_nothing_installed("${name}" "${home}")
endfunction()

function(copy_release name)
    file(REMOVE_RECURSE "${TEST_DIR}/${name}")
    file(MAKE_DIRECTORY "${TEST_DIR}/${name}/release")
    file(COPY "${release}/${archive}" "${release}/neura-release.txt" DESTINATION "${TEST_DIR}/${name}/release")
endfunction()

# An archive of another size.
copy_release(wrong-size)
file(APPEND "${TEST_DIR}/wrong-size/release/${archive}" "x")
broken(wrong-size "the release metadata says")

# An archive of the right size with other content.
copy_release(wrong-digest)
file(SIZE "${release}/${archive}" size)
string(REPEAT "x" ${size} filler)
file(WRITE "${TEST_DIR}/wrong-digest/release/${archive}" "${filler}")
broken(wrong-digest "SHA-256 mismatch")

# Metadata with no archive for this machine.
copy_release(no-asset)
file(WRITE "${TEST_DIR}/no-asset/release/neura-release.txt" "neura-release 1\nversion ${LC_VERSION}\n")
broken(no-asset "has no archive for ${LC_TRIPLE}")

# Metadata that is something else.
copy_release(not-metadata)
file(WRITE "${TEST_DIR}/not-metadata/release/neura-release.txt" "<html>Not Found</html>\n")
broken(not-metadata "is not release metadata")

# An archive whose members leave its directory, with metadata that matches it.
copy_release(escape)
file(MAKE_DIRECTORY "${TEST_DIR}/escape/pack/neura-${LC_VERSION}-${LC_TRIPLE}" "${TEST_DIR}/escape/pack/outside")
file(WRITE "${TEST_DIR}/escape/pack/neura-${LC_VERSION}-${LC_TRIPLE}/install.sh" "exit 0\n")
file(WRITE "${TEST_DIR}/escape/pack/outside/file" "x\n")
lc_exec(pack WORKING_DIRECTORY "${TEST_DIR}/escape/pack"
    COMMAND "${LC_TAR}" -czf "${TEST_DIR}/escape/release/${archive}" "neura-${LC_VERSION}-${LC_TRIPLE}" outside)
lc_expect_status("packing the escaping archive" pack 0)
file(SHA256 "${TEST_DIR}/escape/release/${archive}" digest)
file(SIZE "${TEST_DIR}/escape/release/${archive}" size)
file(WRITE "${TEST_DIR}/escape/release/neura-release.txt"
    "neura-release 1\nversion ${LC_VERSION}\nasset ${LC_TRIPLE} ${archive} ${digest} ${size}\n")
broken(escape "holds a member outside")

# --- sources that are refused before anything is read -----------------------------

set(home "${TEST_DIR}/http")
file(REMOVE_RECURSE "${home}")
bootstrap(http "${home}" --source "http://example.invalid/release")
lc_expect_status("an http source" http 1)
lc_expect_contains("an http source" "${http_ERR}" "an http:// source is not accepted")
expect_nothing_installed("an http source" "${home}")

bootstrap(unknown "${home}" --frobnicate)
lc_expect_status("an unknown option" unknown 1)
lc_expect_contains("an unknown option" "${unknown_ERR}" "unknown option")
