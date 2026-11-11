# lifecycle-install: a release archive made by packaging/make-archive.sh and
# installed by its install.sh into a temporary root.
#
# Checks the layout, the marker, the links, the installed tool (version,
# Hello World through the bin link, doctor), the refusals of install.sh, and
# that no update notice or update state appears when the standard streams
# are not terminals. The notice itself, which needs a terminal, is not
# tested here.
if(NOT DEFINED BINARY_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR OR NOT DEFINED NEURA)
    message(FATAL_ERROR "Provide BINARY_DIR, SOURCE_DIR, TEST_DIR, and NEURA")
endif()
include("${SOURCE_DIR}/tests/lifecycle/common.cmake")

lc_make_install("${TEST_DIR}")
set(installer_env "HOME=${LC_HOME}" "NEURA_INSTALL_ROOT=${LC_ROOT}" "NEURA_BIN_DIR=${LC_BIN_DIR}")

# Layout.
foreach(path IN ITEMS
        "versions/${LC_VERSION}/bin/neura"
        "versions/${LC_VERSION}/share/neura/stdlib/std"
        "versions/${LC_VERSION}/share/doc/neura/LICENSE")
    if(NOT EXISTS "${LC_ROOT}/${path}")
        message(FATAL_ERROR "the install has no ${path}")
    endif()
endforeach()
if(EXISTS "${LC_PREFIX}/lib/neura/launchers" AND
   NOT EXISTS "${LC_ROOT}/versions/${LC_VERSION}/lib/neura/launchers/${LC_TRIPLE}")
    message(FATAL_ERROR "the install has no launchers for ${LC_TRIPLE}")
endif()
foreach(path IN ITEMS install.sh VERSION)
    if(NOT EXISTS "${LC_UNPACKED}/${path}")
        message(FATAL_ERROR "the archive has no ${path}")
    endif()
endforeach()
file(READ "${LC_UNPACKED}/VERSION" archive_version)
lc_expect_equal("the archive's VERSION" "${archive_version}" "${LC_VERSION}\n")
if(NOT IS_SYMLINK "${LC_ROOT}/current")
    message(FATAL_ERROR "current is not a symbolic link")
endif()
file(READ_SYMLINK "${LC_ROOT}/current" current_target)
lc_expect_equal("current" "${current_target}" "versions/${LC_VERSION}")
if(NOT IS_SYMLINK "${LC_LINK}")
    message(FATAL_ERROR "${LC_LINK} is not a symbolic link")
endif()
file(READ_SYMLINK "${LC_LINK}" link_target)
lc_expect_equal("the bin link" "${link_target}" "${LC_ROOT}/current/bin/neura")
file(READ "${LC_ROOT}/neura-install.txt" marker)
lc_expect_equal("the marker" "${marker}"
    "neura-install 1\nmethod archive\nlink ${LC_LINK}\nversion ${LC_VERSION}\n")
lc_expect_no_leftovers("install.sh" "${LC_ROOT}")

# The archive is reproducible: packing the same prefix again gives the same bytes.
lc_exec(again COMMAND "${LC_SH}" "${SOURCE_DIR}/packaging/make-archive.sh"
    "${LC_PREFIX}" "${TEST_DIR}/archives-again" "${LC_VERSION}" "${LC_TRIPLE}")
lc_expect_status("make-archive.sh again" again 0)
file(SHA256 "${LC_ARCHIVES}/neura-${LC_VERSION}-${LC_TRIPLE}.tar.gz" first_digest)
file(SHA256 "${TEST_DIR}/archives-again/neura-${LC_VERSION}-${LC_TRIPLE}.tar.gz" second_digest)
lc_expect_equal("the digest of the archive packed twice" "${second_digest}" "${first_digest}")

# Release metadata.
lc_exec(metadata COMMAND "${LC_SH}" "${SOURCE_DIR}/packaging/make-release-metadata.sh" "${LC_ARCHIVES}" "${LC_VERSION}")
lc_expect_status("make-release-metadata.sh" metadata 0)
file(SHA256 "${LC_ARCHIVES}/neura-${LC_VERSION}-${LC_TRIPLE}.tar.gz" archive_digest)
file(SIZE "${LC_ARCHIVES}/neura-${LC_VERSION}-${LC_TRIPLE}.tar.gz" archive_size)
file(READ "${LC_ARCHIVES}/neura-release.txt" release_text)
lc_expect_equal("neura-release.txt" "${release_text}"
    "neura-release 1\nversion ${LC_VERSION}\nasset ${LC_TRIPLE} neura-${LC_VERSION}-${LC_TRIPLE}.tar.gz ${archive_digest} ${archive_size}\n")
file(READ "${LC_ARCHIVES}/SHA256SUMS" sums_text)
lc_expect_equal("SHA256SUMS" "${sums_text}" "${archive_digest}  neura-${LC_VERSION}-${LC_TRIPLE}.tar.gz\n")

# The installed tool, through the bin link.
lc_exec(version COMMAND "${LC_LINK}" --version)
lc_expect_status("installed neura --version" version 0)
lc_expect_equal("installed neura --version" "${version_OUT}" "${LC_VERSION_LINE}")

file(MAKE_DIRECTORY "${TEST_DIR}/hello")
lc_exec(build WORKING_DIRECTORY "${TEST_DIR}/hello"
    COMMAND "${LC_LINK}" build "${SOURCE_DIR}/examples/hello-world/hello-world.nra" -o hello.nvr)
lc_expect_status("building Hello World through the bin link" build 0)
lc_expect_equal("the build's standard error" "${build_ERR}" "")
lc_exec(run WORKING_DIRECTORY "${TEST_DIR}/hello" COMMAND "${LC_LINK}" run hello.nvr)
lc_expect_status("running Hello World through the bin link" run 0)
lc_expect_equal("Hello World output" "${run_OUT}" "Hello, World!\n")
lc_expect_equal("Hello World standard error" "${run_ERR}" "")

lc_exec(doctor COMMAND "${LC_LINK}" doctor)
lc_expect_status("neura doctor" doctor 0)
lc_expect_contains("neura doctor" "${doctor_OUT}" "ok  install: method archive, root ${LC_ROOT}")
lc_expect_contains("neura doctor" "${doctor_OUT}" "ok  bin-link: ${LC_LINK}")
lc_expect_contains("neura doctor" "${doctor_OUT}" "ok  stdlib: ")
if(doctor_OUT MATCHES "(^|\n)FAIL")
    message(FATAL_ERROR "neura doctor reported a failure:\n${doctor_OUT}")
endif()

# No notice and no update state without terminals, with CI and the opt-out unset.
lc_exec(quiet ENV --unset=CI --unset=NEURA_NO_UPDATE_CHECK "HOME=${LC_HOME}"
    WORKING_DIRECTORY "${TEST_DIR}/hello" COMMAND "${LC_LINK}" check "${SOURCE_DIR}/examples/hello-world/hello-world.nra")
lc_expect_status("neura check through the bin link" quiet 0)
lc_expect_equal("neura check standard output" "${quiet_OUT}" "")
lc_expect_equal("neura check standard error" "${quiet_ERR}" "")
if(EXISTS "${LC_ROOT}/update-check.txt")
    message(FATAL_ERROR "a command wrote update-check.txt without a terminal")
endif()

# install.sh refuses to run twice for one version, and changes nothing.
lc_snapshot(before "${LC_ROOT}")
lc_snapshot(bin_before "${LC_BIN_DIR}")
lc_exec(twice ENV ${installer_env} WORKING_DIRECTORY "${LC_UNPACKED}" COMMAND "${LC_SH}" ./install.sh)
if(twice_STATUS EQUAL 0)
    message(FATAL_ERROR "a second install.sh without --force succeeded")
endif()
lc_expect_contains("the second install.sh" "${twice_ERR}" "already installed")
lc_snapshot(after "${LC_ROOT}")
lc_snapshot(bin_after "${LC_BIN_DIR}")
lc_expect_equal("the install root after a refused install" "${after}" "${before}")
lc_expect_equal("the bin directory after a refused install" "${bin_after}" "${bin_before}")

# --force replaces the copy and leaves a working install.
lc_exec(forced ENV ${installer_env} WORKING_DIRECTORY "${LC_UNPACKED}" COMMAND "${LC_SH}" ./install.sh --force)
lc_expect_status("install.sh --force" forced 0)
lc_snapshot(forced_state "${LC_ROOT}")
lc_expect_equal("the install root after --force" "${forced_state}" "${before}")

# --help documents the environment variables.
lc_exec(help WORKING_DIRECTORY "${LC_UNPACKED}" COMMAND "${LC_SH}" ./install.sh --help)
lc_expect_status("install.sh --help" help 0)
lc_expect_contains("install.sh --help" "${help_OUT}" "NEURA_INSTALL_ROOT")
lc_expect_contains("install.sh --help" "${help_OUT}" "NEURA_BIN_DIR")

# A regular file at the bin path is not overwritten, and nothing is installed.
file(MAKE_DIRECTORY "${TEST_DIR}/bin-file")
file(WRITE "${TEST_DIR}/bin-file/neura" "not a link\n")
lc_exec(blocked ENV "HOME=${LC_HOME}" "NEURA_INSTALL_ROOT=${TEST_DIR}/root-blocked" "NEURA_BIN_DIR=${TEST_DIR}/bin-file"
    WORKING_DIRECTORY "${LC_UNPACKED}" COMMAND "${LC_SH}" ./install.sh)
if(blocked_STATUS EQUAL 0)
    message(FATAL_ERROR "install.sh overwrote a regular file in the bin directory")
endif()
file(READ "${TEST_DIR}/bin-file/neura" kept)
lc_expect_equal("the regular file at the bin path" "${kept}" "not a link\n")
if(EXISTS "${TEST_DIR}/root-blocked/versions" OR EXISTS "${TEST_DIR}/root-blocked/neura-install.txt")
    message(FATAL_ERROR "install.sh installed files although it refused to link")
endif()

# A link to somewhere outside the root is not replaced either.
file(MAKE_DIRECTORY "${TEST_DIR}/bin-elsewhere")
file(WRITE "${TEST_DIR}/elsewhere.txt" "elsewhere\n")
file(CREATE_LINK "${TEST_DIR}/elsewhere.txt" "${TEST_DIR}/bin-elsewhere/neura" SYMBOLIC)
lc_exec(outside ENV "HOME=${LC_HOME}" "NEURA_INSTALL_ROOT=${TEST_DIR}/root-outside" "NEURA_BIN_DIR=${TEST_DIR}/bin-elsewhere"
    WORKING_DIRECTORY "${LC_UNPACKED}" COMMAND "${LC_SH}" ./install.sh)
if(outside_STATUS EQUAL 0)
    message(FATAL_ERROR "install.sh replaced a link that points outside the install root")
endif()
file(READ_SYMLINK "${TEST_DIR}/bin-elsewhere/neura" outside_target)
lc_expect_equal("the link that points elsewhere" "${outside_target}" "${TEST_DIR}/elsewhere.txt")

# A root owned by a package manager is not touched.
file(MAKE_DIRECTORY "${TEST_DIR}/root-owned")
file(WRITE "${TEST_DIR}/root-owned/neura-install.txt" "neura-install 1\nmethod homebrew\nlink /opt/homebrew/bin/neura\nversion 0.0.1\n")
lc_exec(owned ENV "HOME=${LC_HOME}" "NEURA_INSTALL_ROOT=${TEST_DIR}/root-owned" "NEURA_BIN_DIR=${TEST_DIR}/bin-owned"
    WORKING_DIRECTORY "${LC_UNPACKED}" COMMAND "${LC_SH}" ./install.sh)
if(owned_STATUS EQUAL 0)
    message(FATAL_ERROR "install.sh installed into a root owned by homebrew")
endif()
lc_expect_contains("install.sh in a homebrew root" "${owned_ERR}" "homebrew")
if(EXISTS "${TEST_DIR}/root-owned/versions")
    message(FATAL_ERROR "install.sh wrote into a root owned by homebrew")
endif()
