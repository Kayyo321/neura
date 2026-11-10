# End-to-end evidence for overlapping host I/O and concurrent record and replay
# (docs/lang/Concurrency.md sections 7, 10.4, and 11). The programs under io/
# run against the loopback interface and the real clock:
#
#   echo             a TCP echo service: two server tasks and two client tasks
#   cancelled        tasks parked in read, accept, and sleep, cancelled by a deadline
#   sleep            three one-second sleeps that overlap
#   with-connection  TcpStream.with_connection closing on Ok, on Err, and on cancellation
#
# Each is run live, recorded, and replayed without any capability. The tests
# need loopback sockets: when the sandbox refuses them the programs fail
# loudly (a bind or connect error in the output), and so does this test.
if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, and TEST_DIR")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(programs "${SOURCE_DIR}/tests/concurrency/io")

function(neura prefix)
    execute_process(
        COMMAND "${NEURA}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 120
    )
    set("${prefix}_status" "${status}" PARENT_SCOPE)
    set("${prefix}_stdout" "${stdout}" PARENT_SCOPE)
    set("${prefix}_stderr" "${stderr}" PARENT_SCOPE)
endfunction()

function(must label)
    neura(result ${ARGN})
    if(NOT result_status STREQUAL "0")
        message(FATAL_ERROR "${label}: ${ARGN} failed with status ${result_status}\n${result_stdout}\n${result_stderr}")
    endif()
    set(must_stdout "${result_stdout}" PARENT_SCOPE)
    set(must_stderr "${result_stderr}" PARENT_SCOPE)
endfunction()

# Runs the program with the capabilities it needs, records the run, and
# replays the trace with none. Every output must equal the expected one.
function(run_all name)
    file(READ "${programs}/${name}.expected" expected)
    must("${name}" build "${programs}/${name}.nra" -o "${name}.nvr")

    must("${name}" run ${ARGN} "${name}.nvr")
    if(NOT must_stdout STREQUAL expected OR NOT must_stderr STREQUAL "")
        message(FATAL_ERROR "${name}: the live run printed\n${must_stdout}\nexpected\n${expected}\nstandard error: ${must_stderr}")
    endif()

    must("${name}" run ${ARGN} --record "${name}.trace" "${name}.nvr")
    if(NOT must_stdout STREQUAL expected)
        message(FATAL_ERROR "${name}: the recorded run printed\n${must_stdout}\nexpected\n${expected}")
    endif()
    file(READ "${TEST_DIR}/${name}.trace" trace)
    if(NOT trace MATCHES "^neura-trace 3\nsettings quantum 1024 max-tasks 65536 max-queued-messages 1048576 max-pending-requests 4096\n")
        message(FATAL_ERROR "${name}: the trace does not start with the version 3 header and settings\n${trace}")
    endif()
    if(NOT trace MATCHES "\ndeliver [0-9]+ [0-9]+")
        message(FATAL_ERROR "${name}: the trace records no delivery of an overlapping request")
    endif()

    # No capability at all: the replay answers every request from the trace.
    must("${name}" run --replay "${name}.trace" "${name}.nvr")
    if(NOT must_stdout STREQUAL expected OR NOT must_stderr STREQUAL "")
        message(FATAL_ERROR "${name}: the replay printed\n${must_stdout}\nexpected\n${expected}\nstandard error: ${must_stderr}")
    endif()

    # The quantum and the limits come from the trace, not from the command line.
    must("${name}" run --replay "${name}.trace" --task-quantum 3 --max-tasks 8 --max-pending-requests 1 "${name}.nvr")
    if(NOT must_stdout STREQUAL expected)
        message(FATAL_ERROR "${name}: a replay under another quantum printed\n${must_stdout}")
    endif()

    set(trace "${trace}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# The echo service.
run_all(echo --allow-net)
foreach(service IN ITEMS connect accept read write)
    if(NOT trace MATCHES "\nrequest neura[.]net[.]${service} 2 ")
        message(FATAL_ERROR "echo: the trace holds no version 2 ${service} request")
    endif()
endforeach()
if(NOT trace MATCHES "\nrequest neura[.]net[.]accept 2 \\[[0-9]+\\] task [0-9]+ tick [0-9]+ cancellable\nok [0-9]+\n")
    message(FATAL_ERROR "echo: the accepts are not recorded as cancellable overlapping requests\n${trace}")
endif()

# The same program without the capability fails loudly: nothing is served.
neura(denied run echo.nvr)
if(NOT denied_stdout MATCHES "heard: <no port>" OR denied_stdout MATCHES "hello from client")
    message(FATAL_ERROR "echo: the run without --allow-net should serve nothing\n${denied_stdout}")
endif()

# A trace that records other bytes than the replayed program writes diverges,
# naming the request.
string(REPLACE "hello from client 1" "hello from client 7" tampered "${trace}")
file(WRITE "${TEST_DIR}/tampered.trace" "${tampered}")
neura(diverged run --replay tampered.trace echo.nvr)
if(NOT diverged_status STREQUAL "1" OR
   NOT diverged_stderr MATCHES "replay of tampered.trace diverged: request [0-9]+ \\(neura[.]net[.]write version 2\\) differs in argument 1")
    message(FATAL_ERROR "echo: a tampered trace should stop the replay with status 1\n${diverged_status}\n${diverged_stderr}")
endif()

# A replay reports where the schedule leaves the trace.
string(REGEX REPLACE "\ndeliver ([0-9]+) ([0-9]+)" "\ndeliver 99999 \\2" late "${trace}")
file(WRITE "${TEST_DIR}/late.trace" "${late}")
neura(late run --replay late.trace echo.nvr)
if(late_status STREQUAL "0" OR NOT late_stderr MATCHES "replay of late.trace diverged: ")
    message(FATAL_ERROR "echo: deliveries moved to a tick the run never reaches should diverge\n${late_status}\n${late_stderr}")
endif()

# --each cannot record or replay, for an image with tasks as for any other.
file(WRITE "${TEST_DIR}/lists.txt" "a\nb\n")
foreach(mode IN ITEMS --record --replay)
    neura(each run --each lists.txt ${mode} echo.trace echo.nvr)
    if(each_status STREQUAL "0" OR NOT each_stderr MATCHES "--each cannot be combined with ${mode}")
        message(FATAL_ERROR "echo: --each ${mode} should be refused\n${each_status}\n${each_stderr}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Cancellation: tasks parked in read, accept, and sleep observe Cancelled.
run_all(cancelled --allow-net --allow-clock)
if(NOT trace MATCHES "\nclock [0-9]+ [0-9]+\n" OR NOT trace MATCHES "\ndeliver [0-9]+ [0-9]+ cancelled\n")
    message(FATAL_ERROR "cancelled: the trace records no clock reading or cancelled delivery\n${trace}")
endif()
if(NOT trace MATCHES "\nrequest neura[.]clock[.]sleep 2 \\[60000000000\\] task [0-9]+ tick [0-9]+ cancellable\nerror 15\n")
    message(FATAL_ERROR "cancelled: the sleep was not recorded as cancelled with error 15\n${trace}")
endif()
if(NOT trace MATCHES "\nrequest neura[.]net[.]read 2 \\[[0-9]+, 16, 0\\] task [0-9]+ tick [0-9]+ cancellable\nerror 15\n")
    message(FATAL_ERROR "cancelled: the read was not recorded as cancelled with error 15\n${trace}")
endif()

# ---------------------------------------------------------------------------
# Sleeps overlap: three one-second sleeps take about a second. The program
# checks its own elapsed time against the generous bounds in its text.
run_all(sleep --allow-clock)
if(NOT trace MATCHES "\nrequest neura[.]clock[.]sleep 2 \\[1000000000\\] task [0-9]+ tick [0-9]+ cancellable\nok 0\n")
    message(FATAL_ERROR "sleep: the trace holds no completed sleep\n${trace}")
endif()

# ---------------------------------------------------------------------------
# with_connection closes on Ok, on Err, and on cancellation.
run_all(with-connection --allow-net --allow-clock)

message(STATUS "concurrency io: echo, cancelled, sleep, and with-connection ran live, recorded, and replayed")
