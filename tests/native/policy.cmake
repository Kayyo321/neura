cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED NEURA OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_DIR OR NOT IS_ABSOLUTE "${MATHOPS}" OR
   NOT EXISTS "${MATHOPS}")
    message(FATAL_ERROR "Provide NEURA, SOURCE_DIR, TEST_DIR, and the absolute MATHOPS shared-library path")
endif()
file(REMOVE_RECURSE "${TEST_DIR}")
set(launch_dir "${TEST_DIR}/launch-dir")
set(absolute_prefix "${TEST_DIR}/absolute")
file(MAKE_DIRECTORY "${launch_dir}/relative" "${launch_dir}/relative-neighbor" "${absolute_prefix}")
file(WRITE "${launch_dir}/relative/inside.txt" "relative data")
file(WRITE "${absolute_prefix}/inside.txt" "absolute data")
file(WRITE "${launch_dir}/relative-neighbor/outside.txt" "neighbor data")
set(ENV{NEURA_NATIVE_POLICY_TEST} "environment data")
unset(ENV{NEURA_RUN_OPTIONS})

function(invoke prefix directory)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    foreach(field IN ITEMS status stdout stderr)
        set(${prefix}_${field} "${${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

function(build)
    invoke(built "${TEST_DIR}" "${NEURA}" build ${ARGN})
    if(NOT built_status STREQUAL "0")
        message(FATAL_ERROR "build ${ARGN} returned ${built_status}\n${built_stdout}${built_stderr}")
    endif()
    set(build_notes "${built_stderr}" PARENT_SCOPE)
endfunction()

set(groups fs net env clock entropy native)
set(capability_image "${TEST_DIR}/capabilities.nvr")
build("${SOURCE_DIR}/tests/native/policy-capabilities.nra" -o "${capability_image}")

function(check_capabilities executable)
    set(granted ${ARGN})
    set(relative denied)
    set(absolute denied)
    set(neighbor denied)
    if("fs" IN_LIST granted OR "fs-prefixes" IN_LIST granted)
        set(relative "relative data")
        set(absolute "absolute data")
    endif()
    if("fs" IN_LIST granted)
        set(neighbor "neighbor data")
    endif()
    set(expected "fs-relative: ${relative}\nfs-absolute: ${absolute}\nfs-neighbor: ${neighbor}\n")
    foreach(group IN ITEMS net env clock entropy)
        set(value denied)
        if(group IN_LIST granted)
            if(group STREQUAL "env")
                set(value "environment data")
            else()
                set(value allowed)
            endif()
        endif()
        string(APPEND expected "${group}: ${value}\n")
    endforeach()

    invoke(result "${launch_dir}" "${TEST_DIR}/${executable}" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    if("native" IN_LIST granted)
        string(APPEND expected "native: allowed\n")
        if(NOT result_status STREQUAL "0" OR NOT result_stderr STREQUAL "")
            message(FATAL_ERROR "${executable} should allow native access\n${result_stdout}${result_stderr}")
        endif()
    elseif(NOT result_status STREQUAL "134" OR
           NOT result_stderr MATCHES "native access denied: the native capability is not granted")
        message(FATAL_ERROR "${executable} should trap on denied native access\n${result_stdout}${result_stderr}")
    endif()
    if(NOT result_stdout STREQUAL expected)
        message(FATAL_ERROR "${executable}: expected\n${expected}got\n${result_stdout}${result_stderr}")
    endif()
endfunction()

function(denial_notes granted result)
    set(notes "")
    foreach(group IN LISTS groups)
        if(NOT group IN_LIST granted)
            string(APPEND notes
                "neura: note: the image uses ${group} services, which are denied; they fail with CapabilityDenied\n")
        endif()
    endforeach()
    set(${result} "${notes}" PARENT_SCOPE)
endfunction()

# Nothing is denied by default: every group is granted, there is nothing to
# note, and the image can use every service.
build(--native "${capability_image}" -o default)
if(NOT build_notes STREQUAL "")
    message(FATAL_ERROR "a build with no capability option should print no notes\n${build_notes}")
endif()
check_capabilities(default ${groups})

# --deny-all denies every group; each group's note appears.
build(--native --deny-all "${capability_image}" -o denied-all)
denial_notes("" expected_notes)
if(NOT build_notes STREQUAL expected_notes)
    message(FATAL_ERROR "expected the six denial notes\n${build_notes}")
endif()
check_capabilities(denied-all)

foreach(group IN LISTS groups)
    # Denying one group leaves the other five granted and notes only that one.
    set(others ${groups})
    list(REMOVE_ITEM others "${group}")
    build(--native "--deny-${group}" "${capability_image}" -o "deny-${group}")
    check_capabilities("deny-${group}" ${others})
    denial_notes("${others}" expected_notes)
    if(NOT build_notes STREQUAL expected_notes)
        message(FATAL_ERROR "--deny-${group} should note only the ${group} group\n${build_notes}")
    endif()

    # --allow after --deny-all grants exactly that group.
    build(--native --deny-all "--allow-${group}" "${capability_image}" -o "only-${group}")
    check_capabilities("only-${group}" "${group}")
    denial_notes("${group}" expected_notes)
    if(NOT build_notes STREQUAL expected_notes)
        message(FATAL_ERROR "--deny-all --allow-${group} should remove only the ${group} note\n${build_notes}")
    endif()
endforeach()

# The --allow options are still accepted without a --deny option.
build(--native --allow-fs --allow-net --allow-env --allow-clock --allow-entropy --allow-native
      "${capability_image}" -o everything)
if(NOT build_notes STREQUAL "")
    message(FATAL_ERROR "granting every capability should print no notes\n${build_notes}")
endif()
check_capabilities(everything ${groups})

# The relative prefix is absent in the build directory. Its sibling shares
# the same leading characters but falls outside the allowed path component.
build(--native --deny-all "--allow-fs=${absolute_prefix},relative" "${capability_image}" -o prefixed)
check_capabilities(prefixed fs-prefixes)
denial_notes("fs" expected_notes)
if(NOT build_notes STREQUAL expected_notes)
    message(FATAL_ERROR "--allow-fs=prefixes should remove the fs note\n${build_notes}")
endif()

# --allow-fs=prefixes narrows the default grant of files and leaves the other
# groups granted.
build(--native "--allow-fs=${absolute_prefix},relative" "${capability_image}" -o narrowed)
set(others ${groups})
list(REMOVE_ITEM others fs)
check_capabilities(narrowed fs-prefixes ${others})
if(NOT build_notes STREQUAL "")
    message(FATAL_ERROR "narrowing files keeps them granted, so no note\n${build_notes}")
endif()

# Capability options apply in command-line order. The policy is read back with
# `neura inspect`.
function(inspect_capabilities expected)
    build(--native ${ARGN} "${SOURCE_DIR}/examples/hello-world/hello-world.nra" -o order-case)
    invoke(inspected "${TEST_DIR}" "${NEURA}" inspect "${TEST_DIR}/order-case")
    if(NOT inspected_status STREQUAL "0" OR NOT inspected_stdout MATCHES "(^|\n)capabilities: ([^\n]*)\n")
        message(FATAL_ERROR "inspect failed for ${ARGN}\n${inspected_stdout}${inspected_stderr}")
    endif()
    if(NOT CMAKE_MATCH_2 STREQUAL "${expected}")
        message(FATAL_ERROR "${ARGN} should record 'capabilities: ${expected}', got '${CMAKE_MATCH_2}'")
    endif()
endfunction()
set(everything_line "console, process exit, files, network, environment, clock, entropy, native")
inspect_capabilities("${everything_line}")
inspect_capabilities("console, process exit" --deny-all)
inspect_capabilities("console, process exit, files (data), network" --deny-all --allow-net --allow-fs=data)
inspect_capabilities("console, process exit" --allow-net --deny-all)
inspect_capabilities("console, process exit, network, environment, clock, entropy, native" --deny-fs)
inspect_capabilities("console, process exit, files, environment, clock, entropy, native" --deny-net)
inspect_capabilities("console, process exit, files, network, clock, entropy, native" --deny-env)
inspect_capabilities("console, process exit, files, network, environment, entropy, native" --deny-clock)
inspect_capabilities("console, process exit, files, network, environment, clock, native" --deny-entropy)
inspect_capabilities("console, process exit, files, network, environment, clock, entropy" --deny-native)
# A deny followed by an allow of the same group grants it again, and the
# reverse order denies it.
inspect_capabilities("${everything_line}" --deny-net --allow-net)
inspect_capabilities("console, process exit, files, environment, clock, entropy, native" --allow-net --deny-net)
# Files: a list narrows the default, a plain --allow-fs after it widens to every
# path, a plain --allow-fs before it is narrowed, and lists accumulate until a
# --deny-fs or --deny-all clears them.
inspect_capabilities("console, process exit, files (a), network, environment, clock, entropy, native" --allow-fs=a)
inspect_capabilities("${everything_line}" --allow-fs=a --allow-fs)
inspect_capabilities("console, process exit, files (a), network, environment, clock, entropy, native"
    --allow-fs --allow-fs=a)
inspect_capabilities("console, process exit, files (a, b), network, environment, clock, entropy, native"
    --allow-fs=a --allow-fs=b)
inspect_capabilities("console, process exit, files (b), network, environment, clock, entropy, native"
    --allow-fs=a --deny-fs --allow-fs=b)
inspect_capabilities("console, process exit, files (b), network, environment, clock, entropy, native"
    --allow-fs=a --deny-all --allow-fs=b --allow-net --allow-env --allow-clock --allow-entropy --allow-native)
inspect_capabilities("console, process exit, network, environment, clock, entropy, native" --allow-fs=a --deny-fs)

# NEURA_RUN_OPTIONS on a launcher that is not sealed can only restrict, so it
# accepts the --deny options. The restricted run must behave exactly like an
# executable built with the same denial.
function(restricted_by_environment settings built_with)
    set(ENV{NEURA_RUN_OPTIONS} "${settings}")
    invoke(restricted "${launch_dir}" "${TEST_DIR}/default" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    unset(ENV{NEURA_RUN_OPTIONS})
    invoke(reference "${launch_dir}" "${TEST_DIR}/${built_with}" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    # A trap names the program; the two executables have different names.
    string(REGEX REPLACE "(^|\n)[^\n]+: trap:" "\\1<program>: trap:" restricted_stderr "${restricted_stderr}")
    string(REGEX REPLACE "(^|\n)[^\n]+: trap:" "\\1<program>: trap:" reference_stderr "${reference_stderr}")
    if(NOT restricted_status STREQUAL reference_status OR NOT restricted_stdout STREQUAL reference_stdout OR
       NOT restricted_stderr STREQUAL reference_stderr)
        message(FATAL_ERROR "NEURA_RUN_OPTIONS='${settings}' should behave like an executable built with ${built_with}\n"
            "environment (${restricted_status}): ${restricted_stdout}${restricted_stderr}\n"
            "built (${reference_status}): ${reference_stdout}${reference_stderr}")
    endif()
endfunction()
restricted_by_environment("--deny-fs" deny-fs)
restricted_by_environment("--deny-net" deny-net)
restricted_by_environment("--deny-env" deny-env)
restricted_by_environment("--deny-clock" deny-clock)
restricted_by_environment("--deny-entropy" deny-entropy)
restricted_by_environment("--deny-native" deny-native)
restricted_by_environment("--deny-all" denied-all)
build(--native --deny-net --deny-env "${capability_image}" -o deny-env-net)
restricted_by_environment("--deny-net\t--deny-env" deny-env-net)
# Several settings, a --deny repeated, and an observing option together.
restricted_by_environment("--deny-fs --deny-fs --max-steps 100000000\t--deny-clock --deny-net --deny-env --deny-entropy --deny-native"
    denied-all)

# NEURA_RUN_OPTIONS still cannot widen the policy: --allow-* is refused for the
# executable that denies everything as well.
foreach(group IN LISTS groups)
    set(ENV{NEURA_RUN_OPTIONS} "--allow-${group}")
    invoke(widened "${launch_dir}" "${TEST_DIR}/denied-all" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    unset(ENV{NEURA_RUN_OPTIONS})
    if(NOT widened_status STREQUAL "1" OR NOT widened_stdout STREQUAL "" OR
       NOT widened_stderr MATCHES ": error: NEURA_RUN_OPTIONS: --allow-${group} is not accepted by a native executable\n$")
        message(FATAL_ERROR "NEURA_RUN_OPTIONS=--allow-${group} must stay a start-up error\n${widened_stdout}${widened_stderr}")
    endif()
endforeach()

# A sealed executable ignores the variable, --deny options included, so it
# keeps the policy it was built with.
build(--native --sealed "${capability_image}" -o sealed-default)
build(--native --sealed --deny-net "${capability_image}" -o sealed-no-net)
foreach(settings IN ITEMS "--deny-all" "--deny-fs --deny-native" "--allow-fs --allow-net")
    set(ENV{NEURA_RUN_OPTIONS} "${settings}")
    invoke(sealed_all "${launch_dir}" "${TEST_DIR}/sealed-default" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    invoke(sealed_none "${launch_dir}" "${TEST_DIR}/sealed-no-net" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    unset(ENV{NEURA_RUN_OPTIONS})
    invoke(plain_all "${launch_dir}" "${TEST_DIR}/default" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    invoke(plain_no_net "${launch_dir}" "${TEST_DIR}/deny-net" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    if(NOT sealed_all_status STREQUAL "0" OR NOT sealed_all_stdout STREQUAL plain_all_stdout OR
       NOT sealed_all_stderr STREQUAL "" OR NOT sealed_none_status STREQUAL "0" OR
       NOT sealed_none_stdout STREQUAL plain_no_net_stdout OR NOT sealed_none_stderr STREQUAL "")
        message(FATAL_ERROR "a sealed executable should ignore NEURA_RUN_OPTIONS='${settings}'\n"
            "${sealed_all_stdout}${sealed_all_stderr}${sealed_none_stdout}${sealed_none_stderr}")
    endif()
endforeach()
if(NOT plain_all_stdout MATCHES "net: allowed\n" OR NOT plain_no_net_stdout MATCHES "net: denied\n")
    message(FATAL_ERROR "the reference executables should differ in the network\n${plain_all_stdout}${plain_no_net_stdout}")
endif()


set(ENV{NEURA_RUN_OPTIONS} "--allow-fs")
invoke(refused "${launch_dir}" "${TEST_DIR}/default" "${MATHOPS}" "${absolute_prefix}/inside.txt")
unset(ENV{NEURA_RUN_OPTIONS})
if(NOT refused_status STREQUAL "1" OR NOT refused_stdout STREQUAL "" OR
   NOT refused_stderr MATCHES ": error: NEURA_RUN_OPTIONS: --allow-fs is not accepted by a native executable\n$")
    message(FATAL_ERROR "the launcher should reject --allow-fs before running\n${refused_stdout}${refused_stderr}")
endif()

# Malformed settings are start-up errors, and the quantum, which changes the
# interleaving of tasks and so the order of their output, is refused.
function(refused_options settings message_text)
    set(ENV{NEURA_RUN_OPTIONS} "${settings}")
    invoke(rejected "${launch_dir}" "${TEST_DIR}/default" "${MATHOPS}" "${absolute_prefix}/inside.txt")
    unset(ENV{NEURA_RUN_OPTIONS})
    if(NOT rejected_status STREQUAL "1" OR NOT rejected_stdout STREQUAL "" OR
       NOT rejected_stderr MATCHES ": error: NEURA_RUN_OPTIONS: ${message_text}\n$")
        message(FATAL_ERROR "NEURA_RUN_OPTIONS='${settings}' should fail with '${message_text}'\n${rejected_stdout}${rejected_stderr}")
    endif()
endfunction()
refused_options("--task-quantum 1" "--task-quantum is not accepted by a native executable")
refused_options("--task-quantum" "--task-quantum is not accepted by a native executable")
refused_options("--max-steps" "expected a positive integer after --max-steps")
refused_options("--max-steps 0" "expected a positive integer after --max-steps")
refused_options("--max-frames x" "expected a positive integer after --max-frames")
refused_options("--max-tasks -1" "expected a positive integer after --max-tasks")
refused_options("--record" "expected a value after --record")
refused_options("--replay" "expected a value after --replay")
refused_options("--record a --replay b" "--record and --replay cannot be combined")
refused_options("--gpu=auto" "--gpu=auto is not accepted by a native executable")

function(compare_with_cpu name settings status diagnosis)
    invoke(cpu "${TEST_DIR}" "${NEURA}" run --no-gpu --stats ${ARGN} "${TEST_DIR}/${name}.nvr")
    set(ENV{NEURA_RUN_OPTIONS} "${settings}")
    invoke(native "${TEST_DIR}" "${TEST_DIR}/${name}")
    unset(ENV{NEURA_RUN_OPTIONS})
    string(REPLACE "gpu: off\n" "" cpu_stderr "${cpu_stderr}")
    string(REGEX REPLACE "(^|\n)[^\n]+: trap:" "\\1<program>: trap:" cpu_stderr "${cpu_stderr}")
    string(REGEX REPLACE "(^|\n)[^\n]+: trap:" "\\1<program>: trap:" native_stderr "${native_stderr}")
    if(NOT cpu_status STREQUAL status OR NOT native_status STREQUAL status OR
       NOT cpu_stderr MATCHES "${diagnosis}" OR NOT native_stdout STREQUAL cpu_stdout OR
       NOT native_stderr STREQUAL cpu_stderr)
        message(FATAL_ERROR "${name} with NEURA_RUN_OPTIONS='${settings}' should match the CPU runner\n"
            "CPU (${cpu_status}): ${cpu_stdout}${cpu_stderr}\nNative (${native_status}): ${native_stdout}${native_stderr}")
    endif()
    set(compared_stderr "${cpu_stderr}" PARENT_SCOPE)
endfunction()

set(traps "${SOURCE_DIR}/tests/backend/traps")
set(hello "${SOURCE_DIR}/examples/hello-world/hello-world.nra")
build("${traps}/endless-loop.nra" -o steps.nvr)
build(--native --max-steps 1000 steps.nvr -o steps)
compare_with_cpu(steps "--stats" 152 "step limit exceeded: 1000 steps" --max-steps 1000)
compare_with_cpu(steps "--max-steps\t400 --stats" 152 "step limit exceeded: 400 steps" --max-steps 400)
compare_with_cpu(steps "--max-steps 5000 --stats" 152 "step limit exceeded: 1000 steps" --max-steps 1000)

build("${traps}/deep-recursion.nra" -o frames.nvr)
build(--native --max-frames 32 frames.nvr -o frames)
compare_with_cpu(frames "--stats" 139 "frame depth limit exceeded" --max-frames 32)
compare_with_cpu(frames "--max-frames 16 --stats" 139 "frame depth limit exceeded" --max-frames 16)
compare_with_cpu(frames "--max-frames 128 --stats" 139 "frame depth limit exceeded" --max-frames 32)

build("${hello}" -o heap.nvr)
build(--native --max-heap-cells 50 heap.nvr -o heap)
compare_with_cpu(heap "--stats" 137 "heap limit exceeded" --max-heap-cells 50)
set(recorded_heap_stderr "${compared_stderr}")
compare_with_cpu(heap "--max-heap-cells 1 --stats" 137 "heap limit exceeded" --max-heap-cells 1)
if(compared_stderr STREQUAL recorded_heap_stderr)
    message(FATAL_ERROR "the heap fixture should distinguish the lower limit from the recorded limit")
endif()
compare_with_cpu(heap "--max-heap-cells 100000 --stats" 137 "heap limit exceeded" --max-heap-cells 50)

configure_file("${TEST_DIR}/heap.nvr" "${TEST_DIR}/observed.nvr" COPYONLY)
build(--native observed.nvr -o observed)
compare_with_cpu(observed " \t--stats --profile  " 0 "profile: steps by function" --profile)

build(--native --sealed observed.nvr -o sealed)
set(ENV{NEURA_RUN_OPTIONS} "--max-steps 1\t--stats --profile --record sealed.trace --allow-fs")
invoke(sealed "${TEST_DIR}" "${TEST_DIR}/sealed")
unset(ENV{NEURA_RUN_OPTIONS})
if(NOT sealed_status STREQUAL "0" OR NOT sealed_stdout STREQUAL "Hello, World!\n" OR
   NOT sealed_stderr STREQUAL "" OR EXISTS "${TEST_DIR}/sealed.trace")
    message(FATAL_ERROR "a sealed launcher should ignore every environment option\n${sealed_stdout}${sealed_stderr}")
endif()
