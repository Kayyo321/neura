# lifecycle-update: `neura update` and `neura update --check` against local
# release directories (--source <directory>), run through an install made
# from a release archive.
#
# The failure cases come first and each one must leave the installation
# byte-for-byte as it was, with no staging directory, lock, or temporary
# file. The successful update comes last because it replaces the install
# with a stand-in `#!/bin/sh` build that reports version 9.9.9.
#
# Fault injection. The environment variable NEURA_TEST_UPDATE_FAULT=<step>
# makes the named step of `neura update` fail. It exists only for this test
# and is not a supported interface. The steps are download, checksum,
# extract, verify, activate-link, and activate-marker; a failure in an
# activate step must be rolled back.
#
# Not covered: downloads over HTTPS (no network), and `gh attestation
# verify` (it needs the network). The tests pass with or without gh on PATH.
if(NOT DEFINED BINARY_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR OR NOT DEFINED NEURA)
    message(FATAL_ERROR "Provide BINARY_DIR, SOURCE_DIR, TEST_DIR, and NEURA")
endif()
include("${SOURCE_DIR}/tests/lifecycle/common.cmake")

lc_make_install("${TEST_DIR}")
set(work "${TEST_DIR}/work")
file(MAKE_DIRECTORY "${work}")
set(update_env "HOME=${LC_HOME}")
set(new_version "9.9.9")

set(good_script [=[#!/bin/sh
case "$1" in
--version) echo "neura 9.9.9 (test, commit test)" ;;
esac
exit 0
]=])
set(lying_script [=[#!/bin/sh
case "$1" in
--version) echo "neura 1.2.3 (test, commit test)" ;;
esac
exit 0
]=])

# A copy of the installed prefix whose bin/neura is `script`.
function(lc_fake_prefix destination script)
    file(REMOVE_RECURSE "${destination}")
    lc_exec(copy COMMAND "${CMAKE_COMMAND}" -E copy_directory "${LC_PREFIX}" "${destination}")
    lc_expect_status("copying the prefix" copy 0)
    file(REMOVE "${destination}/bin/neura")
    file(WRITE "${destination}/bin/neura" "${script}")
    file(CHMOD "${destination}/bin/neura" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endfunction()

# A release directory made by the packaging scripts for a stand-in build.
function(lc_scripted_release name script)
    lc_fake_prefix("${work}/${name}-prefix" "${script}")
    lc_exec(archive COMMAND "${LC_SH}" "${SOURCE_DIR}/packaging/make-archive.sh"
        "${work}/${name}-prefix" "${work}/${name}" "${new_version}" "${LC_TRIPLE}")
    lc_expect_status("make-archive.sh for ${name}" archive 0)
    lc_exec(metadata COMMAND "${LC_SH}" "${SOURCE_DIR}/packaging/make-release-metadata.sh" "${work}/${name}" "${new_version}")
    lc_expect_status("make-release-metadata.sh for ${name}" metadata 0)
endfunction()

# A release directory for an archive made by hand. SHA and SIZE override the
# values written to the metadata.
function(lc_manual_release name archive)
    cmake_parse_arguments(ARG "" "SHA;SIZE;TRIPLE" "" ${ARGN})
    file(SHA256 "${archive}" digest)
    file(SIZE "${archive}" size)
    if(ARG_SHA)
        set(digest "${ARG_SHA}")
    endif()
    if(ARG_SIZE)
        set(size "${ARG_SIZE}")
    endif()
    set(triple "${LC_TRIPLE}")
    if(ARG_TRIPLE)
        set(triple "${ARG_TRIPLE}")
    endif()
    file(REMOVE_RECURSE "${work}/${name}")
    file(MAKE_DIRECTORY "${work}/${name}")
    get_filename_component(file_name "${archive}" NAME)
    file(COPY "${archive}" DESTINATION "${work}/${name}")
    file(WRITE "${work}/${name}/neura-release.txt"
        "neura-release 1\nversion ${new_version}\nasset ${triple} ${file_name} ${digest} ${size}\n")
endfunction()

# An archive of a hand-made tree: the layout of a good archive in miniature.
function(lc_tree name)
    set(tree "${work}/${name}-tree")
    file(REMOVE_RECURSE "${tree}")
    set(top "${tree}/neura-${new_version}-${LC_TRIPLE}")
    file(MAKE_DIRECTORY "${top}/bin" "${top}/share/neura/stdlib/std")
    file(WRITE "${top}/bin/neura" "${good_script}")
    file(CHMOD "${top}/bin/neura" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    file(WRITE "${top}/share/neura/stdlib/std/x.nra" "\n")
    set(tree_top "${top}" PARENT_SCOPE)
    set(tree_dir "${tree}" PARENT_SCOPE)
endfunction()

lc_snapshot(pristine "${LC_ROOT}")
set(pristine_line "${LC_VERSION_LINE}")

# After a failure the original install is byte-for-byte as it was and works.
function(lc_expect_untouched what)
    lc_snapshot(now "${LC_ROOT}")
    lc_expect_equal("${what}: the install root" "${now}" "${pristine}")
    lc_expect_no_leftovers("${what}" "${LC_ROOT}")
    lc_exec(still COMMAND "${LC_LINK}" --version)
    lc_expect_status("${what}: the installed neura" still 0)
    lc_expect_equal("${what}: the installed neura" "${still_OUT}" "${pristine_line}")
endfunction()

# lc_expect_failure(<what> <release dir> <text> [ENV ...] [ARGS ...])
function(lc_expect_failure what release text)
    cmake_parse_arguments(ARG "" "" "ENV;ARGS" ${ARGN})
    lc_exec(failure ENV ${update_env} ${ARG_ENV} COMMAND "${LC_LINK}" update --source "${release}" ${ARG_ARGS})
    lc_expect_status("${what}" failure 1)
    lc_expect_contains("${what}" "${failure_ERR}" "${text}")
    lc_expect_untouched("${what}")
endfunction()

# --- (a) the same version, an older version -------------------------------

lc_exec(release COMMAND "${LC_SH}" "${SOURCE_DIR}/packaging/make-release-metadata.sh" "${LC_ARCHIVES}" "${LC_VERSION}")
lc_expect_status("release metadata for the installed version" release 0)
foreach(flags IN ITEMS "" "--check")
    lc_exec(same ENV ${update_env} COMMAND "${LC_LINK}" update ${flags} --source "${LC_ARCHIVES}")
    lc_expect_status("update ${flags} to the same version" same 0)
    lc_expect_equal("update ${flags} to the same version" "${same_OUT}" "neura ${LC_VERSION} is up to date\n")
endforeach()
file(MAKE_DIRECTORY "${work}/older")
file(WRITE "${work}/older/neura-release.txt" "neura-release 1\nversion 0.0.1\n")
lc_exec(older ENV ${update_env} COMMAND "${LC_LINK}" update --source "${work}/older")
lc_expect_status("update to an older version" older 0)
lc_expect_equal("update to an older version" "${older_OUT}" "neura ${LC_VERSION} is up to date\n")
lc_expect_untouched("up to date")

# --- (b) a newer version: --check changes nothing --------------------------

lc_scripted_release(good "${good_script}")
lc_exec(check ENV ${update_env} COMMAND "${LC_LINK}" update --check --source "${work}/good")
lc_expect_status("update --check with a newer version" check 4)
lc_expect_contains("update --check" "${check_OUT}" "neura ${LC_VERSION} is installed; neura ${new_version} is available")
lc_expect_untouched("update --check")

# --- (c) failures -----------------------------------------------------------

set(good_archive "${work}/good/neura-${new_version}-${LC_TRIPLE}.tar.gz")
set(zero_digest "0000000000000000000000000000000000000000000000000000000000000000")
lc_manual_release(bad-sha "${good_archive}" SHA "${zero_digest}")
lc_expect_failure("a wrong SHA-256" "${work}/bad-sha" "SHA-256 mismatch")
file(SIZE "${good_archive}" good_size)
math(EXPR wrong_size "${good_size} + 1")
lc_manual_release(bad-size "${good_archive}" SIZE "${wrong_size}")
lc_expect_failure("a wrong size" "${work}/bad-size" "the release metadata says ${wrong_size}")

# A member outside the top-level directory.
lc_tree(evil)
file(WRITE "${tree_dir}/evil" "outside\n")
lc_exec(pack COMMAND "${LC_TAR}" -czPf "${work}/evil.tar.gz" -C "${tree_dir}" --transform "s,^evil$,../evil," "neura-${new_version}-${LC_TRIPLE}" evil)
lc_expect_status("packing the archive with a ../evil member" pack 0)
lc_exec(listing COMMAND "${LC_TAR}" -tzf "${work}/evil.tar.gz")
lc_expect_contains("the hand-made archive" "${listing_OUT}" "../evil")
lc_manual_release(evil "${work}/evil.tar.gz")
lc_expect_failure("an archive with a ../evil member" "${work}/evil" "is not inside")
if(EXISTS "${work}/evil" AND EXISTS "${LC_ROOT}/../evil")
    message(FATAL_ERROR "the ../evil member was extracted outside the root")
endif()

# A member outside the directory through an absolute name.
lc_tree(absolute)
file(WRITE "${tree_dir}/absolute-member" "outside\n")
lc_exec(pack COMMAND "${LC_TAR}" -czPf "${work}/absolute.tar.gz" -C "${tree_dir}" --transform "s,^absolute-member$,/tmp/neura-absolute-member," "neura-${new_version}-${LC_TRIPLE}" absolute-member)
lc_expect_status("packing the archive with an absolute member" pack 0)
lc_manual_release(absolute "${work}/absolute.tar.gz")
lc_expect_failure("an archive with an absolute member" "${work}/absolute" "is not inside")

# A symbolic link.
lc_tree(linked)
file(CREATE_LINK "/etc/passwd" "${tree_top}/link" SYMBOLIC)
lc_exec(pack COMMAND "${LC_TAR}" -czf "${work}/linked.tar.gz" -C "${tree_dir}" "neura-${new_version}-${LC_TRIPLE}")
lc_expect_status("packing the archive with a symbolic link" pack 0)
lc_manual_release(linked "${work}/linked.tar.gz")
lc_expect_failure("an archive with a symbolic link" "${work}/linked" "is not a regular file or a directory")

# A hard link.
lc_tree(hardlinked)
file(CREATE_LINK "${tree_top}/bin/neura" "${tree_top}/bin/neura2")
lc_exec(pack COMMAND "${LC_TAR}" -czf "${work}/hardlinked.tar.gz" -C "${tree_dir}" "neura-${new_version}-${LC_TRIPLE}")
lc_expect_status("packing the archive with a hard link" pack 0)
lc_manual_release(hardlinked "${work}/hardlinked.tar.gz")
lc_expect_failure("an archive with a hard link" "${work}/hardlinked" "is not a regular file or a directory")

# A staged binary that reports another version.
lc_scripted_release(lying "${lying_script}")
lc_expect_failure("a staged binary that reports another version" "${work}/lying" "expected a line starting \"neura ${new_version} \"")

# A release without a build for this platform.
lc_manual_release(other-triple "${good_archive}" TRIPLE "riscv64-plan9-gnu")
lc_expect_failure("a release without a build for this platform" "${work}/other-triple" "has no build for ${LC_TRIPLE}")

# Malformed metadata.
file(MAKE_DIRECTORY "${work}/malformed")
file(WRITE "${work}/malformed/neura-release.txt" "this is not release metadata\n")
lc_expect_failure("malformed metadata" "${work}/malformed" "the release metadata is not valid")
file(MAKE_DIRECTORY "${work}/missing")
lc_expect_failure("a source without metadata" "${work}/missing" "cannot read release metadata")
file(MAKE_DIRECTORY "${work}/traversal")
file(WRITE "${work}/traversal/neura-release.txt"
    "neura-release 1\nversion ${new_version}\nasset ${LC_TRIPLE} ../x.tar.gz ${zero_digest} 10\n")
lc_expect_failure("a file name with a slash" "${work}/traversal" "the file name is not valid")

# Every injected fault.
foreach(step IN ITEMS download checksum extract verify activate-link activate-marker)
    set(expected "injected fault at step ${step}")
    if(step MATCHES "^activate")
        set(expected "update failed and was rolled back; neura ${LC_VERSION} is still installed")
    endif()
    lc_expect_failure("a fault at ${step}" "${work}/good" "${expected}" ENV "NEURA_TEST_UPDATE_FAULT=${step}")
endforeach()

# --require-attestation needs gh. Give the child a PATH that has tar and no gh.
file(MAKE_DIRECTORY "${work}/no-gh")
file(CREATE_LINK "${LC_TAR}" "${work}/no-gh/tar" SYMBOLIC)
lc_expect_failure("--require-attestation without gh" "${work}/good" "gh (the GitHub CLI), which is not on PATH"
    ENV "PATH=${work}/no-gh" ARGS --require-attestation)

# The update lock: another update, and a lock that is more than an hour old.
file(WRITE "${LC_ROOT}/.update-lock" "1\n")
lc_exec(locked ENV ${update_env} COMMAND "${LC_LINK}" update --source "${work}/good")
lc_expect_status("update with a lock" locked 1)
lc_expect_contains("update with a lock" "${locked_ERR}" "another update appears to be running")
if(NOT EXISTS "${LC_ROOT}/.update-lock")
    message(FATAL_ERROR "update removed a lock it did not create")
endif()
lc_exec(touch COMMAND touch -t 200001010000 "${LC_ROOT}/.update-lock")
lc_expect_status("touch" touch 0)
lc_exec(stale ENV ${update_env} COMMAND "${LC_LINK}" update --source "${work}/good")
lc_expect_status("update with an old lock" stale 1)
lc_expect_contains("update with an old lock" "${stale_ERR}" "more than an hour old")
if(NOT EXISTS "${LC_ROOT}/.update-lock")
    message(FATAL_ERROR "update broke an old lock by itself")
endif()
file(REMOVE "${LC_ROOT}/.update-lock")

# --- (e) sources ------------------------------------------------------------

foreach(flags IN ITEMS "" "--check")
    lc_exec(http ENV ${update_env} COMMAND "${LC_LINK}" update ${flags} --source "http://127.0.0.1:9/releases")
    lc_expect_status("update ${flags} from an http:// source" http 1)
    lc_expect_contains("update ${flags} from an http:// source" "${http_ERR}" "https://")
    lc_exec(other ENV ${update_env} COMMAND "${LC_LINK}" update ${flags} --source "file:///tmp")
    lc_expect_status("update ${flags} from a file:// source" other 1)
    lc_exec(none ENV ${update_env} COMMAND "${LC_LINK}" update ${flags} --source "${work}/does-not-exist")
    lc_expect_status("update ${flags} from a missing directory" none 1)
endforeach()
lc_expect_untouched("rejected sources")
lc_exec(options ENV ${update_env} COMMAND "${LC_LINK}" update --bogus)
lc_expect_status("update with an unknown option" options 1)

# --- (d) installs that neura does not manage --------------------------------

foreach(arguments IN ITEMS "update" "update;--check" "uninstall" "uninstall;--dry-run")
    lc_exec(unmanaged ENV ${update_env} COMMAND "${NEURA}" ${arguments})
    lc_expect_status("${arguments} on the build tree's neura" unmanaged 3)
    lc_expect_contains("${arguments} on the build tree's neura" "${unmanaged_ERR}" "not an archive install")
endforeach()
lc_exec(unmanaged_doctor COMMAND "${NEURA}" doctor)
lc_expect_status("doctor on the build tree's neura" unmanaged_doctor 0)
lc_expect_contains("doctor on the build tree's neura" "${unmanaged_doctor_OUT}" "ok  install: unmanaged")

set(homebrew "${work}/homebrew")
file(MAKE_DIRECTORY "${homebrew}/versions/${LC_VERSION}/bin")
file(COPY "${NEURA}" DESTINATION "${homebrew}/versions/${LC_VERSION}/bin")
get_filename_component(neura_name "${NEURA}" NAME)
if(NOT neura_name STREQUAL "neura")
    file(RENAME "${homebrew}/versions/${LC_VERSION}/bin/${neura_name}" "${homebrew}/versions/${LC_VERSION}/bin/neura")
endif()
file(WRITE "${homebrew}/neura-install.txt" "neura-install 1\nmethod homebrew\nlink /opt/homebrew/bin/neura\nversion ${LC_VERSION}\n")
foreach(arguments IN ITEMS "update" "update;--check" "uninstall" "uninstall;--yes")
    lc_exec(owned ENV ${update_env} COMMAND "${homebrew}/versions/${LC_VERSION}/bin/neura" ${arguments})
    lc_expect_status("${arguments} on a homebrew install" owned 3)
    lc_expect_contains("${arguments} on a homebrew install" "${owned_ERR}" "homebrew")
endforeach()
if(NOT EXISTS "${homebrew}/neura-install.txt" OR NOT EXISTS "${homebrew}/versions/${LC_VERSION}")
    message(FATAL_ERROR "a command changed a homebrew install")
endif()
file(WRITE "${homebrew}/neura-install.txt" "neura-install 1\nmethod homebrew\n")
lc_exec(broken ENV ${update_env} COMMAND "${homebrew}/versions/${LC_VERSION}/bin/neura" update)
lc_expect_status("update with a malformed marker" broken 1)
lc_expect_contains("update with a malformed marker" "${broken_ERR}" "marker is not valid")
lc_exec(broken_doctor COMMAND "${homebrew}/versions/${LC_VERSION}/bin/neura" doctor)
lc_expect_status("doctor with a malformed marker" broken_doctor 1)
lc_expect_contains("doctor with a malformed marker" "${broken_doctor_OUT}" "FAIL  install")

# --- (b) a newer version: the update ----------------------------------------

# A crashed update's old staging directory is removed.
file(MAKE_DIRECTORY "${LC_ROOT}/.staging-1-crashed")
file(WRITE "${LC_ROOT}/.staging-1-crashed/file" "left over\n")
lc_exec(touch COMMAND touch -t 200001010000 "${LC_ROOT}/.staging-1-crashed")
lc_expect_status("touch" touch 0)

lc_exec(update ENV ${update_env} COMMAND "${LC_LINK}" update --source "${work}/good")
lc_expect_status("update" update 0)
lc_expect_contains("update" "${update_OUT}" "updated neura ${LC_VERSION} to ${new_version}")
lc_expect_contains("update" "${update_OUT}" "verified: size and SHA-256")
if(NOT update_OUT MATCHES "(NOT verified|not checked): build provenance")
    message(FATAL_ERROR "update did not say whether build provenance was verified:\n${update_OUT}")
endif()
file(READ_SYMLINK "${LC_ROOT}/current" current_target)
lc_expect_equal("current after the update" "${current_target}" "versions/${new_version}")
file(READ "${LC_ROOT}/neura-install.txt" marker)
lc_expect_equal("the marker after the update" "${marker}"
    "neura-install 1\nmethod archive\nlink ${LC_LINK}\nversion ${new_version}\n")
file(GLOB versions RELATIVE "${LC_ROOT}/versions" "${LC_ROOT}/versions/*")
lc_expect_equal("the version directories after the update" "${versions}" "${new_version}")
file(GLOB entries RELATIVE "${LC_ROOT}" "${LC_ROOT}/*" "${LC_ROOT}/.[a-z]*")
list(SORT entries)
lc_expect_equal("the entries of the root after the update" "${entries}" "current;neura-install.txt;versions")
lc_exec(after COMMAND "${LC_LINK}" --version)
lc_expect_status("the bin link after the update" after 0)
lc_expect_equal("the bin link after the update" "${after_OUT}" "neura 9.9.9 (test, commit test)\n")
