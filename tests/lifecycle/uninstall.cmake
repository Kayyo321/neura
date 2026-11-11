# lifecycle-uninstall: `neura uninstall` on installs made from a release
# archive. Standard input is never a terminal here, so the interactive
# prompt is not tested; --yes and its absence are.
if(NOT DEFINED BINARY_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR OR NOT DEFINED NEURA)
    message(FATAL_ERROR "Provide BINARY_DIR, SOURCE_DIR, TEST_DIR, and NEURA")
endif()
include("${SOURCE_DIR}/tests/lifecycle/common.cmake")

# --- an install with unrelated files ------------------------------------------

lc_make_install("${TEST_DIR}/one")
file(WRITE "${LC_ROOT}/keep.txt" "not neura's\n")
file(WRITE "${LC_BIN_DIR}/other-tool" "not neura's\n")
# State and leftovers that uninstall owns.
file(WRITE "${LC_ROOT}/update-check.txt" "neura-update-check 1\nchecked 1\nlatest 0.1.0\n")
file(MAKE_DIRECTORY "${LC_ROOT}/.staging-1-old")
file(WRITE "${LC_ROOT}/.update-lock" "1\n")
set(uninstall_env "HOME=${LC_HOME}")

# --dry-run removes nothing.
lc_snapshot(before "${LC_ROOT}")
lc_snapshot(bin_before "${LC_BIN_DIR}")
lc_exec(dry ENV ${uninstall_env} COMMAND "${LC_LINK}" uninstall --dry-run)
lc_expect_status("uninstall --dry-run" dry 0)
lc_expect_contains("uninstall --dry-run" "${dry_OUT}" "would remove ${LC_ROOT}/versions/${LC_VERSION}")
lc_expect_contains("uninstall --dry-run" "${dry_OUT}" "would remove ${LC_ROOT}/current")
lc_expect_contains("uninstall --dry-run" "${dry_OUT}" "would remove ${LC_ROOT}/neura-install.txt")
lc_expect_contains("uninstall --dry-run" "${dry_OUT}" "would remove ${LC_LINK}")
lc_expect_contains("uninstall --dry-run" "${dry_OUT}" "would leave ${LC_ROOT}")
lc_expect_contains("uninstall --dry-run" "${dry_OUT}" "keep.txt")
lc_snapshot(after "${LC_ROOT}")
lc_snapshot(bin_after "${LC_BIN_DIR}")
lc_expect_equal("the root after --dry-run" "${after}" "${before}")
lc_expect_equal("the bin directory after --dry-run" "${bin_after}" "${bin_before}")

# Without --yes and without a terminal, nothing is removed.
lc_exec(refused ENV ${uninstall_env} COMMAND "${LC_LINK}" uninstall)
lc_expect_status("uninstall without --yes" refused 1)
lc_expect_contains("uninstall without --yes" "${refused_ERR}" "pass --yes")
lc_snapshot(after "${LC_ROOT}")
lc_snapshot(bin_after "${LC_BIN_DIR}")
lc_expect_equal("the root after a refused uninstall" "${after}" "${before}")
lc_expect_equal("the bin directory after a refused uninstall" "${bin_after}" "${bin_before}")

# An option that does not exist.
lc_exec(unknown ENV ${uninstall_env} COMMAND "${LC_LINK}" uninstall --everything)
lc_expect_status("uninstall with an unknown option" unknown 1)

# --yes removes neura and nothing else.
lc_exec(removed ENV ${uninstall_env} COMMAND "${LC_LINK}" uninstall --yes)
lc_expect_status("uninstall --yes" removed 0)
lc_expect_contains("uninstall --yes" "${removed_OUT}" "removed ${LC_ROOT}/versions/${LC_VERSION}")
lc_expect_contains("uninstall --yes" "${removed_OUT}" "removed ${LC_LINK}")
lc_expect_contains("uninstall --yes" "${removed_OUT}" "removed ${LC_ROOT}/neura-install.txt")
lc_expect_contains("uninstall --yes" "${removed_OUT}" "left ${LC_ROOT}, which still holds")
lc_expect_contains("uninstall --yes" "${removed_OUT}" "keep.txt")
foreach(path IN ITEMS versions current neura-install.txt update-check.txt .staging-1-old .update-lock)
    if(EXISTS "${LC_ROOT}/${path}" OR IS_SYMLINK "${LC_ROOT}/${path}")
        message(FATAL_ERROR "uninstall left ${path}")
    endif()
endforeach()
if(EXISTS "${LC_LINK}" OR IS_SYMLINK "${LC_LINK}")
    message(FATAL_ERROR "uninstall left the bin link")
endif()
file(READ "${LC_ROOT}/keep.txt" kept)
lc_expect_equal("the unrelated file in the root" "${kept}" "not neura's\n")
file(READ "${LC_BIN_DIR}/other-tool" kept)
lc_expect_equal("the unrelated file in the bin directory" "${kept}" "not neura's\n")

# --- a bin path that is not neura's link ---------------------------------------

foreach(kind IN ITEMS file elsewhere)
    lc_make_install("${TEST_DIR}/${kind}")
    file(REMOVE "${LC_LINK}")
    if(kind STREQUAL "file")
        file(WRITE "${LC_LINK}" "a program of the user\n")
    else()
        file(WRITE "${TEST_DIR}/${kind}/elsewhere.txt" "elsewhere\n")
        file(CREATE_LINK "${TEST_DIR}/${kind}/elsewhere.txt" "${LC_LINK}" SYMBOLIC)
    endif()
    # The bin link is gone, so run the installed tool directly.
    lc_exec(removed ENV "HOME=${LC_HOME}" COMMAND "${LC_ROOT}/current/bin/neura" uninstall --yes)
    lc_expect_status("uninstall with a ${kind} at the bin path" removed 0)
    lc_expect_contains("uninstall with a ${kind} at the bin path" "${removed_OUT}" "left ${LC_LINK}")
    if(NOT (EXISTS "${LC_LINK}" OR IS_SYMLINK "${LC_LINK}"))
        message(FATAL_ERROR "uninstall removed the ${kind} at the bin path")
    endif()
    if(kind STREQUAL "file")
        file(READ "${LC_LINK}" content)
        lc_expect_equal("the file at the bin path" "${content}" "a program of the user\n")
    else()
        file(READ_SYMLINK "${LC_LINK}" target)
        lc_expect_equal("the link at the bin path" "${target}" "${TEST_DIR}/${kind}/elsewhere.txt")
        if(NOT EXISTS "${TEST_DIR}/${kind}/elsewhere.txt")
            message(FATAL_ERROR "uninstall removed the target of a link outside the root")
        endif()
    endif()
    # With nothing else in the root, the root goes too.
    if(EXISTS "${LC_ROOT}")
        message(FATAL_ERROR "uninstall left the empty root ${LC_ROOT}")
    endif()
endforeach()

# --- a link into the root is removed even when current is gone -----------------

lc_make_install("${TEST_DIR}/dangling")
file(REMOVE "${LC_ROOT}/current")
lc_exec(dangling ENV "HOME=${LC_HOME}" COMMAND "${LC_ROOT}/versions/${LC_VERSION}/bin/neura" uninstall --yes)
lc_expect_status("uninstall with a dangling bin link" dangling 0)
if(IS_SYMLINK "${LC_LINK}" OR EXISTS "${LC_ROOT}")
    message(FATAL_ERROR "uninstall left files after a dangling bin link:\n${dangling_OUT}")
endif()

# --- a leftover that cannot be followed -------------------------------------------

# A symbolic link inside the root that points at a directory outside it is
# removed as a link; its target is not entered.
lc_make_install("${TEST_DIR}/escape")
file(MAKE_DIRECTORY "${TEST_DIR}/escape/outside")
file(WRITE "${TEST_DIR}/escape/outside/precious" "do not delete\n")
file(CREATE_LINK "${TEST_DIR}/escape/outside" "${LC_ROOT}/versions/${LC_VERSION}/share/outside-link" SYMBOLIC)
lc_exec(escape ENV "HOME=${LC_HOME}" COMMAND "${LC_LINK}" uninstall --yes)
lc_expect_status("uninstall with a link out of the root" escape 0)
if(NOT EXISTS "${TEST_DIR}/escape/outside/precious")
    message(FATAL_ERROR "uninstall followed a symbolic link out of the root")
endif()
if(EXISTS "${LC_ROOT}/versions")
    message(FATAL_ERROR "uninstall left the versions directory")
endif()
