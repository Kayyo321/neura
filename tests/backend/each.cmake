cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
file(REAL_PATH "${TEST_DIR}" directory)
set(tests "${SOURCE_DIR}/tests/backend")
set(first "${directory}/first-argument.nvr")
set(all "${directory}/all-arguments.nvr")
set(lists "${directory}/arguments.txt")

function(invoke)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status
        OUTPUT_FILE "${directory}/stdout"
        ERROR_VARIABLE stderr
    )
    file(READ "${directory}/stdout" stdout)
    set(status "${status}" PARENT_SCOPE)
    set(stdout "${stdout}" PARENT_SCOPE)
    set(stderr "${stderr}" PARENT_SCOPE)
endfunction()

function(expect_result label expected_status expected_stdout expected_stderr)
    if(NOT status STREQUAL "${expected_status}" OR NOT stdout STREQUAL "${expected_stdout}" OR
       NOT stderr STREQUAL "${expected_stderr}")
        message(FATAL_ERROR "${label}: expected status ${expected_status}, stdout [${expected_stdout}], "
                            "stderr [${expected_stderr}]; got status ${status}, stdout [${stdout}], stderr [${stderr}]")
    endif()
endfunction()

invoke(build "${tests}/arguments/first-argument.nra" -o "${first}")
expect_result("build first-argument fixture" 0 "" "")
invoke(build "${tests}/each/arguments.nra" -o "${all}")
expect_result("build all-arguments fixture" 0 "" "")

# A comment, spaces and tabs between arguments, bare and quoted text, all
# six escapes, a quoted empty argument, an empty line and a final line
# without LF. A # with leading whitespace is an ordinary bare argument.
file(WRITE "${lists}" "# a comment\n  bare\t\"two words\"  \n")
file(APPEND "${lists}" [=["a\"b\\c\nd\re\tf\x21\xC3\xa9" ""

 #not-a-comment
"#quoted" last]=])
invoke(run --each "${lists}" "${all}")
set(expected "member\n<bare>\n<two words>\nmember\n<a\"b\\c\nd\re\tf!é>\n<>\nmember\nmember\n<#not-a-comment>\nmember\n<#quoted>\n<last>\n")
# CMake may normalize carriage returns in text reads. Compare the exact
# output file to ensure the escaped CR reaches the program as a byte.
file(WRITE "${directory}/expected" "${expected}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${directory}/stdout" "${directory}/expected"
                RESULT_VARIABLE bytes_differ)
if(NOT status STREQUAL "0" OR NOT bytes_differ STREQUAL "0" OR NOT stderr STREQUAL "")
    message(FATAL_ERROR "the argument grammar produced status ${status}, output [${stdout}], stderr [${stderr}]")
endif()

file(WRITE "${lists}" "one extra\n\"two words\"\n#ignored\nhéllo\n")
set(serial_output "")
set(serial_steps 0)
set(serial_peak 0)
set(serial_requests 0)
foreach(value IN ITEMS one "two words" "héllo")
    if(value STREQUAL "one")
        invoke(run --stats "${first}" -- "${value}" extra)
    else()
        invoke(run --stats "${first}" -- "${value}")
    endif()
    if(NOT status STREQUAL "0" OR
       NOT stderr MATCHES "^stats: steps ([0-9]+), peak heap cells ([0-9]+), host requests ([0-9]+), (graphs [0-9]+, neurons [0-9]+, edges [0-9]+, image bytes [0-9]+)\n")
        message(FATAL_ERROR "serial run of ${value}: status ${status}, stderr [${stderr}]")
    endif()
    math(EXPR serial_steps "${serial_steps} + ${CMAKE_MATCH_1}")
    if(CMAKE_MATCH_2 GREATER serial_peak)
        set(serial_peak "${CMAKE_MATCH_2}")
    endif()
    math(EXPR serial_requests "${serial_requests} + ${CMAKE_MATCH_3}")
    set(image_costs "${CMAKE_MATCH_4}")
    string(APPEND serial_output "${stdout}")
endforeach()
invoke(run --each "${lists}" "${first}")
expect_result("member stdout is the concatenation of serial runs" 0 "${serial_output}" "")
invoke(run --stats --each "${lists}" "${first}")
expect_result("aggregated stats" 0 "${serial_output}"
              "stats: members 3, steps ${serial_steps}, peak heap cells ${serial_peak}, host requests ${serial_requests}, ${image_costs}\n")

file(WRITE "${lists}" "one two\n\nthree")
invoke(run --each "${lists}" "${all}" -- common "common two")
expect_result("common arguments come before each member's arguments" 0
              "member\n<common>\n<common two>\n<one>\n<two>\nmember\n<common>\n<common two>\nmember\n<common>\n<common two>\n<three>\n" "")

invoke(run "${first}")
if(NOT status STREQUAL "139")
    message(FATAL_ERROR "expected the standalone empty argument list to trap with 139, got ${status}")
endif()
string(REPLACE "${first}: trap:" "${first}: ${lists}:3: trap:" trap_report "${stderr}")
file(WRITE "${lists}" "before\n# skipped\n\nafter")
invoke(run --each "${lists}" "${first}")
expect_result("trap report uses the file line number and later members still run" 139 "first: before\nfirst: after\n" "${trap_report}")

file(WRITE "${lists}" "# comment\nfailure\nsuccess\nother-failure\nlast")
invoke(run --each "${lists}" "${all}")
expect_result("first nonzero status wins" 27
              "member\n<failure>\nmember\n<success>\nmember\n<other-failure>\nmember\n<last>\n"
              "${all}: ${lists}:2: exit status 27\n${all}: ${lists}:4: exit status 42\n")

function(bad_line body diagnostic)
    file(WRITE "${lists}" "valid\n# a comment\n${body}")
    invoke(run --each "${lists}" "${first}")
    expect_result("line three error" 1 "" "neura: error: ${lists}: line 3: ${diagnostic}\n")
endfunction()

bad_line("value\r\n" "carriage return before the line end")
bad_line([=[bad"quote]=] "quote or backslash in an unquoted argument")
bad_line([=[bad\slash]=] "quote or backslash in an unquoted argument")
bad_line([=["no closing quote]=] "unterminated quoted argument")
bad_line([=["bad\q"]=] "invalid escape")
bad_line([=["bad\x7"]=] "invalid escape")
bad_line([=["bad\xaz"]=] "invalid escape")
bad_line([=["quoted"bare]=] "expected a space after a quoted argument")
bad_line([=[first "\xFF"]=] "argument 2 is not valid UTF-8")
string(ASCII 255 invalid_byte)
bad_line("${invalid_byte}" "argument 1 is not valid UTF-8")

file(WRITE "${lists}" "# only comments\n# and no lists")
invoke(run --each "${lists}" "${first}")
expect_result("no lists" 1 "" "neura: error: ${lists}: no argument lists\n")
file(WRITE "${lists}" "")
invoke(run --each "${lists}" "${first}")
expect_result("empty file" 1 "" "neura: error: ${lists}: no argument lists\n")

string(REPEAT "x\n" 1048577 many)
file(WRITE "${lists}" "${many}")
unset(many)
invoke(run --each "${lists}" "${first}")
expect_result("too many argument lists" 1 "" "neura: error: ${lists}: more than 1048576 argument lists\n")

string(REPEAT "x" 67108865 huge)
file(WRITE "${lists}" "${huge}")
unset(huge)
invoke(run --each "${lists}" "${first}")
expect_result("file exceeds 64 MiB" 1 "" "neura: error: ${lists}: larger than 64 MiB\n")

file(WRITE "${lists}" "one\n")
foreach(mode IN ITEMS --record --replay --profile)
    if(mode STREQUAL "--profile")
        invoke(run --each "${lists}" "${mode}" "${first}")
    else()
        invoke(run --each "${lists}" "${mode}" "${directory}/trace" "${first}")
    endif()
    expect_result("incompatible option ${mode}" 1 "" "neura: error: --each cannot be combined with ${mode}\n")
endforeach()
if(EXISTS "${directory}/trace")
    message(FATAL_ERROR "incompatible --record created a trace")
endif()
invoke(run --each "${lists}" --each "${lists}" "${first}")
expect_result("repeated --each" 1 "" "neura: error: --each given more than once\n")
invoke(check --each "${lists}" "${tests}/arguments/first-argument.nra")
expect_result("--each applies only to run" 1 "" "neura: error: --each applies only to run\n")
