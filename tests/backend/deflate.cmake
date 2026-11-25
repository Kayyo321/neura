# Std.Encoding.Internal.Deflate, the internal DEFLATE, zlib and gzip module, checked against Python's zlib.
#
# A program outside the standard library cannot import an internal module (E0702), so this test copies the
# tools and the standard library into a scratch package and adds tests/backend/deflate/harness.nra to that copy
# as Std.TestSupport.DeflateHarness. It builds tests/backend/deflate/main.nra against the package and runs
# tests/backend/deflate/vectors.py, which generates the vectors and checks every result.
#
# Arguments: NEURA (the neura tool), SOURCE_DIR, TEST_DIR, PYTHON.

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/package/bin")
include("${CMAKE_CURRENT_LIST_DIR}/../tools.cmake")
neura_copy_tools("${NEURA}" "${TEST_DIR}/package/bin")
get_filename_component(binary "${NEURA}" NAME)
set(tool "${TEST_DIR}/package/bin/${binary}")
set(library "${TEST_DIR}/package/share/neura/stdlib")
file(COPY "${SOURCE_DIR}/stdlib/" DESTINATION "${library}")
file(MAKE_DIRECTORY "${library}/std/test-support")
configure_file("${CMAKE_CURRENT_LIST_DIR}/deflate/harness.nra" "${library}/std/test-support/deflate-harness.nra"
    COPYONLY)

# The module stays internal: a program may not import it directly.
file(WRITE "${TEST_DIR}/direct.nra" "let deflate = import:Std.Encoding.Internal.Deflate;\n")
execute_process(COMMAND "${tool}" check "${TEST_DIR}/direct.nra"
    RESULT_VARIABLE direct_result OUTPUT_VARIABLE direct_output ERROR_VARIABLE direct_output)
if(direct_result EQUAL 0 OR NOT direct_output MATCHES "E0702")
    message(FATAL_ERROR "a program imported Std.Encoding.Internal.Deflate (${direct_result}):\n${direct_output}")
endif()

execute_process(COMMAND "${tool}" build "${CMAKE_CURRENT_LIST_DIR}/deflate/main.nra" -o "${TEST_DIR}/main.nvr"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_output)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "building the DEFLATE harness failed (${build_result}):\n${build_output}")
endif()

execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/deflate/vectors.py"
        --neura "${tool}" --image "${TEST_DIR}/main.nvr" --work "${TEST_DIR}/work"
    RESULT_VARIABLE vectors_result OUTPUT_VARIABLE vectors_output ERROR_VARIABLE vectors_output)
message(STATUS "${vectors_output}")
if(NOT vectors_result EQUAL 0)
    message(FATAL_ERROR "DEFLATE vectors failed (${vectors_result})")
endif()
