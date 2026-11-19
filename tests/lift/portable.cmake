# Image portability across C++ compilers (docs/runtime/NVM.md, "Execution tiers"). The converter and the
# structural proof build circuits by calling gadgets that add neurons; when two such calls were arguments of one
# call, or operands of one `+`, the compiler chose their order, so GCC and Clang (which builds nvm.wasm and the
# macOS release) built different graphs from the same source, and a runtime built by one rejected the lift
# hints of an image built by the other. tests/lift/portable/graphs.nra uses every gadget that did.
#
# The digest below is of the fixture's graph table as a GCC build on x86-64 Linux wrote it. Every compiler must
# write the same graphs, and lift every hinted block of its own image without a structural rejection: together
# these mean any build proves any other build's hints. When a change to the converter, the circuit library or
# the library code the fixture calls changes the graphs on purpose, build the fixture with GCC and replace the
# digest; a difference that only one compiler shows is an evaluation-order bug at the gadget that differs.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()
set(expected_graphs "e37c8ae7d06c15ee05be97a30e4e2749966e717635936e66ed864a4e9b526882")

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(source "${SOURCE_DIR}/tests/lift/portable/graphs.nra")
set(image "${TEST_DIR}/graphs.nvr")
execute_process(COMMAND "${NEURA}" build "${source}" -o "${image}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "build of the fixture failed with status ${status}\n${stderr}")
endif()
execute_process(COMMAND "${NEURA}" disassemble "${image}" -o "${TEST_DIR}/graphs.nvra" RESULT_VARIABLE status
                ERROR_VARIABLE stderr)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "disassembly failed with status ${status}\n${stderr}")
endif()
file(STRINGS "${TEST_DIR}/graphs.nvra" lines REGEX "^(graph |  node )")
list(LENGTH lines count)
string(JOIN "\n" table ${lines})
string(SHA256 graphs "${table}")
if(NOT graphs STREQUAL expected_graphs)
    message(FATAL_ERROR "the fixture's graph table (${count} lines) has digest ${graphs}, not ${expected_graphs}. "
                        "If the graphs changed on purpose, rebuild with GCC and update tests/lift/portable.cmake; "
                        "otherwise this compiler orders a gadget's neurons differently.")
endif()

set(ENV{NEURA_TIER_EAGER} 1)
execute_process(COMMAND "${NEURA}" run --tier-stats "${image}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status STREQUAL "69")
    message(FATAL_ERROR "the fixture exited with ${status}, not 69\n${stderr}")
endif()
if(NOT stderr MATCHES "hints rejected 0;" OR stderr MATCHES "lifted 0 ")
    message(FATAL_ERROR "the fixture's own hints were rejected:\n${stderr}")
endif()
message(STATUS "${count} graph lines match the GCC digest; every hint lifted")
